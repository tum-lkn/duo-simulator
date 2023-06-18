// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        


#ifndef RLBTCP_H
#define RLBTCP_H

/*
 * An RLB source and sink
 */

#include <list>
#include <map>
#include "config.h"
#include "network.h"
#include "rlbtcppacket.h"
#include "rlb.h"
#include "fairpullqueue.h"
#include "eventlist.h"

#define timeInf 0

class RlbTcpSink;

class Queue;

//class RlbSrc : public PacketSink, public EventSource {
class RlbTcpSrc : public RlbSrc {
    friend class RlbTcpSink;
 public:
    RlbTcpSrc(DynExpTopology* top, NdpLogger* logger, TrafficLogger* pktlogger, EventList &eventlist, int flow_src, int flow_dst);
    //uint32_t get_id(){ return id;}
    virtual void connect(RlbTcpSink& sink, simtime_picosec startTime);
    void startflow();

    void set_flowsize(uint64_t flow_size_in_bytes) {_flow_size = flow_size_in_bytes;}
    uint64_t get_flowsize() {return _flow_size;} // bytes
    inline void set_start_time(simtime_picosec startTime) {_start_time = startTime;}
    inline simtime_picosec get_start_time() {return _start_time;}

    inline int get_flow_src() {return _flow_src;}
    inline int get_flow_dst() {return _flow_dst;}

    void doNextEvent() override;

    void receivePacket(Packet &pkt) override;

    // TCP functions
    void set_ssthresh(uint64_t s) { _ssthresh = s; }

    uint32_t effective_window();

    virtual void rtx_timer_hook(simtime_picosec now, simtime_picosec period);

    virtual void inflate_window();

    virtual void deflate_window();

    // Mechanism
    void clear_timer(uint64_t start, uint64_t end);

    void retransmit_packet();

    void send_packets();

    // sending function single packet
    void sendToRlbModule(Packet *pkt);

    uint64_t _sent; // keep track of how many packets we've sent
    uint16_t _mss; // maximum segment size

    RlbTcpSink* _sink;

    int _pkts_sent; // number of packets sent


    // TCP attributes
    // should really be private, but loggers want to see:
    uint64_t _highest_sent;  //seqno is in bytes
    uint64_t _packets_sent;
    uint32_t _cwnd;
    uint32_t _maxcwnd;
    uint64_t _last_acked;
    uint32_t _ssthresh;
    uint16_t _dupacks;

    int32_t _app_limited;
     //round trip time estimate, needed for coupled congestion control
    simtime_picosec _rtt, _rto, _mdev, _base_rtt;
    int _cap;
    simtime_picosec _rtt_avg, _rtt_cum;
    //simtime_picosec when[MAX_SENT];
    int _sawtooth;

    uint32_t _unacked; // an estimate of the amount of unacked data WE WANT TO HAVE in the network
    uint32_t _effcwnd; // an estimate of our current transmission rate, expressed as a cwnd
    uint64_t _recoverq;
    bool _in_fast_recovery;

    bool _established;
    uint32_t _drops;

    simtime_picosec _RFC2988_RTO_timeout;
    bool _rtx_timeout_pending;

    simtime_picosec _last_ping;

 private:
    // Connectivity
    PacketFlow _flow;
    string _nodename;

    simtime_picosec _start_time;
    uint64_t _flow_size;  //The flow size in bytes.  Stop sending after this amount.
};


class RlbTcpSink : public RlbSink {
    friend class RlbTcpSrc;
 public:
    RlbTcpSink(DynExpTopology* top, EventList &eventlist, int flow_src, int flow_dst);
 

    //uint32_t get_id(){ return id;} // this is for logging...
    void receivePacket(Packet& pkt) override;
    
    uint64_t total_received() const { return _total_received;}

    virtual void doNextEvent() override; // don't actually use this, but need it to get access to eventlist
    
    void sendToRlbModule(Packet *pkt);

    RlbTcpAck::seq_t _cumulative_ack; // the packet we have cumulatively acked
    uint64_t _packets;
    uint32_t _drops;

    uint64_t cumulative_ack() { return _cumulative_ack + _received.size() * 1000; }

    uint32_t drops() { return _src->_drops; }
 
    RlbTcpSrc* _src;

    list<RlbTcpAck::seq_t> _received; /* list of packets above a hole, that
				      we've received */

    int _pkts_received; // number of packets received

    vector<uint64_t> distribution_hops_per_bit, distribution_hops_per_packet;

    uint64_t total_hops_per_bit, total_received_packets, _pkts_received_retransmits;
    mem_b _bytes_received_retransmits;
    uint64_t _total_received;
 private:
    void connect(RlbTcpSrc& src);

    // Mechanism
    void send_ack(simtime_picosec ts, bool marked);

    string _nodename;


};

class RlbTcpRtxTimerScanner : public EventSource {
public:
    RlbTcpRtxTimerScanner(simtime_picosec scanPeriod, EventList &eventlist);

    void doNextEvent() override;

    void registerTcp(RlbTcpSrc &tcpsrc);

private:
    simtime_picosec _scanPeriod;
    typedef list<RlbTcpSrc *> tcps_t;
    tcps_t _tcps;
};

#endif  // RLBTCP

