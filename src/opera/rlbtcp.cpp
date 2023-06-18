// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-    
#include <math.h>
#include <iostream>
#include "rlbtcp.h"
#include "queue.h"
#include <stdio.h>
#include "ecn.h"

#include "rlbmodule.h"

////////////////////////////////////////////////////////////////
//  RLB SOURCE
////////////////////////////////////////////////////////////////

RlbTcpSrc::RlbTcpSrc(DynExpTopology* top, NdpLogger* logger, TrafficLogger* pktlogger, EventList &eventlist, int flow_src, int flow_dst)
    : RlbSrc(top, logger, pktlogger, eventlist, flow_src, flow_dst), _flow(pktlogger) {
    
    _mss = Packet::data_packet_size(); // maximum segment size (mss)
    _sink = nullptr;
    _flow_size = ((uint64_t)1)<<63; // this should always get set after `new RlbTcpSrc()` gets called
    _pkts_sent = 0;

    //_node_num = _global_node_count++;
    //_nodename = "rlbsrc" + to_string(_node_num);

    // TCP attributes
    _maxcwnd = 0xffffffff;//MAX_SENT*_mss;
    _sawtooth = 0;
    _rtt_avg = timeFromMs(0);
    _rtt_cum = timeFromMs(0);
    _base_rtt = timeInf;
    _cap = 0;
    _highest_sent = 0;
    _packets_sent = 0;
    _app_limited = -1;
    _established = false;
    _effcwnd = 0;

    _ssthresh = 0xffffffff;

    _last_acked = 0;
    _last_ping = timeInf;
    _dupacks = 0;
    _rtt = 0;
    _rto = timeFromMs(3000);
    _mdev = 0;
    _recoverq = 0;
    _in_fast_recovery = false;
    _drops = 0;

    _rtx_timeout_pending = false;
    _RFC2988_RTO_timeout = timeInf;

    _nodename = "tcpsrc";
}

void RlbTcpSrc::connect(RlbTcpSink& sink, simtime_picosec starttime) {
    
    _sink = &sink;
    _flow.id = id; // identify the packet flow with the source that generated it
    _flow._name = _name;
    _sink->connect(*this);

    set_start_time(starttime); // record the start time in _start_time
    eventlist().sourceIsPending(*this,starttime);
}

void RlbTcpSrc::startflow() {
    _cwnd = 10 * _mss;
    _unacked = _cwnd;
    _established = false;

    _sent = 0;


    send_packets();
    /* while (_sent < _flow_size) {
        sendToRlbModule();
    }*/
}

void RlbTcpSrc::sendToRlbModule(Packet *pkt) {
    // RlbPacket* p = RlbPacket::newpkt(_top, _flow, _flow_src, _flow_dst, _sink, _mss, _pkts_sent);
    // ^^^ this sets the current source and destination (used for routing)
    // RLB module uses the "real source" and "real destination" to make decisions
    pkt->set_dummy(false);
    pkt->set_real_dst(_flow_dst); // set the "real" destination
    pkt->set_real_src(_flow_src); // set the "real" source
    // pkt->set_ts(eventlist().now()); // time sent, not really needed...

    RlbModule* module = _top->get_rlb_module(_flow_src); // returns pointer to Rlb module
    module->receivePacket(*pkt, 0);

    // debug:
    //cout << "RlbTcpSrc[" << _flow_src << "] has sent " << _pkts_sent << " packets" << endl;
    //cout << "Sent " << _sent << " bytes out of " << _flow_size << " bytes." << endl;
}

void RlbTcpSrc::receivePacket(Packet &pkt) {
    simtime_picosec ts;
    auto *p = (RlbTcpAck *) (&pkt);
    RlbTcpAck::seq_t seqno = p->ackno();

    pkt.flow().logTraffic(pkt, *this, TrafficLogger::PKT_RCVDESTROY);

    ts = p->ts();
    p->free();

    if (seqno < _last_acked) {
        //cout << "O seqno" << seqno << " last acked "<< _last_acked;
        return;
    }

    if (seqno == 1) {
        //assert(!_established);
        _established = true;
    } else if (seqno > 1 && !_established) {
        cout << "Should be _established " << seqno << endl;
    }

    //assert(seqno >= _last_acked);  // no dups or reordering allowed in this simple simulator

    //compute rtt
    uint64_t m = eventlist().now() - ts;

    if (m != 0) {
        if (_rtt > 0) {
            uint64_t abs;
            if (m > _rtt)
                abs = m - _rtt;
            else
                abs = _rtt - m;

            _mdev = 3 * _mdev / 4 + abs / 4;
            _rtt = 7 * _rtt / 8 + m / 8;
            _rto = _rtt + 4 * _mdev;
        } else {
            _rtt = m;
            _mdev = m / 2;
            _rto = _rtt + 4 * _mdev;
        }
        if (_base_rtt == timeInf || _base_rtt > m)
            _base_rtt = m;
    }
    //  cout << "Base "<<timeAsMs(_base_rtt)<< " RTT " << timeAsMs(_rtt)<< " Queued " << queued_packets << endl;

    if (_rto < timeFromMs(1))
        _rto = timeFromMs(1);

    if (seqno >= _flow_size) {
        cout << "FCT " << _flow_src << " " << _flow_dst << " " << _flow_size <<
             " " << timeAsMs(eventlist().now() - get_start_time()) << " " << fixed << timeAsMs(get_start_time())
             << " " << _sink->total_hops_per_bit << " " << _sink->total_received_packets
             << " " << _sink->_total_received << " ";
        for (uint i = 0; i < 10; i++) {
            cout  << _sink->distribution_hops_per_bit[i] << ",";
        }
        cout  << " ";
        for (uint i = 0; i < 10; i++) {
            cout << _sink->distribution_hops_per_packet[i] << ",";
        }

        cout << " nacks=" << -1;
        cout << " rtx_after_nack=" << -1;
        cout << " rtx_received_pkts=" << -1;
        cout << " rtx_received_bytes=" << -1;

        cout  << " stripped=";
        for (uint i = 0; i < 10; i++) {
            cout << -1 << ",";
        }
        cout << endl;
    }

    if (seqno > _last_acked) { // a brand new ack
        _RFC2988_RTO_timeout = eventlist().now() + _rto;// RFC 2988 5.3
        _last_ping = eventlist().now();

        if (seqno >= _highest_sent) {
            _highest_sent = seqno;
            _RFC2988_RTO_timeout = timeInf;// RFC 2988 5.2
            _last_ping = timeInf;
        }


        if (!_in_fast_recovery) { // best behaviour: proper ack of a new packet, when we were expecting it
            //clear timers
            _last_acked = seqno;
            _dupacks = 0;
            inflate_window();

            if (_cwnd > _maxcwnd) {
                _cwnd = _maxcwnd;
            }

            _unacked = _cwnd;
            _effcwnd = _cwnd;
            send_packets();
            return;
        }
        // We're in fast recovery, i.e. one packet has been
        // dropped but we're pretending it's not serious
        if (seqno >= _recoverq) {
            // got ACKs for all the "recovery window": resume
            // normal service
            uint32_t flightsize = _highest_sent - seqno;
            _cwnd = min(_ssthresh, flightsize + _mss);
            _unacked = _cwnd;
            _effcwnd = _cwnd;
            _last_acked = seqno;
            _dupacks = 0;
            _in_fast_recovery = false;
            send_packets();
            return;
        }
        // In fast recovery, and still getting ACKs for the
        // "recovery window"
        // This is dangerous. It means that several packets
        // got lost, not just the one that triggered FR.
        uint32_t new_data = seqno - _last_acked;
        _last_acked = seqno;
        if (new_data < _cwnd)
            _cwnd -= new_data;
        else
            _cwnd = 0;
        _cwnd += _mss;
        retransmit_packet();
        send_packets();
        return;
    }
    // It's a dup ack
    if (_in_fast_recovery) { // still in fast recovery; hopefully the prodigal ACK is on it's way
        _cwnd += _mss;
        if (_cwnd > _maxcwnd) {
            _cwnd = _maxcwnd;
        }
        // When we restart, the window will be set to
        // min(_ssthresh, flightsize+_mss), so keep track of
        // this
        _unacked = min(_ssthresh, (uint32_t) (_highest_sent - _recoverq + _mss));
        if (_last_acked + _cwnd >= _highest_sent + _mss)
            _effcwnd = _unacked; // starting to send packets again
        send_packets();
        return;
    }
    // Not yet in fast recovery. What should we do instead?
    _dupacks++;

    if (_dupacks != 3) { // not yet serious worry
        send_packets();
        return;
    }
    // _dupacks==3
    if (_last_acked < _recoverq) {
        /* See RFC 3782: if we haven't recovered from timeouts
	   etc. don't do fast recovery */
        return;
    }

    // begin fast recovery

    //only count drops in CA state
    _drops++;

    deflate_window();

    if (_sawtooth > 0)
        _rtt_avg = _rtt_cum / _sawtooth;
    else
        _rtt_avg = timeFromMs(0);

    _sawtooth = 0;
    _rtt_cum = timeFromMs(0);

    retransmit_packet();
    _cwnd = _ssthresh + 3 * _mss;
    _unacked = _ssthresh;
    _effcwnd = 0;
    _in_fast_recovery = true;
    _recoverq = _highest_sent; // _recoverq is the value of the
    // first ACK that tells us things
    // are back on track
}

void RlbTcpSrc::deflate_window() {
    _ssthresh = max(_cwnd / 2, (uint32_t) (2 * _mss));
}

void RlbTcpSrc::inflate_window() {
    int newly_acked = (_last_acked + _cwnd) - _highest_sent;
    // be very conservative - possibly not the best we can do, but
    // the alternative has bad side effects.
    if (newly_acked > _mss) newly_acked = _mss;
    if (newly_acked < 0)
        return;
    if (_cwnd < _ssthresh) { //slow start
        int increase = min(_ssthresh - _cwnd, (uint32_t) newly_acked);
        _cwnd += increase;
        newly_acked -= increase;
    } else {
        // additive increase
        uint32_t pkts = _cwnd / _mss;

        _cwnd += (newly_acked * _mss) / _cwnd;  //XXX beware large windows, when this increase gets to be very small

        if (pkts != _cwnd / _mss) {
            _rtt_cum += _rtt;
            _sawtooth++;
        }
    }
}

uint32_t RlbTcpSrc::effective_window() {
    return _in_fast_recovery ? _ssthresh : _cwnd;
}


// Note: the data sequence number is the number of Byte1 of the packet, not the last byte.
void RlbTcpSrc::send_packets() {
    int c = _cwnd;

    if (!_established) {
        //send SYN packet and wait for SYN/ACK
        Packet *p = RlbTcpPacket::new_syn_pkt(_top, _flow,_flow_src, _flow_dst, _sink, this,  1, 1);
        _highest_sent = 1;

        sendToRlbModule(p);

        if (_RFC2988_RTO_timeout == timeInf) {// RFC2988 5.1
            _RFC2988_RTO_timeout = eventlist().now() + _rto;
        }
        //cout << "Sending SYN, waiting for SYN/ACK" << endl;
        return;
    }

    if (_app_limited >= 0 && _rtt > 0) {
        uint64_t d = (uint64_t) _app_limited * _rtt / 1000000000;
        if (c > d) {
            c = d;
        }

        if (c == 0) {
            //      _RFC2988_RTO_timeout = timeInf;
        }
    }

    while ((_last_acked + c >= _highest_sent + _mss) && (_highest_sent < _flow_size+1)) {
        uint64_t data_seq = 0;

        uint16_t size_to_send = _mss;
        bool last_packet = false;
        if (_highest_sent + _mss > _flow_size+1) {
            last_packet = true;
            size_to_send = _flow_size + 1 - _highest_sent;
        }

        auto *p = RlbTcpPacket::newpkt(_top, _flow, _flow_src,
                                       _flow_dst, _sink, this, _highest_sent + 1,
                                       data_seq,
                                         size_to_send);

        p->flow().logTraffic(*p, *this, TrafficLogger::PKT_CREATESEND);
        p->set_ts(eventlist().now());

        _highest_sent += size_to_send;  //XX beware wrapping
        _packets_sent += size_to_send;

        sendToRlbModule(p);

        if (_RFC2988_RTO_timeout == timeInf) {// RFC2988 5.1
            _RFC2988_RTO_timeout = eventlist().now() + _rto;
        }
        if (last_packet) break;
    }
}

void RlbTcpSrc::retransmit_packet() {
    if (!_established) {
        assert(_highest_sent == 1);

        Packet *p = RlbTcpPacket::new_syn_pkt(_top, _flow, _flow_src, _flow_dst, _sink, this, 1, 1);
        sendToRlbModule(p);

        cout << "Resending SYN, waiting for SYN/ACK " << _flow_src << "->" << _flow_dst << " " << _flow_size << endl;
        return;
    }

    uint64_t data_seq = 0;


    auto *p = RlbTcpPacket::newpkt(_top, _flow, _flow_src, _flow_dst,_sink, this,  _last_acked + 1, data_seq, _mss);


    p->flow().logTraffic(*p, *this, TrafficLogger::PKT_CREATESEND);
    p->set_ts(eventlist().now());
    sendToRlbModule(p);

    _packets_sent += _mss;

    if (_RFC2988_RTO_timeout == timeInf) {// RFC2988 5.1
        _RFC2988_RTO_timeout = eventlist().now() + _rto;
    }
}

void RlbTcpSrc::rtx_timer_hook(simtime_picosec now, simtime_picosec period) {
    if (now <= _RFC2988_RTO_timeout || _RFC2988_RTO_timeout == timeInf)
        return;

    if (_highest_sent == 0)
        return;

    /*cout << "At " << timeAsSec(now) << " RTO " << _rto / 1000000000 << " MDEV "
         << _mdev / 1000000000 << " RTT " << _rtt / 1000000000 << " SEQ " << _last_acked / _mss << " HSENT "
         << _highest_sent
         << " CWND " << _cwnd / _mss << " FAST RECOVERY? " << _in_fast_recovery << " Flow ID "
         << str() << endl;
    */
    // here we can run into phase effects because the timer is checked
    // only periodically for ALL flows but if we keep the difference
    // between scanning time and real timeout time when restarting the
    // flows we should minimize them !
    if (!_rtx_timeout_pending) {
        _rtx_timeout_pending = true;

        // check the timer difference between the event and the real value
        simtime_picosec too_late = now - (_RFC2988_RTO_timeout);

        // careful: we might calculate a negative value if _rto suddenly drops very much
        // to prevent overflow but keep randomness we just divide until we are within the limit
        while (too_late > period) too_late >>= 1;

        // carry over the difference for restarting
        simtime_picosec rtx_off = (period - too_late) / 200;

        eventlist().sourceIsPendingRel(*this, rtx_off);

        //reset our rtx timerRFC 2988 5.5 & 5.6

        _rto *= 2;
        //if (_rto > timeFromMs(1000))
        //  _rto = timeFromMs(1000);
        _RFC2988_RTO_timeout = now + _rto;
    }
}

void RlbTcpSrc::doNextEvent() {
    if (_rtx_timeout_pending) {
        _rtx_timeout_pending = false;

        if (_in_fast_recovery) {
            uint32_t flightsize = _highest_sent - _last_acked;
            _cwnd = min(_ssthresh, flightsize + _mss);
        }

        deflate_window();

        _cwnd = _mss;

        _unacked = _cwnd;
        _effcwnd = _cwnd;
        _in_fast_recovery = false;
        _recoverq = _highest_sent;

        if (_established)
            _highest_sent = _last_acked + _mss;

        _dupacks = 0;

        retransmit_packet();

        if (_sawtooth > 0)
            _rtt_avg = _rtt_cum / _sawtooth;
        else
            _rtt_avg = timeFromMs(0);

        _sawtooth = 0;
        _rtt_cum = timeFromMs(0);
    } else {
        //cout << "Starting flow" << endl;
        startflow();
    }
}




////////////////////////////////////////////////////////////////
//  RLB SINK
////////////////////////////////////////////////////////////////


RlbTcpSink::RlbTcpSink(DynExpTopology* top, EventList &eventlist, int flow_src, int flow_dst)
    : RlbSink(top, eventlist, flow_src, flow_dst)
{
    _src = nullptr;
    _nodename = "rlbsink";
    _total_received = 0;
    _pkts_received = 0;

    _bytes_received_retransmits = 0;
    _pkts_received_retransmits = 0;

    distribution_hops_per_bit.resize(10);
    distribution_hops_per_packet.resize(10);
}

void RlbTcpSink::doNextEvent() {
    // just a hack to get access to eventlist
}

void RlbTcpSink::connect(RlbTcpSrc& src)
{
    _src = &src;
    _cumulative_ack = 0;
    _drops = 0;
}

// Note: _cumulative_ack is the last byte we've ACKed.
// seqno is the first byte of the new packet.
void RlbTcpSink::receivePacket(Packet& pkt) {
    RlbTcpPacket *p = (RlbTcpPacket*)(&pkt);

    switch (pkt.type()) {
    case NDP:
    case NDPACK:
    case NDPNACK:
    case NDPPULL:
    case RLB:
    case RLBTCPACK:
        cout << "RLB receiver received an NDP packet!" << endl;
        abort();
    case RLBTCP:
        break;
    }
    RlbTcpPacket::seq_t seqno = p->seqno();
    simtime_picosec ts = p->ts();

    bool marked = p->flags() & ECN_CE;

    int size = p->size(); // TODO: the following code assumes all packets are the same size
    uint64_t hops = p->get_crthop() + 1;


    pkt.flow().logTraffic(pkt, *this, TrafficLogger::PKT_RCVDESTROY);
    p->free();

    _packets += size;

    _total_received+=size;

    total_hops_per_bit += hops * size;
    total_received_packets += 1;

    uint index = (hops >= 10) ? 9 : hops-1;
    distribution_hops_per_packet[index] += 1;
    distribution_hops_per_bit[index] += size;

    //cout << "Sink recv seqno " << seqno << " size " << size << endl;

    if (seqno == _cumulative_ack + 1) { // it's the next expected seq no
        _cumulative_ack = seqno + size - 1;
        //cout << "New cumulative ack is " << _cumulative_ack << endl;
        // are there any additional received packets we can now ack?
        while (!_received.empty() && (_received.front() == _cumulative_ack + 1)) {
            _received.pop_front();
            _cumulative_ack += size;
        }
    } else if (seqno < _cumulative_ack + 1) {
    } else { // it's not the next expected sequence number
        if (_received.empty()) {
            _received.push_front(seqno);
            //it's a drop in this simulator there are no reorderings.
            _drops += (1000 + seqno - _cumulative_ack - 1) / 1000;
        } else if (seqno > _received.back()) { // likely case
            _received.push_back(seqno);
        } else { // uncommon case - it fills a hole
            list<uint64_t>::iterator i;
            for (i = _received.begin(); i != _received.end(); i++) {
                if (seqno == *i) break; // it's a bad retransmit
                if (seqno < (*i)) {
                    _received.insert(i, seqno);
                    break;
                }
            }
        }
    }
    send_ack(ts, marked);
}

void RlbTcpSink::send_ack(simtime_picosec ts, bool marked) {
    auto *ack = RlbTcpAck::newpkt(_top, _src->_flow, 0, _cumulative_ack, 0, _flow_dst, _flow_src, _src);

    ack->flow().logTraffic(*ack, *this, TrafficLogger::PKT_CREATESEND);
    ack->set_ts(ts);
    if (marked)
        ack->set_flags(ECN_ECHO);
    else
        ack->set_flags(0);

    sendToRlbModule(ack);
}

void RlbTcpSink::sendToRlbModule(Packet *pkt) {
    // RlbPacket* p = RlbPacket::newpkt(_top, _flow, _flow_src, _flow_dst, _sink, _mss, _pkts_sent);
    // ^^^ this sets the current source and destination (used for routing)
    // RLB module uses the "real source" and "real destination" to make decisions
    pkt->set_dummy(false);
    pkt->set_real_dst(_flow_src); // set the "real" destination
    pkt->set_real_src(_flow_dst); // set the "real" source
    // pkt->set_ts(eventlist().now()); // time sent, not really needed...

    RlbModule* module = _top->get_rlb_module(_flow_dst); // returns pointer to Rlb module
    module->receivePacket(*pkt, 0);
    // debug:
    //cout << "RlbTcpSrc[" << _flow_src << "] has sent " << _pkts_sent << " packets" << endl;
    //cout << "Sent " << _sent << " bytes out of " << _flow_size << " bytes." << endl;
}

////////////////////////////////////////////////////////////////
//  TCP RETRANSMISSION TIMER
////////////////////////////////////////////////////////////////

RlbTcpRtxTimerScanner::RlbTcpRtxTimerScanner(simtime_picosec scanPeriod, EventList &eventlist)
        : EventSource(eventlist, "RtxScanner"), _scanPeriod(scanPeriod) {
    eventlist.sourceIsPendingRel(*this, _scanPeriod);
}

void RlbTcpRtxTimerScanner::registerTcp(RlbTcpSrc &tcpsrc) {
    _tcps.push_back(&tcpsrc);
}

void RlbTcpRtxTimerScanner::doNextEvent() {
    simtime_picosec now = eventlist().now();
    tcps_t::iterator i;
    for (i = _tcps.begin(); i != _tcps.end(); i++) {
        (*i)->rtx_timer_hook(now, _scanPeriod);
    }
    eventlist().sourceIsPendingRel(*this, _scanPeriod);
}
