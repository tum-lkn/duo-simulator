// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#include "config.h"
#include <sstream>
#include <strstream>
#include <fstream> // need to read flows
#include <iostream>
#include <string.h>
#include <math.h>
#include "network.h"
#include "randomqueue.h"
//#include "subflow_control.h"
//#include "shortflows.h"
#include "pipe.h"
#include "eventlist.h"
#include "logfile.h"
#include "loggers.h"
#include "clock.h"
#include "tcp.h"
#include "compositequeue.h"
#include "queue.h"
//#include "firstfit.h"
#include "topology.h"
//#include "connection_matrix.h"

// Choose the topology here:
#include "expander_topology.h"

#include <list>

// Simulation params

#define PRINT_PATHS 0

#define PERIODIC 0
#include "main.h"

uint32_t RTT_rack = 0; // ns
uint32_t RTT_net = 500; // ns

#define DEFAULT_PACKET_SIZE 1500 // MAXIMUM FULL packet size (includes header + payload), Bytes
#define DEFAULT_HEADER_SIZE 64 // header size, Bytes

#define DEFAULT_QUEUE_SIZE 8

string ntoa(double n); // convert a double to a string
string itoa(uint64_t n); // convert an int to a string

EventList eventlist;

Logfile* lg;

void exit_error(char* progr){
    cout << "Usage " << progr << " [UNCOUPLED(DEFAULT)|COUPLED_INC|FULLY_COUPLED|COUPLED_EPSILON] [epsilon][COUPLED_SCALABLE_TCP" << endl;
    exit(1);
}

void print_path(std::ofstream &paths, const Route* rt){
    for (unsigned int i=1;i<rt->size()-1;i+=2){
	RandomQueue* q = (RandomQueue*)rt->at(i);
	if (q!=NULL)
	    paths << q->str() << " ";
	else 
	    paths << "NULL ";
    }

    paths<<endl;
}

int main(int argc, char **argv) {
	// set packet size in bytes:
    Packet::set_packet_size(DEFAULT_PACKET_SIZE - DEFAULT_HEADER_SIZE);
    mem_b queuesize = DEFAULT_QUEUE_SIZE * DEFAULT_PACKET_SIZE;

    // cwnd = 30
    int cwnd = 30;
    stringstream filename(ios_base::out);
    string flowfile; // so we can read the flows from a specified file
    string topfile; // so we can read the topology from a specified file
    double pull_rate; // so we can set the pull rate from the command line
    double simtime; // seconds
    double utiltime = .01; // seconds
    int VLB; // use VLB routing (for large flows) ? (input as a flag)

    double rto_ms = 10;
    mem_b high_prio_flowsize = 0;

    int i = 1;
    filename << "logout.dat";
    RouteStrategy route_strategy = NOT_SET;

    // parse the command line flags:
    while (i<argc) {
	if (!strcmp(argv[i],"-o")){
	    filename.str(std::string());
	    filename << argv[i+1];
	    i++;
	} else if (!strcmp(argv[i],"-cwnd")){
	    cwnd = atoi(argv[i+1]);
	    i++;
	} else if (!strcmp(argv[i],"-q")){
	    queuesize = atoi(argv[i+1]) * DEFAULT_PACKET_SIZE;
	    i++;
	} else if (!strcmp(argv[i],"-strat")){
	    if (!strcmp(argv[i+1], "perm")) {
		route_strategy = SCATTER_PERMUTE;
	    } else if (!strcmp(argv[i+1], "rand")) {
		route_strategy = SCATTER_RANDOM;
	    } else if (!strcmp(argv[i+1], "pull")) {
		route_strategy = PULL_BASED;
	    } else if (!strcmp(argv[i+1], "single")) {
		route_strategy = SINGLE_PATH;
	    }
	    i++;
    } else if (!strcmp(argv[i],"-flowfile")) {
        flowfile = argv[i+1];
        i++;
    } else if (!strcmp(argv[i],"-topfile")) {
            topfile = argv[i+1];
            i++;
    } else if (!strcmp(argv[i],"-pullrate")) {
        pull_rate = atof(argv[i+1]);
        i++;
    } else if (!strcmp(argv[i],"-VLB")) {
        VLB = atoi(argv[i+1]);
        i++;
    } else if (!strcmp(argv[i],"-rto")) {
        rto_ms = atof(argv[i+1]);
        i++;
    } else if (!strcmp(argv[i],"-hpflowsize")) {
        high_prio_flowsize = atof(argv[i+1]);
        i++;
    } else if (!strcmp(argv[i],"-simtime")) {
        simtime = atof(argv[i+1]);
        i++;
    } else if (!strcmp(argv[i],"-utiltime")) {
        utiltime = atof(argv[i+1]);
        i++;
	} else {
	    exit_error(argv[0]);
	}
	i++;
    }
    srand(13);

    eventlist.setEndtime(timeFromSec(simtime));
    Clock c(timeFromSec(5 / 100.), eventlist);


    route_strategy = SCATTER_RANDOM; // this is the only one that works with VLB right now
      
    if (route_strategy == NOT_SET) {
	fprintf(stderr, "Route Strategy not set.  Use the -strat param.  \nValid values are perm, rand, pull, rg and single\n");
	exit(1);
    }

    //cout << "cwnd " << cwnd << endl;
      
    // prepare the loggers
    //cout << "Logging to " << filename.str() << endl;
    //Logfile 
    Logfile logfile(filename.str(), eventlist);

#if PRINT_PATHS
    filename << ".paths";
    cout << "Logging path choices to " << filename.str() << endl;
    std::ofstream paths(filename.str().c_str());
    if (!paths){
	cout << "Can't open for writing paths file!"<<endl;
	exit(1);
    }
#endif

    lg = &logfile;

    logfile.setStartTime(timeFromSec(100)); // record events starting at this simulator time
    // NdpSinkLoggerSampling object will iterate through all NdpSinks and log their rate every
    // X milliseconds. This is used to get throughput measurements after the experiment ends.
    TcpSinkLoggerSampling sinkLogger = TcpSinkLoggerSampling(timeFromMs(10), eventlist);
    logfile.addLogger(sinkLogger);
    TcpTrafficLogger traffic_logger = TcpTrafficLogger();
    logfile.addLogger(traffic_logger);

    // NdpSinkLoggerSampling object will iterate through all NdpSinks and log their rate every
    // X microseconds. This is used to get throughput measurements after the experiment ends.
    TcpRtxTimerScanner tcpRtxScanner(timeFromMs(1), eventlist);

    NdpSinkLoggerSampling ndpSinkLogger = NdpSinkLoggerSampling(timeFromUs(50.), eventlist);
    logfile.addLogger(ndpSinkLogger);
    NdpTrafficLogger ndp_traffic_logger = NdpTrafficLogger();
    logfile.addLogger(ndp_traffic_logger);
    NdpRtxTimerScanner ndpRtxScanner(timeFromMs(1), eventlist);

// this creates the Expander topology
#ifdef EXPANDER
    ExpanderTopology* top = new ExpanderTopology(queuesize, &logfile, &eventlist, ENHANCED_COMPOSITE, topfile);
#endif

	// initialize all sources/sinks

    CompositeQueue::set_high_prio_flowsize(high_prio_flowsize);
    PriorityQueue::set_high_prio_flowsize(high_prio_flowsize);

    NdpSrc::setMinRTO(1000); // microseconds
    NdpSrc::setRouteStrategy(route_strategy);
    NdpSink::setRouteStrategy(route_strategy);

    NdpSrc *ndpSrc;
    NdpSink *ndpSink;

    // Prepare a pacer for each host
    vector<NdpPullPacer*> pacers;
    for (int j=0; j<top->no_of_nodes(); j++)
        // Set the pull rate to something reasonable.
        // we can be more aggressive if the load is lower
        pacers.push_back(new NdpPullPacer(eventlist, 1));


    ifstream input(flowfile);
    if (input.is_open()){
        string line;
        int64_t temp;
        // get flows. Format: (src) (dst) (bytes) (starttime microseconds)
        while(!input.eof()){
            vector<int64_t> vtemp;
            getline(input, line);
            stringstream stream(line);
            while (stream >> temp)
                vtemp.push_back(temp);
            //cout << "src = " << vtemp[0] << " dest = " << vtemp[1] << " bytes " << vtemp[2] << " time " << vtemp[3] << endl;
            
            // source and destination hosts for this flow
            int flow_src = vtemp[0];
            int flow_dst = vtemp[1];

            if (vtemp[2] >= high_prio_flowsize) {
                auto tcpSrc = new TcpSrc(NULL, nullptr, eventlist, flow_src, flow_dst);
                auto tcpSnk = new TcpSink();

                tcpSrc->set_ssthresh(10 * Packet::data_packet_size());
                tcpSrc->set_flowsize(vtemp[2]);

                tcpSrc->_rto = timeFromMs(rto_ms);

                tcpRtxScanner.registerTcp(*tcpSrc);

                Route* routeout, *routein;

                // debug:
                //cout << "flow: src = " << flow_src << ", dst = " << flow_dst << ", size = " << vtemp[2] << endl;

                vector<const Route*>* srcpaths;
                if (VLB==1) {
                    srcpaths = top->get_paths(flow_src, flow_dst, vtemp[2]>100000);
                    // tcpSrc->setvlb(vtemp[2]>100000);
                }
                else {
                    srcpaths = top->get_paths(flow_src, flow_dst, false);
                    // tcpSrc->setvlb(false);
                }
                int choice = rand()%srcpaths->size();
                routeout = new Route(*(srcpaths->at(choice)));
                routeout->push_back(tcpSnk);

                vector<const Route*>* dstpaths;
                if (VLB==1)
                    dstpaths = top->get_paths(flow_dst, flow_src, vtemp[2]>100000);
                else
                    dstpaths = top->get_paths(flow_dst, flow_src, false);
                routein = new Route(*(dstpaths->at(choice)));
                routein->push_back(tcpSrc);

                tcpSrc->connect(*routeout, *routein, *tcpSnk, timeFromNs(vtemp[3]/1.));

                // tcpSrc->set_num_shortest_paths(top->get_num_shortest_paths(flow_src, flow_dst));
                // tcpSnk->set_num_shortest_paths(top->get_num_shortest_paths(flow_dst, flow_src));

                // tcpSrc->set_paths(srcpaths);
                // tcpSnk->set_paths(dstpaths);
                sinkLogger.monitorSink(tcpSnk);

            } else {
                ndpSrc = new NdpSrc(NULL, NULL, eventlist, flow_src, flow_dst);
                ndpSrc->setCwnd(cwnd * Packet::data_packet_size()); // congestion window
                ndpSrc->set_flowsize(vtemp[2]); // bytes

                // Use the same pacer per destination host
                ndpSink = new NdpSink(pacers[flow_dst]);
                ndpRtxScanner.registerNdp(*ndpSrc);

                Route* routeout, *routein;

                // debug:
                //cout << "flow: src = " << flow_src << ", dst = " << flow_dst << ", size = " << vtemp[2] << endl;

                vector<const Route*>* srcpaths;
                if (VLB==1) {
                    srcpaths = top->get_paths(flow_src, flow_dst, vtemp[2]>100000);
                    ndpSrc->setvlb(vtemp[2]>100000);
                }
                else {
                    srcpaths = top->get_paths(flow_src, flow_dst, false);
                    ndpSrc->setvlb(false);
                }
                routeout = new Route(*(srcpaths->at(0)));
                routeout->push_back(ndpSink);

                vector<const Route*>* dstpaths;
                if (VLB==1)
                    dstpaths = top->get_paths(flow_dst, flow_src, vtemp[2]>100000);
                else
                    dstpaths = top->get_paths(flow_dst, flow_src, false);
                routein = new Route(*(dstpaths->at(0)));
                routein->push_back(ndpSrc);

                ndpSrc->connect(*routeout, *routein, *ndpSink, timeFromNs(vtemp[3]/1.));

                ndpSrc->set_num_shortest_paths(top->get_num_shortest_paths(flow_src, flow_dst));
                ndpSink->set_num_shortest_paths(top->get_num_shortest_paths(flow_dst, flow_src));

                ndpSrc->set_paths(srcpaths);
                ndpSink->set_paths(dstpaths);
                sinkLogger.monitorSink(ndpSink);

            }



        }
    }


    UtilMonitor* UM = new UtilMonitor(top, eventlist);
    UM->start(timeFromSec(utiltime));
    


    // Record the setup
    int pktsize = Packet::data_packet_size();
    logfile.write("# pktsize=" + ntoa(pktsize) + " bytes");
    logfile.write("# hostnicrate = " + ntoa(HOST_NIC) + " pkt/sec");
    logfile.write("# corelinkrate = " + ntoa(HOST_NIC*CORE_TO_HOST) + " pkt/sec");

    // GO!
    while (eventlist.doNextEvent()) {}

    // Final data collection
    cout << "diff_seqno_distribution=";
    for (auto it : Packet::get_seqno_diffs()) {
        cout << it.first << ":" << it.second << ",";
    }
    cout << endl;

}

string ntoa(double n) {
    stringstream s;
    s << n;
    return s.str();
}

string itoa(uint64_t n) {
    stringstream s;
    s << n;
    return s.str();
}
