// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#ifndef PIPE_H
#define PIPE_H

/*
 * A pipe is a dumb device which simply delays all incoming packets
 */

#include <list>
#include <utility>
#include "config.h"
#include "eventlist.h"
#include "network.h"
#include "loggertypes.h"


class Pipe : public EventSource, public PacketSink {
 public:

    Pipe(simtime_picosec delay, EventList& eventlist);
    Pipe(simtime_picosec delay, EventList& eventlist, bool is_da);
    void receivePacket(Packet& pkt) override; // inherited from PacketSink
    void doNextEvent() override; // inherited from EventSource

    virtual void sendFromPipe(Packet *pkt);

    uint64_t reportBytes(); // reports to the UtilMonitor
    uint64_t _bytes_delivered; // keep track of how many (non-hdr,ACK,NACK,PULL,RTX) packets were delivered to hosts

    mem_b reportHeaderBytes();
    mem_b _header_bytes_delivered;

    uint64_t reportBytesRtx(); // reports to the UtilMonitor
    mem_b _bytes_sent_retransmit;

    double reportAvgHopCount(); // Report on the avg. hop cnt value per sent packet as observed in the past collection interval
    uint64_t _no_sent_packets, _sum_hop_count;

    void get_hop_count(Packet *pkt);

    bool has_packets_inflight() { return !_inflight.empty(); }

    simtime_picosec delay() { return _delay; }
    const string& nodename() override { return _nodename; }
    bool is_priority_flow() override { return false; }

    static void set_segregated_routing(bool value) { segregated_routing = value; }

    static bool segregated_routing;

 private:
    simtime_picosec _delay;
    typedef pair<simtime_picosec,Packet*> pktrecord_t;
    list<pktrecord_t> _inflight; // the packets in flight (or being serialized)
    string _nodename;
    bool is_da_pipe;
};

class UtilMonitor : public EventSource {
 public:

    UtilMonitor(DebruijnTopology* top, EventList &eventlist);

    void start(simtime_picosec period);
    void doNextEvent() override;
    void printAggUtil();

    DebruijnTopology* _top;
    simtime_picosec _period; // picoseconds between utilization reports
    uint64_t _max_agg_Bps; // delivered to endhosts, across the whole network
    uint64_t _max_B_in_period;
    int _H; // number of hosts
    int _N; // number of racks
    int _hpr; // number of hosts per rack
    int _uplinks;
};


#endif
