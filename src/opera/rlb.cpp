// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-    
#include <math.h>
#include <iostream>
#include "rlb.h"
#include "queue.h"
#include <stdio.h>

#include "rlbmodule.h"

////////////////////////////////////////////////////////////////
//  RLB SOURCE
////////////////////////////////////////////////////////////////

RlbSrc::RlbSrc(DynExpTopology* top, NdpLogger* logger, TrafficLogger* pktlogger, EventList &eventlist, int flow_src, int flow_dst)
    : EventSource(eventlist,"rlbsrc"), _logger(logger), _flow(pktlogger), _flow_src(flow_src), _flow_dst(flow_dst), _top(top) {
    
    _mss = 1436; // 8936; // 1436; // Packet::data_packet_size(); // maximum segment size (mss)
    _sink = 0;
    //_flow_size = ((uint64_t)1)<<63; // this should always get set after `new RlbSrc()` gets called
    _pkts_sent = 0;

    //_node_num = _global_node_count++;
    //_nodename = "rlbsrc" + to_string(_node_num);
}

void RlbSrc::connect(RlbSink& sink, simtime_picosec starttime) {
    
    _sink = &sink;
    _flow.id = id; // identify the packet flow with the source that generated it
    _flow._name = _name;
    _sink->connect(*this);

    set_start_time(starttime); // record the start time in _start_time
    eventlist().sourceIsPending(*this,starttime);
}

void RlbSrc::startflow() {
    _sent = 0;

    // debug:
    //cout << "flow size = " << _flow_size << " bytes" << endl;

    _top->add_new_demand(_flow_src, _flow_dst, (mem_b) _flow_size);

    while (_sent < _flow_size) {
        sendToRlbModule();
    }
}

void RlbSrc::sendToRlbModule() {
    RlbPacket* p = RlbPacket::newpkt(_top, _flow, _flow_src, _flow_dst, _sink, _mss, _sent + 1);
    // ^^^ this sets the current source and destination (used for routing)
    // RLB module uses the "real source" and "real destination" to make decisions
    p->set_dummy(false);
    p->set_real_dst(_flow_dst); // set the "real" destination
    p->set_real_src(_flow_src); // set the "real" source
    p->set_ts(eventlist().now()); // time sent, not really needed...
    RlbModule* module = _top->get_rlb_module(_flow_src); // returns pointer to Rlb module
    module->receivePacket(*p, 0);
    _sent = _sent + _mss; // increment how many packets we've sent
    _pkts_sent++;

    // debug:
    //cout << "RlbSrc[" << _flow_src << "] has sent " << _pkts_sent << " packets" << endl;
    //cout << "Sent " << _sent << " bytes out of " << _flow_size << " bytes." << endl;
}

void RlbSrc::doNextEvent() {

    /*
    // first, get the current "superslice"
    int64_t superslice = (eventlist().now() / _top->get_slicetime(3)) %
         _top->get_nsuperslice();
    // next, get the relative time from the beginning of that superslice
    int64_t reltime = eventlist().now() - superslice*_top->get_slicetime(3) -
        (eventlist().now() / (_top->get_nsuperslice()*_top->get_slicetime(3))) * 
        (_top->get_nsuperslice()*_top->get_slicetime(3));
    int slice; // the current slice
    if (reltime < _top->get_slicetime(0))
        slice = 0 + superslice*3;
    else if (reltime < _top->get_slicetime(0) + _top->get_slicetime(1))
        slice = 1 + superslice*3;
    else
        slice = 2 + superslice*3;
    */

    // debug:
    //cout << "Starting flow at " << timeAsUs(eventlist().now()) << " us (current slice = " << slice << ")" << endl;

    startflow();
}


////////////////////////////////////////////////////////////////
//  RLB SINK
////////////////////////////////////////////////////////////////

mem_b RlbSink::_global_received_bytes = 0;

RlbSink::RlbSink(DynExpTopology* top, EventList &eventlist, int flow_src, int flow_dst)
    : EventSource(eventlist,"rlbsnk"), _total_received(0), _flow_src(flow_src), _flow_dst(flow_dst), _top(top)
{
    _src = 0;
    _nodename = "rlbsink";
    _total_received = 0;
    _pkts_received = 0;

    distribution_hops_per_bit.resize(10);
    distribution_hops_per_packet.resize(10);

    min_seqno_diff = -1;
    max_seqno_diff = 0;
    sum_seqno_diff = 0;
    num_seqno_diff = 0;
}

void RlbSink::doNextEvent() {
    // just a hack to get access to eventlist
}

void RlbSink::connect(RlbSrc& src)
{
    _src = &src;
}

// Receive a packet.
void RlbSink::receivePacket(Packet& pkt) {
    RlbPacket *p = (RlbPacket*)(&pkt);
    auto seqno = p->seqno();
    // debug:
    //if (p->seqno() == 1)
    //    cout << "v marked packet received" << endl;

    //simtime_picosec ts = p->ts();

    switch (pkt.type()) {
    case NDP:
    case NDPACK:
    case NDPNACK:
    case NDPPULL:
        cout << "RLB receiver received an NDP packet!" << endl;
        abort();
    case RLB:
        break;
    }

    // debug:
    _pkts_received++;
    //cout << " RlbSink[" << _flow_dst << "] has received " << _pkts_received << " packets" << endl;
    //cout << ">>>Sink: pkt# = " << _pkts_received << ", seqno = " << p->seqno() << " received." << endl;
    //cout << "   received at: " << timeAsMs(eventlist().now()) << " ms" << endl;

    int size = p->size()-HEADER;
    _total_received += size;

    uint64_t hops = p->get_real_crthop() + 1;
    uint index = (hops >= 10) ? 9 : hops-1;
    distribution_hops_per_packet[index] += 1;
    distribution_hops_per_bit[index] += size;

    _top->reduce_received_demand( _src->get_flow_src(), _src->get_flow_dst(), size);
    _global_received_bytes += size;
    p->free();

    /*int64_t seqno_diff = ((int64_t) seqno - ((int64_t) _cumulative_ack + 1));

    Packet::get_seqno_diffs()[seqno_diff / Packet::data_packet_size()] += 1;

    if (seqno_diff < min_seqno_diff || min_seqno_diff == -1) {
        min_seqno_diff = seqno_diff;
    }
    max_seqno_diff = (seqno_diff > max_seqno_diff) ? seqno_diff : max_seqno_diff;
    sum_seqno_diff += seqno_diff;
    num_seqno_diff += 1;

    if (seqno == _cumulative_ack+1) { // it's the next expected seq no
        _cumulative_ack = seqno + size - 1;

        // are there any additional received packets we can now ack?
        while (!_received.empty() && (_received.front() == _cumulative_ack+1) ) {
            _received.pop_front();
            _cumulative_ack+= _received_sizes.front();
            _received_sizes.pop_front();
        }

    } else if (seqno < _cumulative_ack+1) {
        //must have been a bad retransmit
    } else { // it's not the next expected sequence number
        if (_received.empty()) {
            _received.push_front(seqno);
            _received_sizes.push_front(size);
        } else if (seqno > _received.back()) { // likely case
            _received.push_back(seqno);
            _received_sizes.push_back(size);
        }
        else { // uncommon case - it fills a hole
            list<uint64_t>::iterator i;
            list<mem_b>::iterator j = _received_sizes.begin();
            for (i = _received.begin(); i != _received.end(); i++) {
                j++;
                if (seqno == *i) break; // it's a bad retransmit
                if (seqno < (*i)) {
                    _received.insert(i, seqno);
                    _received_sizes.insert(j, size);
                    break;
                }
            }
        }
    }*/


    if (_total_received >= _src->get_flowsize()) {

        // debug:
        //cout << ">>> Received everything from RLB flow [" << _src->get_flow_src() << "->" << _src->get_flow_dst() << "]" << endl;

        // FCT output for processing: (src dst bytes fct_ms timestarted_ms)

        cout << "FCT " << _src->get_flow_src() << " " << _src->get_flow_dst() << " " << _src->get_flowsize() <<
            " " << timeAsMs(eventlist().now() - _src->get_start_time()) << " " << fixed << timeAsMs(_src->get_start_time()) << " ";

        for (uint i = 0; i < 10; i++) {
            cout  << distribution_hops_per_bit[i] << ",";
        }
        cout  << " ";
        for (uint i = 0; i < 10; i++) {
            cout << distribution_hops_per_packet[i] << ",";
        }

        cout << " seqno_diff=" << (double) sum_seqno_diff / num_seqno_diff;
        cout << " min_seqno_diff=" << min_seqno_diff;
        cout << " max_seqno_diff=" << max_seqno_diff;

        cout << endl;
    }
}
