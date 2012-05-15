/*
 *  Copyright (c) 2004-2010 Bonelli Nicola <bonelli@antifork.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <list>
#include <vector>

#include <algorithm>
#include <iterator>
#include <iomanip>

#include <getopt.h>

#include <colorful.hpp>      // more
#include <iomanip.hpp>       // more
#include <string-utils.hpp>  // more

#include <proc_net_wireless.hpp>
#include <proc_interrupt.hpp>
#include <proc_net_dev.hpp>
#include <ifr.hpp>        

extern "C" {
#include <pci/pci.h>
}

extern char *__progname;
static const char * version = "1.1";

typedef more::colorful< more::ecma::bold > BOLD;
typedef more::colorful< more::ecma::reset > RESET;


template <typename CharT, typename Traits, typename Fun>
void pretty_print(std::basic_ostream<CharT, Traits> &out, size_t sp, Fun fun)
{
    out << more::spaces(sp);
    try 
    {
        fun();
    }
    catch(std::exception &e)
    {
        out << "info:" << e.what() << " ";
    }
}


template <typename CharT, typename Traits, typename Fun>
void pretty_printLn(std::basic_ostream<CharT, Traits> &out, size_t sp, Fun fun)
{
    pretty_print(out,sp,fun);
    out << std::endl;
}


int
show_interfaces(bool all, bool verbose, const std::list<std::string> &if_list = std::list<std::string>())
{
    auto ifs = proc::get_if_list();

    auto longest = std::max_element(ifs.begin(), ifs.end(), [](const std::string &lhs, const std::string &rhs) {
                                        return lhs.length() < rhs.length();
                                        }
                                    );

    size_t indent = longest->length() + 2;

    struct pci_access *pacc = pci_alloc();

    // initialize pci library...

    char name_path[] = "/usr/share/misc/pci.ids";

    pci_init(pacc);
    pci_scan_bus(pacc);
    pci_set_name_list_path(pacc, name_path, 0);

    // create a pci filter...
    struct pci_filter filter;
    pci_filter_init(pacc, &filter);

    // buffers for pci functions...
    //
    char pci_namebuf[1024], pci_classbuf[128];

    int devnum = 0;

    for(auto & name : proc::get_if_list()) {

        std::cout << RESET();

        if (devnum++)
            std::cout << std::endl;

        // build the interface by name
        //
        net::ifr iif(name);

        // in case the list is given, skip the interface if not included
        //
        if (!if_list.empty() &&
            find(if_list.begin(), if_list.end(), name) == if_list.end())
            continue;

        // display the interface when it's UP or -a is passed at command line
        //
        if (!all && (iif.flags() & IFF_UP) == 0 && 
            find(if_list.begin(), if_list.end(), name) == if_list.end())
            continue;

        pretty_print(std::cout, 0, [&] {

            // display the interface name
            //
            std::cout << std::left << std::setw(indent-1) << name << ' ' << std::flush;    
            
            auto ecmd = iif.ethtool_command();
            if (iif.ethtool_link())
                std::cout << BOLD();

            // display Link-Speed (if supported)
            //
            if (ecmd) {

                uint32_t speed = ethtool_cmd_speed(ecmd.get());
                
                std::cout << "Link:" << (iif.ethtool_link() ? "yes " : "no ");

                if (speed != 0 && speed != (uint16_t)(-1) && speed != (uint32_t)(-1))
                    std::cout << "Speed:" << speed << "Mb/s ";
               
                // display half/full duplex...
                std::cout << "Duplex:"; 
                switch(ecmd->duplex)
                {
                case DUPLEX_HALF:
                    std::cout << "Half "; break;
                case DUPLEX_FULL:
                    std::cout << "Full "; break;
                default:
                    std::cout << "Unknown "; break;
                }

                // display port...
                std::cout << "Port:"; 
                switch (ecmd->port) {
                case PORT_TP:
                    std::cout << "Twisted Pair "; break;
                case PORT_AUI:
                    std::cout << "AUI "; break;
                case PORT_BNC:
                    std::cout << "BNC "; break;
                case PORT_MII:
                    std::cout << "MII "; break;
                case PORT_FIBRE:
                    std::cout << "FIBRE "; break;
                // case PORT_DA:
                //    std::cout << "Direct Attach Copper "; break;
                //case PORT_NONE:
                //   std::cout << "None "; break;
                //case PORT_OTHER:
                //    std::cout << "Other "; break;
                default:
                    std::cout << "Unknown "; break;
                }
            }

        });

        pretty_printLn(std::cout, 0, [&] {

            // display HWaddr
            //
            std::cout << "HWaddr " << iif.mac();

        });

        std::cout << RESET();

        // display wireless config if avaiable
        //
        char buffer[128];
        
        pretty_printLn(std::cout, indent, [&] {
            
            wireless_info winfo = iif.wifi_info();
            std::cout << winfo.b.name << " ESSID:" << winfo.b.essid << " Mode:" << 
                iw_operation_mode[winfo.b.mode] << " Frequency:" << winfo.b.freq; 
        
            if (winfo.has_bitrate) {
                    iw_print_bitrate(buffer,sizeof(buffer), winfo.bitrate.value);
                    std::cout << "Bit-Rate:" << buffer << ' ';
            }

            if (winfo.has_ap_addr) {
                    std::cout << "Access Point: " << iw_sawap_ntop(&winfo.ap_addr, buffer);
            }
                
        });    

        // display wireless info, if available
        //
        auto wi = proc::get_wireless(name);
        if ( std::get<0>(wi) != 0.0 ||
             std::get<1>(wi) != 0.0 ||
             std::get<2>(wi) != 0.0 ||
             std::get<3>(wi) != 0.0 )
        {

            pretty_printLn(std::cout, indent, [&] {

                std::cout << "wifi_status:" << std::get<0>(wi) <<
                            " link:" << std::get<1>(wi) << " level:" << std::get<2>(wi) << 
                            " noise:" << std::get<3>(wi); 
            });
        }

        // display flags, mtu and metric
        //
        pretty_printLn(std::cout, indent, [&] {
        
            std::cout << iif.flags_str() << "MTU:" << iif.mtu() << " Metric:" << iif.metric(); 

        });

        
        // display inet addr if set
        //
        std::string inet_addr = iif.inet_addr<SIOCGIFADDR>();
        if ( inet_addr.size() ) {

            pretty_printLn(std::cout, indent, [&] {
        
                std::cout << "inet addr:" << inet_addr  
                    << " Bcast:" << iif.inet_addr<SIOCGIFBRDADDR>() 
                    << " Mask:" << iif.inet_addr<SIOCGIFNETMASK>(); 

            });
        }

        // display inet6 addr if set
        //
        std::string inet6_addr = iif.inet6_addr();
        if (inet6_addr.size()) {

            pretty_printLn(std::cout, indent, [&] {
            
                std::cout << inet6_addr;
       
            });
        }
       
        pretty_printLn(std::cout, indent, [&] {

            // display map info
            //
            ifmap m = iif.map();
            std::cout << std::hex <<"base_addr:0x" << m.base_addr;
            if (m.mem_start)
                std::cout << " memory:0x" << m.mem_start << "-0x" << m.mem_end;

            std::cout << std::dec << " irq:" << static_cast<int>(m.irq);

            // display irq events per cpu
            //
            auto cpuint = proc::get_interrupt_counter(static_cast<int>(m.irq));
            int n = 0;
            for(auto ci : cpuint) {
                std::cout << " cpu" << n++ << "=" << ci;
            }

            std::cout << " dma:" << static_cast<int>(m.dma) 
                      << " port:"<< static_cast<int>(m.port) << std::dec;
        });
            

        net::ifr::stats s;
        pretty_printLn(std::cout, indent, [&] {

            s = iif.get_stats();
            // display stats
            //
            std::cout << "Rx bytes:" << s.rx_bytes << " packets:" << s.rx_packets << 
                         " errors:" << s.rx_errs << " dropped:" << s.rx_drop << 
                         " overruns: " << s.rx_fifo << " frame:" << s.rx_frame;
        });    
                         
        pretty_printLn(std::cout, indent, [&] {
            std::cout << "Tx bytes:" << s.tx_bytes << " packets:" << s.tx_packets << 
                         " errors:" << s.tx_errs << " dropped:" << s.tx_drop << 
                         " overruns: " << s.tx_fifo;
            
        });    

        pretty_printLn(std::cout, indent, [&] {
            std::cout << "colls:" << s.tx_colls << " txqueuelen:" << iif.txqueuelen();
        });
                     

        // ... display drvinfo if available
        //
        
        pretty_printLn(std::cout, indent, [&] {
        
            auto info = iif.ethtool_info();
            if (info) {

                char bus_info[16];
                strncpy(bus_info, info->bus_info, sizeof(bus_info)-1);

                // set filter (and display additional pci info, if available)... 
                //
                if (!pci_filter_parse_slot(&filter, bus_info) )
                {
                    struct pci_dev *dev = pacc->devices;
                    for(;dev; dev=dev->next)
                    {
                        pci_fill_info(dev, PCI_FILL_IDENT | PCI_FILL_BASES | PCI_FILL_CLASS); /* Fill in header info we need */
                        if ( pci_filter_match(&filter, dev) )
                            break;
                    }
                    
                    if (dev) { // got it!!!

                        char *pci_name, *pci_class;
                        pci_class = pci_lookup_name(pacc, pci_classbuf, sizeof(pci_classbuf), 
                                                    PCI_LOOKUP_CLASS, dev->device_class);
                        pci_name = pci_lookup_name(pacc, pci_namebuf, sizeof(pci_namebuf), 
                                                   PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE, dev->vendor_id, dev->device_id);

                        std::cout << std::hex 
                        << pci_class << ": " << pci_name << std::dec << std::endl
                        << more::spaces(indent)  << "vendor_id: " << dev->vendor_id 
                        << " device_id: " << dev->device_id << " device_class: " << dev->device_class << std::endl; 
                    }
                }

                pretty_print(std::cout, indent, [&] {
                    // display ethertool_drivinfo...
                    //
                    std::cout << "ether_driver:" << info->driver << " version:" << info->version;  
                    if (strlen(info->fw_version))
                        std::cout << " firmware:" << info->fw_version; 
                    if (strlen(info->bus_info)) 
                    std::cout << " bus:" << info->bus_info;
                });
            }

        });

    }
    pci_cleanup(pacc);    
    std::cout << RESET();
    
    return 0;
}


static 
const char usage_str[] = "\
Usage:%s [options]\n\
   -a, --all            display all interfaces\n\
   -v, --verbose        increase verbosity\n\
   -V, --version        display the version and exit\n\
   -h, --help           print this help\n";


static const struct option long_options[] = {
    {"verbose",  no_argument, NULL, 'v'},
    {"version",  no_argument, NULL, 'V'},
    {"all",      no_argument, NULL, 'S'},
    {"help",     no_argument, NULL, 'h'},
    { NULL,      0          , NULL,  0 }};


int
main(int argc, char *argv[])
{
    bool all = false;
    bool verb = false;

    int i;
    while ((i = getopt_long(argc, argv, "hVva", long_options, 0)) != EOF)
        switch (i) {
        case 'h':
            printf(usage_str, __progname);
            exit(0);
        case 'v':
            verb = true;
            break;
        case 'V':
            fprintf(stderr, "%s %s\n", __progname ,version);
            exit(0);
        case 'a':
            all=true;
            break;
        case '?':
            throw std::runtime_error("unknown option"); 
        }

    argc -= optind;
    argv += optind;

    std::list<std::string> ifs;

    while( argc > 0 ) {
        ifs.push_back(std::string(argv[0]));
        argc--;
        argv++;
    }

    return show_interfaces(all, verb, ifs);
}

