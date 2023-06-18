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
    void receivePacket(Packet& pkt); // inherited from PacketSink
    void doNextEvent(); // inherited from EventSource
    simtime_picosec delay() { return _delay; }
    const string& nodename() { return _nodename; }

    void set_pipe_downlink() { _pipe_is_downlink = true; }
    bool _pipe_is_downlink;
    void set_uplink_pipe_id(int val) { _uplink_pipe_id = val; }
    int _uplink_pipe_id;

    uint64_t reportBytes(); // reports to the UtilMonitor
    uint64_t _bytes_delivered; // keep track of how many (non-hdr,ACK,NACK,PULL,RTX) bytes were delivered to hosts

    mem_b reportHeaderBytes();
    mem_b _header_bytes_delivered;

    uint64_t reportBytesRtx(); // reports to the UtilMonitor
    mem_b _bytes_sent_retransmit;

    double reportAvgHopCount(); // Report on the avg. hop cnt value per sent packet as observed in the past collection interval
    uint64_t _no_sent_packets, _sum_hop_count;

    void get_hop_count(Packet *pkt);

    inline void setlongname(string name) {_longname = name;}
    string _longname;

 private:
    simtime_picosec _delay;
    typedef pair<simtime_picosec,Packet*> pktrecord_t;
    list<pktrecord_t> _inflight; // the packets in flight (or being serialized)
    string _nodename;
};


#endif
