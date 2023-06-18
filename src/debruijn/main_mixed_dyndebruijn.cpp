// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#include "config.h"
#include <iostream>
#include <string.h>
#include <math.h>
#include "network.h"
#include "pipe.h"
#include "eventlist.h"
#include "logfile.h"
#include "loggers.h"
#include "clock.h"
#include "ndp.h"
#include "tcp.h"
#include "compositequeue.h"
#include <list>
#include "main.h"

// Choose the topology here:
#include "debruijn_topology.h"
#include "rlb.h"
#include "rlbmodule.h"

// Simulation params

#define PRINT_PATHS 0

uint32_t delay_host2ToR = 0; // host-to-tor link delay in nanoseconds
uint32_t delay_ToR2ToR = 500; // tor-to-tor link delay in nanoseconds

#define DEFAULT_PACKET_SIZE 1500 // MAXIMUM FULL packet size (includes header + payload), Bytes
#define DEFAULT_HEADER_SIZE 64 // header size, Bytes
    // note: there is another parameter defined in `ndppacket.h`: "ACKSIZE". This should be set to the same size.
// set the NDP queue size in units of packets (of length DEFAULT_PACKET_SIZE Bytes)
#define DEFAULT_QUEUE_SIZE 8

string ntoa(double n); // convert a double to a string
string itoa(uint64_t n); // convert an int to a string

EventList eventlist;

Logfile* lg;

void exit_error(char* progr){
    cout << "Usage " << progr << " [UNCOUPLED(DEFAULT)|COUPLED_INC|FULLY_COUPLED|COUPLED_EPSILON] [epsilon][COUPLED_SCALABLE_TCP" << endl;
    exit(1);
}

int main(int argc, char **argv) {

	// set maximum packet size in bytes:
    Packet::set_packet_size(DEFAULT_PACKET_SIZE - DEFAULT_HEADER_SIZE);
    mem_b queuesize = DEFAULT_QUEUE_SIZE * DEFAULT_PACKET_SIZE;

    // defined in flags:
    stringstream filename(ios_base::out);
    string flowfile; // read the flows from a specified file
    string topfile; // read the topology from a specified file
    double simtime; // seconds
    double utiltime = .01; // seconds
    bool vlb = false;
    bool segregated = false;

    // DeBruijn Algorithm parameters
    uint32_t day_us = 9900;
    uint32_t night_us = 100;
    uint32_t circuit_reservation_duration = 0;  // 10000
    mem_b heavy_hitter_size_byte = 0;

    double rto_ms = 10;
    int cwnd = 30;

    mem_b high_prio_flowsize = 0;

    int i = 1;
    filename << "logout.dat";
    DaLinkAlgorithm da_algorithm = ONE_HOP;

    // parse the command line flags:
    while (i<argc) {
        if (!strcmp(argv[i],"-o")){
	       filename.str(std::string());
	       filename << argv[i+1];
	       i++;
        } else if (!strcmp(argv[i],"-q")){
	       queuesize = atoi(argv[i+1]) * DEFAULT_PACKET_SIZE;
	       i++;
        } else if (!strcmp(argv[i],"-cutoff")) {
            heavy_hitter_size_byte = atol(argv[i+1]);
            i++;
        } else if (!strcmp(argv[i],"-flowfile")) {
			flowfile = argv[i+1];
			i++;
        } else if (!strcmp(argv[i],"-topfile")) {
            topfile = argv[i+1];
            i++;
        } else if (!strcmp(argv[i],"-simtime")) {
            simtime = atof(argv[i+1]);
            i++;
        } else if (!strcmp(argv[i],"-utiltime")) {
            utiltime = atof(argv[i+1]);
            i++;
        } else if (!strcmp(argv[i],"-day")) {
            day_us = atof(argv[i+1]);
            i++;
        } else if (!strcmp(argv[i],"-night")) {
            night_us = atof(argv[i+1]);
            i++;
        } else if (!strcmp(argv[i],"-rsv")) {
            circuit_reservation_duration = atof(argv[i+1]);
            i++;
        } else if (!strcmp(argv[i],"-rto")) {
            rto_ms = atof(argv[i+1]);
            i++;
        } else if (!strcmp(argv[i],"-cwnd")){
            cwnd = atoi(argv[i+1]);
            i++;
        } else  if (!strcmp(argv[i],"-hpflowsize")) {
            high_prio_flowsize = atof(argv[i+1]);
            i++;
        } else if (!strcmp(argv[i],"-vlb")) {
            vlb = true;
        } else if (!strcmp(argv[i],"-segr")) {
            segregated = true;
        } else if (!strcmp(argv[i],"-algo")){
            if (!strcmp(argv[i+1], "1h")) {
                da_algorithm = ONE_HOP;
            }
            i++;
        } else {
            exit_error(argv[0]);
        }
        
        i++;
    }

    srand(13); // random seed

    eventlist.setEndtime(timeFromSec(simtime)); // in seconds
    Clock c(timeFromSec(5 / 100.), eventlist);

    //cout << "cwnd " << cwnd << endl;
    //cout << "Logging to " << filename.str() << endl;
    Logfile logfile(filename.str(), eventlist);

    lg = &logfile;

    // !!!!!!!!!!! make sure to set StartTime to the correct value !!!!!!!!!!!
    // *set this to be longer than the sim time if you don't want to record anything to the logger
    logfile.setStartTime(timeFromSec(10)); // record events starting at this simulator time

    TcpSinkLoggerSampling sinkLogger = TcpSinkLoggerSampling(timeFromMs(10), eventlist);
    logfile.addLogger(sinkLogger);
    TcpTrafficLogger traffic_logger = TcpTrafficLogger();
    // logfile.addLogger(traffic_logger);

    // NdpSinkLoggerSampling object will iterate through all NdpSinks and log their rate every
    // X microseconds. This is used to get throughput measurements after the experiment ends.
    TcpRtxTimerScanner tcpRtxScanner(timeFromMs(1), eventlist);

    NdpSinkLoggerSampling ndpSinkLogger = NdpSinkLoggerSampling(timeFromUs(50.), eventlist);
    logfile.addLogger(ndpSinkLogger);
    NdpTrafficLogger ndp_traffic_logger = NdpTrafficLogger();
    logfile.addLogger(ndp_traffic_logger);
    NdpRtxTimerScanner ndpRtxScanner(timeFromMs(1), eventlist);

    auto top = new DebruijnTopology(queuesize, &logfile, &eventlist, ENHANCED_COMPOSITE, topfile);

    CompositeQueue::set_high_prio_flowsize(high_prio_flowsize);
    PriorityQueue::set_high_prio_flowsize(high_prio_flowsize);

    RouteStrategy route_strategy = SCATTER_RANDOM; // only one routing strategy for now...

    NdpSrc::setMinRTO(1000); // microseconds
    NdpSrc::setRouteStrategy(route_strategy);
    NdpSink::setRouteStrategy(route_strategy);
    Pipe::set_segregated_routing(segregated);

    TcpSrc *tcpSrc;
    TcpSink *tcpSnk;

    NdpSrc *ndpSrc;
    NdpSink *ndpSink;

    // Prepare a pacer for each host
    vector<NdpPullPacer*> pacers;
    for (int j=0; j<top->no_of_nodes(); j++)
        // Set the pull rate to something reasonable.
        // we can be more aggressive if the load is lower
        pacers.push_back(new NdpPullPacer(eventlist, 1));

    // debug:
    cout << "Loading traffic..." << endl;

    //ifstream input("flows.txt");
    ifstream input(flowfile);
    if (input.is_open()){
        string line;
        int64_t temp;
        // get flows. Format: (src) (dst) (bytes) (starttime nanoseconds)
        while(!input.eof()){
            vector<int64_t> vtemp;
            getline(input, line);
            stringstream stream(line);
            while (stream >> temp)
                vtemp.push_back(temp);
            //cout << "src = " << vtemp[0] << ", dest = " << vtemp[1] << ", bytes =  " << vtemp[2] << ", start_time[us] " << vtemp[3] << endl;

            // source and destination hosts for this flow
            int flow_src = vtemp[0];
            int flow_dst = vtemp[1];

            if (vtemp[2] >= high_prio_flowsize) {
                tcpSrc = new TcpSrc(top, NULL, nullptr, eventlist, flow_src, flow_dst);
                tcpSnk = new TcpSink(top, flow_src, flow_dst);

                tcpSrc->set_ssthresh(10 * Packet::data_packet_size());
                tcpSrc->set_flowsize(vtemp[2]);

                tcpSrc->_rto = timeFromMs(rto_ms);

                tcpRtxScanner.registerTcp(*tcpSrc);
                tcpSrc->connect(*tcpSnk, timeFromNs(vtemp[3] / 1.));
            } else {
                ndpSrc = new NdpSrc(top, NULL, NULL, eventlist, flow_src, flow_dst, vlb, true);
                ndpSrc->setCwnd(cwnd * Packet::data_packet_size()); // congestion window
                ndpSrc->set_flowsize(vtemp[2]); // bytes

                // Use the same pacer per destination host
                ndpSink = new NdpSink(top, pacers[flow_dst], flow_src, flow_dst);
                ndpRtxScanner.registerNdp(*ndpSrc);

                // set up the connection event
                ndpSrc->connect(*ndpSink, timeFromNs(vtemp[3] / 1.));
            }
        }
    }

    cout << "Traffic loaded." << endl;

    auto master = new DeBruijnOCSMaster(top, eventlist, day_us, night_us, da_algorithm,
                                                      circuit_reservation_duration, heavy_hitter_size_byte); // synchronizes the RLBmodules
    master->start();

    // NOTE: UtilMonitor defined in "pipe"
    auto UM = new UtilMonitor(top, eventlist);
    UM->start(timeFromSec(utiltime)); // print utilization every X milliseconds.

    // debug:
    //cout << "Starting... " << endl;

    // GO!
    while (eventlist.doNextEvent()) {
    }
    //cout << "Done" << endl;

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
