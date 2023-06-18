#ifndef TCPPACKET_H
#define TCPPACKET_H

#include <list>
#include "network.h"

class TcpSrc;

// TcpPacket and TcpAck are subclasses of Packet.
// They incorporate a packet database, to reuse packet objects that are no longer needed.
// Note: you never construct a new TcpPacket or TcpAck directly; 
// rather you use the static method newpkt() which knows to reuse old packets from the database.

#define HEADER 64

class TcpPacket : public Packet {
public:
	typedef uint64_t seq_t;

	inline static TcpPacket* newpkt(PacketFlow &flow, const Route &route, 
					seq_t seqno, seq_t dataseqno,int size, TcpSrc *ndpsrc) {
	    TcpPacket* p = _packetdb.allocPacket();
	    p->set_route(flow,route,size+HEADER,seqno+size-1); // The TCP sequence number is the first byte of the packet; I will ID the packet by its last byte.
	    p->_type = TCP;
	    p->_seqno = seqno;
	    p->_data_seqno=dataseqno;
	    p->_syn = false;
        p->_tcpsrc = ndpsrc;
        p->current_hop = 0;
	    return p;
	}

	inline static TcpPacket* newpkt(PacketFlow &flow, const Route &route, 
					seq_t seqno, int size, TcpSrc *ndpsrc) {
		return newpkt(flow,route,seqno,0,size, ndpsrc);
	}

	inline static TcpPacket* new_syn_pkt(PacketFlow &flow, const Route &route, 
					seq_t seqno, int size, TcpSrc *ndpsrc) {
		TcpPacket* p = newpkt(flow,route,seqno,0,size, ndpsrc);
		p->_syn = true;
		return p;
	}

	void free() {_packetdb.freePacket(this);}
	virtual ~TcpPacket(){}
	inline seq_t seqno() const {return _seqno;}
	inline seq_t data_seqno() const {return _data_seqno;}
	inline simtime_picosec ts() const {return _ts;}
	inline void set_ts(simtime_picosec ts) {_ts = ts;}

    TcpSrc *get_tcp_src() { return _tcpsrc; }

    void track_drop();
protected:
	seq_t _seqno,_data_seqno;
	bool _syn;
	simtime_picosec _ts;
	static PacketDB<TcpPacket> _packetdb;
    TcpSrc *_tcpsrc;
};

class TcpAck : public Packet {
public:
	typedef TcpPacket::seq_t seq_t;

	inline static TcpAck* newpkt(PacketFlow &flow, const Route &route, 
				     seq_t seqno, seq_t ackno,seq_t dackno, TcpSrc *tcpsrc) {
	    TcpAck* p = _packetdb.allocPacket();
	    p->set_route(flow,route,HEADER,ackno);
	    p->_type = TCPACK;
	    p->_seqno = seqno;
	    p->_ackno = ackno;
	    p->_data_ackno = dackno;

        p->_tcpsrc = tcpsrc;

	    return p;
	}

	inline static TcpAck* newpkt(PacketFlow &flow, const Route &route, 
					seq_t seqno, seq_t ackno, TcpSrc *tcpsrc) {
		return newpkt(flow,route,seqno,ackno,0, tcpsrc);
	}

	void free() {_packetdb.freePacket(this);}
	inline seq_t seqno() const {return _seqno;}
	inline seq_t ackno() const {return _ackno;}
	inline seq_t data_ackno() const {return _data_ackno;}
	inline simtime_picosec ts() const {return _ts;}
	inline void set_ts(simtime_picosec ts) {_ts = ts;}

    TcpSrc *get_tcp_src() { return _tcpsrc; }

	virtual ~TcpAck(){}

protected:
    TcpSrc *_tcpsrc;

	seq_t _seqno;
	seq_t _ackno, _data_ackno;
	simtime_picosec _ts;
	static PacketDB<TcpAck> _packetdb;
};

#endif
