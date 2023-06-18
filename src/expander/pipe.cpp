// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#include "pipe.h"
#include <iostream>
#include <sstream>
#include "ndppacket.h"

Pipe::Pipe(simtime_picosec delay, EventList &eventlist)
        : EventSource(eventlist, "pipe"), _delay(delay) {
    stringstream ss;
    ss << "pipe(" << delay / 1000000 << "us)";
    _nodename = ss.str();

    _pipe_is_downlink = false; // added for util
    _bytes_delivered = 0; // added for util
    _uplink_pipe_id = 100;

    _header_bytes_delivered = 0;
    _bytes_sent_retransmit = 0;
    _no_sent_packets = 0;
    _sum_hop_count = 0;
}

void Pipe::receivePacket(Packet &pkt) {
    pkt.flow().logTraffic(pkt, *this, TrafficLogger::PKT_ARRIVE);
    if (_inflight.empty()) {
        /* no packets currently inflight; need to notify the eventlist
           we've an event pending */
        eventlist().sourceIsPendingRel(*this, _delay);
    }
    _inflight.push_front(make_pair(eventlist().now() + _delay, &pkt));
}


uint64_t Pipe::reportBytes() {
    uint64_t temp;
    temp = _bytes_delivered;
    _bytes_delivered = 0; // reset the counter
    return temp;
}


mem_b Pipe::reportHeaderBytes() {
    mem_b temp;
    temp = _header_bytes_delivered;
    _header_bytes_delivered = 0; // reset the counter
    return temp;
}

uint64_t Pipe::reportBytesRtx() {
    uint64_t temp;
    temp = _bytes_sent_retransmit;
    _bytes_sent_retransmit = 0; // reset the counter
    return temp;
}

double Pipe::reportAvgHopCount() {
    if (_no_sent_packets == 0) return 0;
    double avg = (double) _sum_hop_count / (double) _no_sent_packets;
    _sum_hop_count = 0;
    _no_sent_packets = 0;
    return avg;
}

void Pipe::get_hop_count(Packet *pkt) {
    _sum_hop_count += (uint64_t ) pkt->current_hop;
    _no_sent_packets++;
}

void Pipe::doNextEvent() {
    if (_inflight.size() == 0)
        return;

    Packet *pkt = _inflight.back().second;
    _inflight.pop_back();
    pkt->flow().logTraffic(*pkt, *this, TrafficLogger::PKT_DEPART);

    // debug:
    if (_uplink_pipe_id < 16) {
        switch (pkt->type()) {
            case NDP: {
                if (pkt->size() > 64) { // not a header
                    cout << _uplink_pipe_id << " " << timeAsUs(eventlist().now()) << endl;
                }
                break;
            }
            case NDPACK:
            case NDPNACK:
            case NDPPULL:
                break;
        }
    }

    // if this pipe is a ToR downlink to server

    // check if we need to count this packet:
    switch (pkt->type()) {
        case TCP: {
            _bytes_delivered = _bytes_delivered + pkt->size(); // increment bytes delivered
            break;
        }
        case TCPACK: {
            _header_bytes_delivered = _header_bytes_delivered + pkt->size();
            break;
        }
        case NDP: {
            //NdpPacket* ndp_pkt = dynamic_cast<NdpPacket*>(pkt);
            //if (!ndp_pkt->retransmitted()) {
            if (pkt->size() > 64) { // not a header
                _bytes_delivered = _bytes_delivered + pkt->size(); // increment bytes delivered
                auto ndppkt = dynamic_cast<NdpPacket *>(pkt);
                    if (ndppkt->retransmitted()) {
                        _bytes_sent_retransmit += pkt->size();
                    }
            } else {
                _header_bytes_delivered = _header_bytes_delivered + pkt->size();
            }
            break;
        }
        case NDPACK:
        case NDPNACK:
        case NDPPULL:
            _header_bytes_delivered = _header_bytes_delivered + pkt->size();
            break;
    }

    pkt->current_hop++;
    get_hop_count(pkt);

    // tell the packet to move itself on to the next hop
    pkt->sendOn();

    if (!_inflight.empty()) {
        // notify the eventlist we've another event pending
        simtime_picosec nexteventtime = _inflight.back().first;
        _eventlist.sourceIsPending(*this, nexteventtime);
    }
}
