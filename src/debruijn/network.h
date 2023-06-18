// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#ifndef NETWORK_H
#define NETWORK_H

#include <vector>
#include <iostream>
#include "config.h"
#include "loggertypes.h"
#include "eventlist.h"
//#include "route.h" // not needed anymore

#include "debruijn_topology.h"

class Packet;
class PacketFlow;
class PacketSink;
typedef uint32_t packetid_t;
//void print_route(const Route& route);

class NdpSink;
class NdpSrc;
class RlbSink;
class RlbSrc;
class TcpSource;
class TcpSink;


class DataReceiver {
public:
    DataReceiver() {};

    virtual ~DataReceiver() {};

    virtual uint64_t cumulative_ack() = 0;

    virtual uint32_t get_id() = 0;

    virtual uint32_t drops() = 0;
};

class PacketFlow : public Logged {
    friend class Packet;

public:
    PacketFlow(TrafficLogger *logger);

    virtual ~PacketFlow() {};

    void set_logger(TrafficLogger *logger);

    void logTraffic(Packet &pkt, Logged &location, TrafficLogger::TrafficEvent ev);

    inline uint32_t flow_id() const { return _flow_id; }

    bool log_me() const { return _logger != NULL; }

protected:
    static uint32_t _max_flow_id;
    uint32_t _flow_id;
    TrafficLogger *_logger;
};


typedef enum {
    IP,
    TCP,
    TCPACK,
    TCPNACK,
    NDP,
    NDPACK,
    NDPNACK,
    NDPPULL,
    NDPLITE,
    NDPLITEACK,
    NDPLITEPULL,
    NDPLITERTS,
    ETH_PAUSE,
    RLB
} packet_type;

class VirtualQueue {
public:
    VirtualQueue() {}

    virtual ~VirtualQueue() {}

    virtual void completedService(Packet &pkt) = 0;
};


// See tcppacket.h to illustrate how Packet is typically used.
class Packet {
    friend class PacketFlow;

public:
    /* empty constructor; Packet::set must always be called as
       well. It's a separate method, for convenient reuse */
    Packet() {
        _is_header = false;
        _bounced = false;
        _been_bounced = false;
        _type = IP;
        _flags = 0;
        been_indirected = false;
        _real_crt_hop = -1;
        _hops_on_da = 0;
        _hops_on_static = 0;
    }

    /* say "this packet is no longer wanted". (doesn't necessarily
       destroy it, so it can be reused) */
    virtual void free();

    static void set_packet_size(int packet_size) {
        // Use Packet::set_packet_size() to change the default packet
        // size for TCP or NDP data packets.  You MUST call this
        // before the value has been used to initialize anything else.
        // If someone has already read the value of packet size, no
        // longer allow it to be changed, or all hell will break
        // loose.
        assert(_packet_size_fixed == false);
        _data_packet_size = packet_size;
    }

    static int data_packet_size() {
        _packet_size_fixed = true;
        return _data_packet_size;
    }

    uint16_t size() const { return _size; }

    void set_size(int i) { _size = i; }

    int type() const { return _type; };

    bool header_only() const { return _is_header; }

    bool bounced() const { return _bounced; }

    bool been_bounced() const { return _been_bounced; }

    virtual bool is_vlb() const { return false; }

    PacketFlow &flow() const { return *_flow; }

    virtual ~Packet() {};

    inline const packetid_t id() const { return _id; }

    inline uint32_t flow_id() const { return _flow->flow_id(); }
    // packets don't have routes anymore...
    //const Route* route() const {return _route;}
    //const Route* reverse_route() const {return _route->reverse();}

    virtual void strip_payload() {
        assert(!_is_header);
        _is_header = true;
    };

    virtual void bounce();

    virtual void unbounce(uint16_t pktsize);
    //inline uint32_t path_len() const {return _path_len;}

    inline uint32_t flags() const { return _flags; }

    inline void set_flags(uint32_t f) { _flags = f; }

    //uint32_t nexthop() const {return _nexthop;} // only intended to be used for debugging
    //void set_route(const Route &route);
    string str() const;

    ///////// For label-switched routing: /////////

    void set_topology(DebruijnTopology *top) { _top = top; } // pass in the pointer to the topology object
    DebruijnTopology *get_topology() { return _top; }

    // these are fixed right when the packet goes out the door at the sender queue
    void set_slice_sent(int slice_sent) { _slice_sent = slice_sent; }

    int get_slice_sent() { return _slice_sent; }

    void set_path_index(int path_index) { _path_index = path_index; }

    int get_path_index() { return _path_index; }

    // these are used as the packet traverses the network
    void set_crtToR(int crtToR) { _crtToR = crtToR; }

    int get_crtToR() { return _crtToR; }

    void set_crthop(int crthop) {
        _crthop = crthop;
    }

    int get_crthop() const { return _crthop; }

    int get_real_crthop() const {return _real_crt_hop;}

    void inc_crthop() {
        _crthop++;
        _real_crt_hop++;
        if (_real_crt_hop > 7)
            cout << "MaxHopReached " << _id << " " << _real_src << "-" << _real_dst << " " << _size << " " << _real_crt_hop << endl;
    }

    int get_hops_da() const { return _hops_on_da; }
    void inc_hops_da() { _hops_on_da++; }

    int get_hops_static() const { return _hops_on_static; }
    void inc_hops_static() { _hops_on_static++; }

    bool used_da_and_static() const { return (_hops_on_static > 0) && (_hops_on_da > 0); }

    void set_lasthop(bool val) { _lasthop = val; }

    bool is_lasthop() const { return _lasthop; }

    void set_maxhops(int hops) { _maxhops = hops; }

    int get_maxhops() { return _maxhops; }

    void set_crtport(int port) { _crtport = port; }

    int get_crtport() { return _crtport; }

    int get_src() { return _src; }

    int get_dst() { return _dst; }

    int get_src_ToR() { return _src_ToR; }

    void set_src_ToR(int ToR) { _src_ToR = ToR; }

    int get_dst_ToR() { return _top->get_firstToR(_dst); }

    int get_real_dst_tor() { return _top->get_firstToR(_real_dst); }

    virtual inline PacketSink *get_flow_sink() { return nullptr; }

    virtual inline PacketSink *get_flow_src() { return nullptr; }

    // stuff used for RLB:
    void set_dst(int dst) { _dst = dst; } // the current sending host
    void set_src(int src) { _src = src; } // the current destination host

    void set_real_src(int real_src) { _real_src = real_src; }

    int get_real_src() { return _real_src; }

    void set_real_dst(int real_dst) { _real_dst = real_dst; }

    int get_real_dst() { return _real_dst; }

    bool is_dummy() { return _is_dummy; }

    void set_dummy(bool is_dummy) { _is_dummy = is_dummy; }

    bool _is_dummy; // is this a dummy packet? (used in RLB modulde for rate limiting)

    bool has_been_indirected() const { return been_indirected; }
    void mark_indirected() { been_indirected = true; }

    virtual inline RlbSink *get_rlbsink() { return NULL; }

    virtual inline RlbSrc *get_rlbsrc() { return NULL; }
    // -------------------

    // stuff for debugging:
    void set_time_sent(uint64_t time) { _time_sent = time; }

    uint64_t get_time_sent() { return _time_sent; }

    uint64_t _time_sent;

    static map<int, uint32_t> &get_seqno_diffs() { return _seqno_diffs; }

protected:
    void set_attrs(PacketFlow &flow, int pkt_size, packetid_t id, int src, int dst);

    static int _data_packet_size; // default size of a TCP or NDP data packet,
    // measured in bytes
    static bool _packet_size_fixed; //prevent foot-shooting

    static map<int, uint32_t> _seqno_diffs;

    packet_type _type;

    uint16_t _size;
    bool _is_header;
    bool _bounced; // packet has hit a full queue, and is being bounced back to the sender
    bool _been_bounced; // packet has been bounced previously (for debugging only as of 9/4/18)
    uint32_t _flags; // used for ECN & friends

    ///////// For RLB //////////

    int _real_dst; // _dst is used for routing (it could be the "intermediate" host, if packet is sent on 2 hop path)
    int _real_src; // _real_dst is examined by the RLB module to determine whether to re-enque for a second hop.

    /* FOR DUO */
    bool been_indirected;

    int _hops_on_da, _hops_on_static;

    ///////// For label-switched routing: /////////
    DebruijnTopology *_top;
    // rather than give the packet a route, we just give it access to the topology
    // this is used for label switching
    // it also allows us to drop/misroute packets if they're sent at the wrong time

    int _src_ToR; // we use this for routing since RTS packets can have a src node and "src ToR" that don't match

    int _src; // sending host, used to index labels
    int _dst; // destination host, used to index labels
    int _slice_sent; // the topology slice during which the packet was sent, used to index labels
    int _path_index; // along which of multiple paths the packet was sent, used to index labels
    int _crtToR; // current ToR (used to compute next ToR before packet sent out on pipe)
    int _crthop; // current hop, used to index lables
    int _real_crt_hop;
    bool _lasthop;
    int _maxhops;
    int _crtport;

    packetid_t _id;
    PacketFlow *_flow;
};

// do we even need the PacketSink class anymore?
class PacketSink {
public:
    PacketSink() {}

    virtual ~PacketSink() {}

    virtual void receivePacket(Packet &pkt) = 0;

    virtual void receivePacket(Packet &pkt, VirtualQueue *previousHop) {
        receivePacket(pkt);
    };

    virtual const string &nodename() = 0;

    virtual bool is_priority_flow() = 0;
};

// For speed, it may be useful to keep a database of all packets that
// have been allocated -- that way we don't need a malloc for every
// new packet, we can just reuse old packets. Care, though -- the set()
// method will need to be invoked properly for each new/reused packet

template<class P>
class PacketDB {
public:
    P *allocPacket() {
        if (_freelist.empty()) {
            return new P();
        } else {
            P *p = _freelist.back();
            _freelist.pop_back();
            return p;
        }
    };

    void freePacket(P *pkt) {
        _freelist.push_back(pkt);
    };

protected:
    vector<P *> _freelist; // Irek says it's faster with vector than with list
};


#endif
