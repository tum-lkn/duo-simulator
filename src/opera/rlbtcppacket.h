// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-
#ifndef RLBTCPPACKET_H
#define RLBTCPPACKET_H

#include <list>
#include "network.h"

class RlbTcpSink;
class RlbTcpSrc;

// Subclass of Packet.
// Incorporate a packet database, to reuse packet objects that are no longer needed.
// Note: you never construct a new RlbPacket directly; 
// rather you use the static method newpkt() which knows to reuse old packets from the database.

#define HEADER 64

class RlbTcpPacket : public Packet {
 public:
    typedef uint64_t seq_t;

    inline static RlbTcpPacket* newpkt(DynExpTopology* top, PacketFlow &flow, int src, int dst,
                                       RlbSink* rlbsink, RlbSrc* rlbsrc, int seqno, seq_t dataseqno, int size) {
	RlbTcpPacket* p = _packetdb.allocPacket();
	p->set_attrs(flow, size, seqno + size - 1, src,
                     dst); // set sequence number to zero, not used in RLB
	p->set_topology(top);
    p->_type = RLBTCP;
    p->_seqno = seqno;
	p->_is_header = false;
	p->_bounced = false;
    p->_rlbsink = rlbsink;

    p->_data_seqno=dataseqno;
    p->_syn = false;
	return p;
    }

    // this is an overloaded function for constructing dummy packets:
    inline static RlbTcpPacket* newpkt(int size) {
        RlbTcpPacket* p = _packetdb.allocPacket();
        p->set_size(size);
        p->_type = RLBTCP;
        return p;
    }

    inline static RlbTcpPacket* newpkt(DynExpTopology* top, PacketFlow &flow, int src, int dst,
                                       RlbSink* rlbsink, RlbSrc* rlbsrc, seq_t seqno, int size) {
		return newpkt(top, flow, src, dst, rlbsink, rlbsrc, seqno,0,size);
	}

    inline static RlbTcpPacket* new_syn_pkt(DynExpTopology* top, PacketFlow &flow, int src, int dst,
                                       RlbSink* rlbsink, RlbSrc* rlbsrc, seq_t seqno, int size) {
		RlbTcpPacket* p = newpkt(top, flow, src, dst, rlbsink, rlbsrc, seqno, 0, size);
		p->_syn = true;
		return p;
	}
  
    virtual inline void  strip_payload() override {
	   Packet::strip_payload(); _size = HEADER;
        cout << "Sripping payload of RLB packet - shouldn't happen!" << endl;
    };
	
    void free() override {_packetdb.freePacket(this);}
    virtual ~RlbTcpPacket(){}
    inline int seqno() const {return _seqno;}
    inline seq_t data_seqno() const {return _data_seqno;}

    inline simtime_picosec ts() const {return _ts;}
    inline void set_ts(simtime_picosec ts) {_ts = ts;}
    inline int32_t no_of_paths() const {return _no_of_paths;}

    inline RlbSink* get_rlbsink() override {return _rlbsink;}
    inline RlbSrc* get_rlbsrc() override {return _rlbsrc;}

 protected:
    RlbSink* _rlbsink;
    RlbSrc* _rlbsrc;
    seq_t _seqno, _data_seqno;
    bool _syn;
    simtime_picosec _ts;
    int32_t _no_of_paths;  // how many paths are in the sender's
			    // list.  A real implementation would not
			    // send this in every packet, but this is
			    // simulation, and this is easiest to
			    // implement
    static PacketDB<RlbTcpPacket> _packetdb;
    
};

class RlbTcpAck : public Packet {
public:
	typedef RlbTcpPacket::seq_t seq_t;

	inline static RlbTcpAck* newpkt(DynExpTopology* top, PacketFlow &flow, seq_t seqno, seq_t ackno, seq_t dackno,
                                 int src, int dst, RlbSrc *rlbsrc) {
	    RlbTcpAck* p = _packetdb.allocPacket();
        p->set_attrs(flow, RLBTCP_ACKSIZE , seqno + RLBTCP_ACKSIZE - 1, src, dst);
        p->set_topology(top);
	    p->_type = RLBTCPACK;
	    p->_seqno = seqno;
	    p->_ackno = ackno;
	    p->_data_ackno = dackno;
        p->_rlbsrc = rlbsrc;

	    return p;
	}

	inline static RlbTcpAck* newpkt(DynExpTopology* top, PacketFlow &flow, seq_t seqno, seq_t ackno, int src, int dst,
                                 RlbSrc *ndpsrc) {
		return newpkt(top, flow, seqno,ackno,0, src, dst, ndpsrc);
	}

	void free() override {_packetdb.freePacket(this);}
	inline seq_t seqno() const {return _seqno;}
	inline seq_t ackno() const {return _ackno;}
	inline seq_t data_ackno() const {return _data_ackno;}
	inline simtime_picosec ts() const {return _ts;}
	inline void set_ts(simtime_picosec ts) {_ts = ts;}

     RlbSrc *get_rlbsrc() override { return  _rlbsrc; };

	virtual ~RlbTcpAck(){}
	const static int RLBTCP_ACKSIZE=40;
protected:
    RlbSrc* _rlbsrc;

	seq_t _seqno;
	seq_t _ackno, _data_ackno;
	simtime_picosec _ts;
	static PacketDB<RlbTcpAck> _packetdb;
};

#endif  // RLBTCPPACKET
