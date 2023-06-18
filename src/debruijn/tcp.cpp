// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#include "tcp.h"
#include "ecn.h"
#include "queue.h"
#include <iostream>

#define KILL_THRESHOLD 5
////////////////////////////////////////////////////////////////
//  TCP SOURCE
////////////////////////////////////////////////////////////////

TcpSrc::TcpSrc(DebruijnTopology* top, TcpLogger *logger, TrafficLogger *pktlogger,
               EventList &eventlist, int flowSrc, int flowDst, bool is_priority_flow)
        : EventSource(eventlist, "tcp"), _top(top), _logger(logger), _flow(pktlogger),
            _flow_src(flowSrc), _flow_dst(flowDst), _is_priority_flow(is_priority_flow) {
    _mss = Packet::data_packet_size();
    _maxcwnd = 0xffffffff;//MAX_SENT*_mss;
    _sawtooth = 0;
    _rtt_avg = timeFromMs(0);
    _rtt_cum = timeFromMs(0);
    _base_rtt = timeInf;
    _cap = 0;
    _flow_size = ((uint64_t) 1) << 63;
    _highest_sent = 0;
    _packets_sent = 0;
    _app_limited = -1;
    _established = false;
    _effcwnd = 0;

    _start_time = 0;
    _time_handshake_complete = 0;

    //_ssthresh = 30000;
    _ssthresh = 0xffffffff;

#ifdef MODEL_RECEIVE_WINDOW
    _highest_data_seq = 0;
#endif

    _last_acked = 0;
    _last_ping = timeInf;
    _dupacks = 0;
    _rtt = 0;
    _rto = timeFromMs(3000);
    _mdev = 0;
    _recoverq = 0;
    _in_fast_recovery = false;
    _drops = 0;

#ifdef PACKET_SCATTER
    _crt_path = 0;
    DUPACK_TH = 3;
    _paths = NULL;
#endif

    _rtx_timeout_pending = false;
    _RFC2988_RTO_timeout = timeInf;

    _nodename = "tcpsrc";
    _finished = false;

    _num_additional_acks = 0;
    _num_rtos = 0;
    _num_total_dupacks = 0;

    _dropped_at_hop.resize(10);
}

#ifdef PACKET_SCATTER
void TcpSrc::set_paths(vector<const Route*>* rt) {
    //this should only be used with route
    _paths = new vector<const Route*>();

    for (unsigned int i=0;i<rt->size();i++){
    Route* t = new Route(*(rt->at(i)));
    t->push_back(_sink);
    _paths->push_back(t);
    }
    DUPACK_TH = 3 + rt->size();
    cout << "Setting DUPACK TH to " << DUPACK_TH << endl;
}
#endif

void TcpSrc::startflow() {
    _cwnd = 10 * _mss;
    _unacked = _cwnd;
    _established = false;

    _top->add_new_demand(_flow_src, _flow_dst, _flow_size);

    send_packets();
}

uint32_t TcpSrc::effective_window() {
    return _in_fast_recovery ? _ssthresh : _cwnd;
}

void TcpSrc::connect(TcpSink &sink, simtime_picosec starttime) {
    _sink = &sink;
    _flow.id = id; // identify the packet flow with the TCP source that generated it
    _sink->connect(*this);

    set_start_time(starttime);
    //printf("Tcp %x msrc %x\n",this,_mSrc);
    eventlist().sourceIsPending(*this, starttime);
}

#define ABS(X) ((X)>0?(X):-(X))

void TcpSrc::sendToNIC(Packet* pkt) {
    DebruijnTopology* top = pkt->get_topology();
    Queue* nic = top->get_queue_serv_tor(pkt->get_src()); // returns pointer to nic queue
    nic->receivePacket(*pkt); // send this packet to the nic queue
}

void TcpSrc::receivePacket(Packet &pkt) {
    simtime_picosec ts;
    TcpAck *p = (TcpAck *) (&pkt);
    TcpAck::seq_t seqno = p->ackno();

#ifdef MODEL_RECEIVE_WINDOW
    if (_mSrc)
    _mSrc->receivePacket(pkt);
#endif

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
        _time_handshake_complete = eventlist().now() - _start_time;
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
        if (_finished) {
            _num_additional_acks++;
            cout << "Received another ACK after flow has finished seqno: " << seqno << " " << _flow_src << " " << _flow_dst << " " << _flow_size << " "
            << _start_time << " " << pkt.id() << " " << _num_additional_acks << endl;
            return;
         }
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
            cout << _dropped_at_hop[i] << ",";
        }

        cout << " hops_da=";
        for (uint i = 0; i < 10; i++) {
            cout  << _sink->distribution_da_hops_per_bit[i] << ",";
        }

        cout << " hops_static=";
        for (uint i = 0; i < 10; i++) {
            cout  << _sink->distribution_static_hops_per_bit[i] << ",";
        }

        cout << " redirected_bytes=" << _sink->_bytes_redirected;
        cout << " num_rtos=" << _num_rtos;
        cout << " total_num_dupacks=" << _num_total_dupacks;
        cout << " bytes_imh=" << _sink->_bytes_imh_received;
        cout << " bytes_1dahop=" << _sink->_bytes_onedahop;

        cout << " handshake_time=" << timeAsMs(_time_handshake_complete);

        cout << " seqno_diff=" << (double) _sink->sum_seqno_diff / _sink->num_seqno_diff;
        cout << " min_seqno_diff=" << _sink->min_seqno_diff;
        cout << " max_seqno_diff=" << _sink->max_seqno_diff;

        cout << endl;
        _finished = true;
    }

    if (seqno > _last_acked) { // a brand new ack
        _RFC2988_RTO_timeout = eventlist().now() + _rto;// RFC 2988 5.3
        _last_ping = eventlist().now();

        if (seqno >= _highest_sent) {
            _highest_sent = seqno;
            _RFC2988_RTO_timeout = timeInf;// RFC 2988 5.2
            _last_ping = timeInf;
        }

#ifdef MODEL_RECEIVE_WINDOW
        int cnt;

        _sent_packets.ack_packet(seqno);

        //if ((cnt = _sent_packets.ack_packet(seqno)) > 2)
        //  cout << "ACK "<<cnt<<" packets on " << _flow.id << " " << _highest_sent+1 << " packets in flight " << _sent_packets.crt_count << " diff " << (_highest_sent+_mss-_last_acked)/1000 << " last_acked " << _last_acked << " at " << timeAsMs(eventlist().now()) << endl;
#endif

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
            if (_logger)
                _logger->logTcp(*this, TcpLogger::TCP_RCV);
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

            if (_logger)
                _logger->logTcp(*this, TcpLogger::TCP_RCV_FR_END);
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
        if (_logger)
            _logger->logTcp(*this, TcpLogger::TCP_RCV_FR);
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
        if (_logger)
            _logger->logTcp(*this, TcpLogger::TCP_RCV_DUP_FR);
        send_packets();
        return;
    }
    // Not yet in fast recovery. What should we do instead?
    _dupacks++;
    _num_total_dupacks++;

#ifdef PACKET_SCATTER
    if (_dupacks!=DUPACK_TH)
#else
    if (_dupacks != 3)
#endif
    { // not yet serious worry
        if (_logger)
            _logger->logTcp(*this, TcpLogger::TCP_RCV_DUP);
        send_packets();
        return;
    }
    // _dupacks==3
    // cout << "Entering fast retransmit " << _flow_src << " " << _flow_dst << " " << _flow_size << endl;
    if (_last_acked < _recoverq) {
        /* See RFC 3782: if we haven't recovered from timeouts
	   etc. don't do fast recovery */
        if (_logger)
            _logger->logTcp(*this, TcpLogger::TCP_RCV_3DUPNOFR);
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
    if (_logger)
        _logger->logTcp(*this, TcpLogger::TCP_RCV_DUP_FASTXMIT);
}

void TcpSrc::deflate_window() {
    _ssthresh = max(_cwnd / 2, (uint32_t) (2 * _mss));
}

void
TcpSrc::inflate_window() {
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

        //int tt = (newly_acked * _mss) % _cwnd;
        _cwnd += (newly_acked * _mss) / _cwnd;  //XXX beware large windows, when this increase gets to be very small

        //if (rand()%_cwnd < tt)
        //_cwnd++;

        if (pkts != _cwnd / _mss) {
            _rtt_cum += _rtt;
            _sawtooth++;
        }
    }
}

// Note: the data sequence number is the number of Byte1 of the packet, not the last byte.
void
TcpSrc::send_packets() {
    int c = _cwnd;

    if (!_established) {
        //send SYN packet and wait for SYN/ACK
        Packet *p = TcpPacket::new_syn_pkt(_top, _flow, 1, 1, _flow_src, _flow_dst, this, _sink);
        _highest_sent = 1;

        sendToNIC(p);

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
        //if (c<1000)
        //c = 1000;

        if (c == 0) {
            //      _RFC2988_RTO_timeout = timeInf;
        }

        //rtt in ms
        //printf("%d\n",c);
    }

    while ((_last_acked + c >= _highest_sent + _mss) && (_highest_sent < _flow_size+1)) {
        uint64_t data_seq = 0;

        uint16_t size_to_send = _mss;
        bool last_packet = false;
        if (_highest_sent + _mss > _flow_size+1) {
            last_packet = true;
            size_to_send = _flow_size + 1 - _highest_sent;
        }


#ifdef MODEL_RECEIVE_WINDOW
        int existing_mapping = 0;
        if (_sent_packets.have_mapping(_highest_sent+1)) {
            if (!_sent_packets.get_data_seq(_highest_sent+1, &data_seq)) {
            cout << "Failed to find TRANSMIT packet on " << _flow.id << " last_acked "
                 << _last_acked+1 << " highest_sent " << _highest_sent << endl;
            assert(0);
            } else {
            existing_mapping = 1;
            }
        } else {
            if (_mSrc && !_mSrc->getDataSeq(&data_seq,this))
            break;
        }

        if (!existing_mapping)
            _sent_packets.add_packet(_highest_sent+1, data_seq);

        if (data_seq>_highest_data_seq)
            _highest_data_seq = data_seq;

        //      cout << "Transmit packet on " << _flow.id << " " << _highest_sent+1 << "[" << data_seq << "] packets in flight " << _sent_packets.crt_count << " diff " << (_highest_sent+_mss-_last_acked)/1000 << " last_acked " << _last_acked << " at " << timeAsMs(eventlist().now()) << endl;
#endif

#ifdef PACKET_SCATTER
        TcpPacket* p;

        if (_paths){

#ifdef RANDOM_PATH
            _crt_path = random()%_paths->size();
#endif

            p = TcpPacket::newpkt(_flow, *(_paths->at(_crt_path)), _highest_sent+1,
                      data_seq, _mss);
            _crt_path = (_crt_path + 1) % _paths->size();
        } else {
            p  = TcpPacket::newpkt(_flow, *_route, _highest_sent+1, data_seq, _mss);
        }
#else
        TcpPacket *p = TcpPacket::newpkt(_top, _flow, _highest_sent + 1, data_seq,
                                         size_to_send, _flow_src, _flow_dst, this, _sink);
#endif
        p->flow().logTraffic(*p, *this, TrafficLogger::PKT_CREATESEND);
        p->set_ts(eventlist().now());

        _highest_sent += size_to_send;  //XX beware wrapping
        _packets_sent += size_to_send;

        sendToNIC(p);

        if (_RFC2988_RTO_timeout == timeInf) {// RFC2988 5.1
            _RFC2988_RTO_timeout = eventlist().now() + _rto;
        }
        if (last_packet) break;
    }
}

void
TcpSrc::retransmit_packet() {
    if (!_established) {
        assert(_highest_sent == 1);

        Packet *p = TcpPacket::new_syn_pkt(_top, _flow, 1, 1, _flow_src, _flow_dst, this, _sink);
        sendToNIC(p);

        cout << "Resending SYN, waiting for SYN/ACK" << endl;
        return;
    }

    uint64_t data_seq = 0;

#ifdef MODEL_RECEIVE_WINDOW
    if (! _sent_packets.get_data_seq(_last_acked+1, &data_seq)){
    cout << "Failed to find packet on " << _flow.id << " last_acked " << _last_acked+1 << " highest_sent " << _highest_sent << endl;
    assert(NULL);
    }
    //  else
    //  cout << "Retransmit packet on " << _flow.id << " " << _last_acked+1 << " " << data_seq << endl;
#endif


#ifdef PACKET_SCATTER
    TcpPacket* p;
    if (_paths) {

#ifdef RANDOM_PATH
    _crt_path = random()%_paths->size();
#endif

    p = TcpPacket::newpkt(_flow, *(_paths->at(_crt_path)), _last_acked+1, data_seq, _mss);
    _crt_path = (_crt_path + 1) % _paths->size();
    } else {
    p = TcpPacket::newpkt(_flow, *_route, _last_acked+1, _mss);
    }
#else
    TcpPacket *p = TcpPacket::newpkt(_top, _flow, _last_acked + 1, data_seq, _mss, _flow_src, _flow_dst, this, _sink);
#endif

    p->flow().logTraffic(*p, *this, TrafficLogger::PKT_CREATESEND);
    p->set_ts(eventlist().now());
    sendToNIC(p);

    _packets_sent += _mss;

    if (_RFC2988_RTO_timeout == timeInf) {// RFC2988 5.1
        _RFC2988_RTO_timeout = eventlist().now() + _rto;
    }
}

void TcpSrc::rtx_timer_hook(simtime_picosec now, simtime_picosec period) {
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

        // Count RTO
        _num_rtos++;

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

void TcpSrc::doNextEvent() {
    if (_rtx_timeout_pending) {
        _rtx_timeout_pending = false;

        if (_logger)
            _logger->logTcp(*this, TcpLogger::TCP_TIMEOUT);

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
//  TCP SINK
////////////////////////////////////////////////////////////////

mem_b TcpSink::_global_received_bytes = 0;

TcpSink::TcpSink(DebruijnTopology* top, int flowSrc, int flowDst)
        : Logged("sink"), _cumulative_ack(0), _packets(0), _top(top), _crt_path(0), _flow_src(flowSrc), _flow_dst(flowDst) {
    _nodename = "tcpsink";

    distribution_hops_per_bit.resize(10);
    distribution_da_hops_per_bit.resize(10);
    distribution_static_hops_per_bit.resize(10);
    distribution_hops_per_packet.resize(10);
    _bytes_received_retransmits = 0;
    _pkts_received_retransmits = 0;
    _bytes_redirected = 0;
    _total_received = 0;
    _bytes_imh_received = 0;
    _bytes_onedahop = 0;
    total_hops_per_bit = 0;
    total_received_packets = 0;
    _drops = 0;
    _src = nullptr;

    min_seqno_diff = -1;
    max_seqno_diff = 0;
    sum_seqno_diff = 0;
    num_seqno_diff = 0;
}

void
TcpSink::connect(TcpSrc &src) {
    _src = &src;
    _cumulative_ack = 0;
    _drops = 0;
}

// Note: _cumulative_ack is the last byte we've ACKed.
// seqno is the first byte of the new packet.
void
TcpSink::receivePacket(Packet &pkt) {
    auto *p = (TcpPacket *) (&pkt);
    TcpPacket::seq_t seqno = p->seqno();
    simtime_picosec ts = p->ts();

    bool marked = p->flags() & ECN_CE;

    int size = p->size() - HEADER; // TODO: the following code assumes all packets are the same size
    uint64_t hops = p->get_real_crthop() + 1;
    if (size < 0) {
        throw std::runtime_error("Size of received packet < 0");
    }

    if (pkt.has_been_indirected()) {
        _bytes_redirected += size;
    }

    int da_hops = (pkt.get_hops_da() > 10) ? 10 : pkt.get_hops_da();
    if (da_hops > 0)
        distribution_da_hops_per_bit[da_hops-1] += size;
    da_hops = (pkt.get_hops_static() > 10) ? 10 : pkt.get_hops_static();
    if (da_hops > 0)
        distribution_static_hops_per_bit[da_hops-1] += size;

    /*    if (p->retransmitted()) {
        _bytes_received_retransmits += size;
        _pkts_received_retransmits++;
    }*/

    if (p->used_da_and_static()) {
        _bytes_imh_received += size;
    }

    if (p->get_hops_da() == 1 && p->get_hops_static() == 0) {
        _bytes_onedahop += size;
    }

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
    int64_t seqno_diff = ((int64_t) seqno - ((int64_t) _cumulative_ack + 1));

    Packet::get_seqno_diffs()[seqno_diff / Packet::data_packet_size()] += 1;

    if (seqno_diff < min_seqno_diff || min_seqno_diff == -1) {
        min_seqno_diff = seqno_diff;
    }
    max_seqno_diff = (seqno_diff > max_seqno_diff) ? seqno_diff : max_seqno_diff;
    sum_seqno_diff += seqno_diff;
    num_seqno_diff += 1;

    if (seqno == _cumulative_ack + 1) { // it's the next expected seq no
        _cumulative_ack = seqno + size - 1;

	    // TODO Decrease demand in matrix is this a good location?
        _top->reduce_received_demand(_flow_src, _flow_dst, size);
        _global_received_bytes += size;
        //cout << "New cumulative ack is " << _cumulative_ack << endl;
        // are there any additional received packets we can now ack?
        while (!_received.empty() && (_received.front() == _cumulative_ack + 1)) {
            _received.pop_front();
            _cumulative_ack += _received_sizes.front();

            // Reduce demand also for packets that came out-of-order
            _top->reduce_received_demand(_flow_src, _flow_dst,  _received_sizes.front());
            _global_received_bytes += _received_sizes.front();
            _received_sizes.pop_front();
        }
    } else if (seqno < _cumulative_ack + 1) {
    } else { // it's not the next expected sequence number
        if (_received.empty()) {
            _received.push_front(seqno);
            _received_sizes.push_front(size);   // keep track of packet sizes as well
            //it's a drop in this simulator there are no reorderings.
            _drops += (1000 + seqno - _cumulative_ack - 1) / 1000;
        } else if (seqno > _received.back()) { // likely case
            _received.push_back(seqno);
            _received_sizes.push_back(size);
        } else { // uncommon case - it fills a hole
            list<uint64_t>::iterator i;
            auto j = _received_sizes.begin();
            for (i = _received.begin(); i != _received.end(); i++) {
                j++;
                if (seqno == *i) break; // it's a bad retransmit
                if (seqno < (*i)) {
                    _received.insert(i, seqno);
                    _received_sizes.insert(j,  size);
                    break;
                }
            }
        }
    }
    send_ack(ts, marked);
}

void TcpSink::sendToNIC(Packet* pkt) {
    DebruijnTopology* top = pkt->get_topology();
    Queue* nic = top->get_queue_serv_tor(pkt->get_src()); // returns pointer to nic queue
    nic->receivePacket(*pkt); // send this packet to the nic queue
}

void TcpSink::send_ack(simtime_picosec ts, bool marked) {
#ifdef PACKET_SCATTER
    if (_paths){
#ifdef RANDOM_PATH
    _crt_path = random()%_paths->size();
#endif

    rt = _paths->at(_crt_path);
    _crt_path = (_crt_path+1)%_paths->size();
    }
#endif
    TcpAck *ack = TcpAck::newpkt(_top, _src->_flow, 0, _cumulative_ack, 0, _flow_src, _flow_dst, _src);

    ack->flow().logTraffic(*ack, *this, TrafficLogger::PKT_CREATESEND);
    ack->set_ts(ts);
    if (marked)
        ack->set_flags(ECN_ECHO);
    else
        ack->set_flags(0);

    sendToNIC(ack);
}

#ifdef PACKET_SCATTER
void TcpSink::set_paths(vector<const Route*>* rt) {
    //this should only be used with route
    _paths = new vector<const Route*>();

    for (unsigned int i=0;i<rt->size();i++){
    Route* t = new Route(*(rt->at(i)));
    t->push_back(_src);
    _paths->push_back(t);
    }
}
#endif

////////////////////////////////////////////////////////////////
//  TCP RETRANSMISSION TIMER
////////////////////////////////////////////////////////////////

TcpRtxTimerScanner::TcpRtxTimerScanner(simtime_picosec scanPeriod, EventList &eventlist)
        : EventSource(eventlist, "RtxScanner"), _scanPeriod(scanPeriod) {
    eventlist.sourceIsPendingRel(*this, _scanPeriod);
}

void
TcpRtxTimerScanner::registerTcp(TcpSrc &tcpsrc) {
    _tcps.push_back(&tcpsrc);
}

void TcpRtxTimerScanner::doNextEvent() {
    simtime_picosec now = eventlist().now();
    tcps_t::iterator i;
    for (i = _tcps.begin(); i != _tcps.end(); i++) {
        (*i)->rtx_timer_hook(now, _scanPeriod);
    }
    eventlist().sourceIsPendingRel(*this, _scanPeriod);
}
