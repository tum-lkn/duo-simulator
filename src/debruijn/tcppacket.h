#ifndef TCPPACKET_H
#define TCPPACKET_H

#include <list>
#include "network.h"


// TcpPacket and TcpAck are subclasses of Packet.
// They incorporate a packet database, to reuse packet objects that are no longer needed.
// Note: you never construct a new TcpPacket or TcpAck directly; 
// rather you use the static method newpkt() which knows to reuse old packets from the database.

#define HEADER 64

class TcpPacket : public Packet {
public:
	typedef uint64_t seq_t;

	inline static TcpPacket* newpkt(DebruijnTopology *top, PacketFlow &flow, seq_t seqno, seq_t dataseqno,int size, int src, int dst,
                                    TcpSrc *ndpsrc, TcpSink *ndpsink) {
	    TcpPacket* p = _packetdb.allocPacket();
        p->set_attrs(flow, size + HEADER , seqno + size - 1, src,
                     dst); // The TCP sequence number is the first byte of the packet; I will ID the packet by its last byte.
        p->set_topology(top);
        // Set real SRC and Dst
        p->set_real_src(src);
        p->set_real_dst(dst);

        p->_tcpsrc = ndpsrc;
        p->_tcpsink = ndpsink;

	    p->_type = TCP;
	    p->_seqno = seqno;
	    p->_data_seqno=dataseqno;
	    p->_syn = false;
	    return p;
	}

	inline static TcpPacket* newpkt(DebruijnTopology *top, PacketFlow &flow, seq_t seqno, int size, int src, int dst,
                                    TcpSrc *ndpsrc, TcpSink *ndpsink) {
		return newpkt(top, flow,seqno,0,size, src, dst, ndpsrc, ndpsink);
	}

	inline static TcpPacket* new_syn_pkt(DebruijnTopology *top, PacketFlow &flow, seq_t seqno, int size, int src, int dst,
                                         TcpSrc *ndpsrc, TcpSink *ndpsink) {
		TcpPacket* p = newpkt(top, flow,seqno,0,size, src, dst, ndpsrc, ndpsink);
		p->_syn = true;
		return p;
	}

	void free() override {_packetdb.freePacket(this);}
	~TcpPacket() override = default;
	inline seq_t seqno() const {return _seqno;}
	inline seq_t data_seqno() const {return _data_seqno;}
	inline simtime_picosec ts() const {return _ts;}
	inline void set_ts(simtime_picosec ts) {_ts = ts;}

    PacketSink *get_flow_src() override;
    PacketSink *get_flow_sink() override;

    TcpSrc *get_tcp_src() { return _tcpsrc; }

    void track_drop();
protected:
	seq_t _seqno, _data_seqno;
	bool _syn;
	simtime_picosec _ts;
	static PacketDB<TcpPacket> _packetdb;

    TcpSink *_tcpsink;
    TcpSrc *_tcpsrc;
};

class TcpAck : public Packet {
public:
	typedef TcpPacket::seq_t seq_t;

	inline static TcpAck* newpkt(DebruijnTopology* top, PacketFlow &flow, seq_t seqno, seq_t ackno, seq_t dackno,
                                 int src, int dst, TcpSrc *ndpsrc) {
	    TcpAck* p = _packetdb.allocPacket();
        p->set_attrs(flow, HEADER , seqno + HEADER - 1, src,
                     dst); // The TCP sequence number is the first byte of the packet; I will ID the packet by its last byte.
        p->set_topology(top);

        p->_tcpsrc = ndpsrc;

	    p->_type = TCPACK;
	    p->_seqno = seqno;
	    p->_ackno = ackno;
	    p->_data_ackno = dackno;

	    return p;
	}

	inline static TcpAck* newpkt(DebruijnTopology* top, PacketFlow &flow, seq_t seqno, seq_t ackno, int src, int dst,
                                 TcpSrc *ndpsrc) {
		return newpkt(top, flow,seqno,ackno,0, src, dst, ndpsrc);
	}

	void free() override {_packetdb.freePacket(this);}
	inline seq_t seqno() const {return _seqno;}
	inline seq_t ackno() const {return _ackno;}
	inline seq_t data_ackno() const {return _data_ackno;}
	inline simtime_picosec ts() const {return _ts;}
	inline void set_ts(simtime_picosec ts) {_ts = ts;}

    PacketSink *get_flow_src() override;

    TcpSrc *get_tcp_src() { return _tcpsrc; }

	~TcpAck() override =default;
protected:
    TcpSrc *_tcpsrc;

	seq_t _seqno;
	seq_t _ackno, _data_ackno;
	simtime_picosec _ts;
	static PacketDB<TcpAck> _packetdb;
};

#endif
