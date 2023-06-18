// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#ifndef TCP_H
#define TCP_H

/*
 * A TCP source and sink
 */

#include <list>
#include "config.h"
#include "network.h"
#include "tcppacket.h"
#include "eventlist.h"
#include "sent_packets.h"

//#define MODEL_RECEIVE_WINDOW 1

#define timeInf 0

//#define PACKET_SCATTER 1
//#define RANDOM_PATH 1

//#define MAX_SENT 10000

class TcpSink;

class MultipathTcpSrc;

class MultipathTcpSink;

class TcpSrc : public PacketSink, public EventSource {
    friend class TcpSink;

public:
    TcpSrc(TcpLogger *logger, TrafficLogger *pktlogger, EventList &eventlist, int flow_src, int flow_dst);

    uint32_t get_id() { return id; }

    virtual void connect(const Route &routeout, const Route &routeback,
                         TcpSink &sink, simtime_picosec startTime);

    void startflow();

    inline void joinMultipathConnection(MultipathTcpSrc *multipathSrc) {
        _mSrc = multipathSrc;
    };

    void doNextEvent() override;

    void receivePacket(Packet &pkt) override;

    void replace_route(const Route *newroute);

    void set_flowsize(uint64_t flow_size_in_bytes) {
        _flow_size = flow_size_in_bytes;
        //cout << "Setting flow size to " << _flow_size << endl;
    }

    uint64_t get_flowsize() { return _flow_size; }

    void set_ssthresh(uint64_t s) { _ssthresh = s; }

    uint32_t effective_window();

    virtual void rtx_timer_hook(simtime_picosec now, simtime_picosec period);

    virtual const string &nodename() override { return _nodename; }

    inline void set_start_time(simtime_picosec startTime) {_start_time = startTime;}
    inline simtime_picosec get_start_time() const {return _start_time;}

    // should really be private, but loggers want to see:
    uint64_t _highest_sent;  //seqno is in bytes
    uint64_t _packets_sent;
    uint64_t _flow_size;
    uint32_t _cwnd;
    uint32_t _maxcwnd;
    uint64_t _last_acked;
    uint32_t _ssthresh;
    uint16_t _dupacks;
#ifdef PACKET_SCATTER
    uint16_t DUPACK_TH;
    uint16_t _crt_path;
#endif

    int32_t _app_limited;

    //round trip time estimate, needed for coupled congestion control
    simtime_picosec _rtt, _rto, _mdev, _base_rtt;
    int _cap;
    simtime_picosec _rtt_avg, _rtt_cum;
    //simtime_picosec when[MAX_SENT];
    int _sawtooth;

    uint16_t _mss;
    uint32_t _unacked; // an estimate of the amount of unacked data WE WANT TO HAVE in the network
    uint32_t _effcwnd; // an estimate of our current transmission rate, expressed as a cwnd
    uint64_t _recoverq;
    bool _in_fast_recovery;

    bool _established;

    uint32_t _drops;

    TcpSink *_sink;
    MultipathTcpSrc *_mSrc;
    simtime_picosec _RFC2988_RTO_timeout;
    bool _rtx_timeout_pending;

    void set_app_limit(int pktps);

    const Route *_route;
    simtime_picosec _last_ping;
#ifdef PACKET_SCATTER
    vector<const Route*>* _paths;

    void set_paths(vector<const Route*>* rt);
#endif

    void send_packets();


#ifdef MODEL_RECEIVE_WINDOW
    SentPackets _sent_packets;
    uint64_t _highest_data_seq;
#endif
    int _subflow_id;

    virtual void inflate_window();

    virtual void deflate_window();

    vector<uint32_t> _dropped_at_hop;
private:
    const Route *_old_route;
    uint64_t _last_packet_with_old_route;

    simtime_picosec _start_time;

    // Housekeeping
    TcpLogger *_logger;
    //TrafficLogger* _pktlogger;

    // Connectivity
    PacketFlow _flow;

    // Mechanism
    // void clear_timer(uint64_t start, uint64_t end);

    void retransmit_packet();
    //simtime_picosec _last_sent_time;

    //void clearWhen(TcpAck::seq_t from, TcpAck::seq_t to);
    //void showWhen (int from, int to);
    string _nodename;

    int _flow_src; // the sender (source) for this flow
    int _flow_dst; // the receiver (sink) for this flow

    bool _finished;
    uint32_t _num_additional_acks, _num_rtos, _num_total_dupacks;


};

class TcpSink : public PacketSink, public DataReceiver, public Logged {
    friend class TcpSrc;

public:
    TcpSink();

    inline void joinMultipathConnection(MultipathTcpSink *multipathSink) {
        _mSink = multipathSink;
    };

    void receivePacket(Packet &pkt) override;

    TcpAck::seq_t _cumulative_ack; // the packet we have cumulatively acked
    uint64_t _packets;
    uint32_t _drops;

    uint64_t cumulative_ack() override { return _cumulative_ack + _received.size() * 1000; }

    uint32_t drops() override { return _src->_drops; }

    uint32_t get_id() override { return id; }

    const string &nodename() override { return _nodename; }

    MultipathTcpSink *_mSink;
    list<TcpAck::seq_t> _received; /* list of packets above a hole, that 
				      we've received */
    list<mem_b> _received_sizes;

#ifdef PACKET_SCATTER
    vector<const Route*>* _paths;

    void set_paths(vector<const Route*>* rt);
#endif

    TcpSrc *_src;

    static mem_b get_global_received_bytes() { return _global_received_bytes;}


private:
    // Connectivity
    uint16_t _crt_path;

    void connect(TcpSrc &src, const Route &route);

    const Route *_route;

    // Mechanism
    void send_ack(simtime_picosec ts, bool marked);

    string _nodename;

    uint64_t total_hops_per_bit, total_received_packets, _pkts_received_retransmits;
    vector<uint64_t> distribution_hops_per_bit, distribution_hops_per_packet;
    mem_b _bytes_received_retransmits;
    mem_b _total_received = 0;

    static mem_b _global_received_bytes;
};

class TcpRtxTimerScanner : public EventSource {
public:
    TcpRtxTimerScanner(simtime_picosec scanPeriod, EventList &eventlist);

    void doNextEvent() override;

    void registerTcp(TcpSrc &tcpsrc);

private:
    simtime_picosec _scanPeriod;
    typedef list<TcpSrc *> tcps_t;
    tcps_t _tcps;
};

#endif
