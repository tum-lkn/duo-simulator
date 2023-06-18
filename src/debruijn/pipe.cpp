// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#include "pipe.h"
#include <iostream>
#include <sstream>

#include "queue.h"
#include "compositequeue.h"
#include "enhanced_compositequeue.h"
#include "ndp.h"
#include "ndppacket.h"
#include "tcp.cpp"
#include "ndp.cpp"


bool Pipe::segregated_routing = false;


Pipe::Pipe(simtime_picosec delay, EventList &eventlist)
        : EventSource(eventlist, "pipe"), _delay(delay) {
    //stringstream ss;
    //ss << "pipe(" << delay/1000000 << "us)";
    //_nodename= ss.str();

    _bytes_delivered = 0;
    _bytes_sent_retransmit = 0;
    _header_bytes_delivered = 0;
    is_da_pipe = false;
}

Pipe::Pipe(simtime_picosec delay, EventList &eventlist, bool is_da)
        : EventSource(eventlist, "pipe"), _delay(delay) {
    //stringstream ss;
    //ss << "pipe(" << delay/1000000 << "us)";
    //_nodename= ss.str();

    _bytes_delivered = 0;
    _bytes_sent_retransmit = 0;
    _header_bytes_delivered = 0;
    is_da_pipe = is_da;
}

void Pipe::receivePacket(Packet &pkt) {
    //pkt.flow().logTraffic(pkt,*this,TrafficLogger::PKT_ARRIVE);
    //cout << "Pipe: Packet received at " << timeAsUs(eventlist().now()) << " us" << endl;
    if (_inflight.empty()) {
        /* no packets currently inflight; need to notify the eventlist
            we've an event pending */
        eventlist().sourceIsPendingRel(*this, _delay);
    }
    _inflight.push_front(make_pair(eventlist().now() + _delay, &pkt));
}

void Pipe::doNextEvent() {
    if (_inflight.empty())
        return;

    Packet *pkt = _inflight.back().second;
    _inflight.pop_back();
    //pkt->flow().logTraffic(*pkt, *this,TrafficLogger::PKT_DEPART);

    // tell the packet to move itself on to the next hop
    //pkt->sendOn();
    //pkt->sendFromPipe();
    sendFromPipe(pkt);

    if (!_inflight.empty()) {
        // notify the eventlist we've another event pending
        simtime_picosec nexteventtime = _inflight.back().first;
        _eventlist.sourceIsPending(*this, nexteventtime);
    }
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
    _sum_hop_count += pkt->get_real_crthop();
    _no_sent_packets++;
}


void Pipe::sendFromPipe(Packet *pkt) {
    //if (_bounced) {
    //    // !!!
    //    cout << "_bounced not implemented" << endl;
    //    abort();
    //}
    //else {
    if (pkt->is_lasthop()) {

        // debug:
        //if (pkt->been_bounced() == true && pkt->bounced() == false) {
        //    cout << "! Pipe sees a last-hop previously-bounced packet" << endl;
        //    cout << "    src = " << pkt->get_src() << endl;
        //}

        // debug:
        //cout << "Pipe - packet on its last hop" << endl;

        // we'll be delivering to an NdpSink or NdpSrc based on packet type
        // ! OR an RlbModule
        switch (pkt->type()) {
            case NDP: {
                if (!pkt->bounced()) {
                    if (pkt->size() > 64) {// not a header
                        _bytes_delivered = _bytes_delivered + pkt->size(); // increment packet delivered
                        auto ndppkt = dynamic_cast<NdpPacket *>(pkt);
                        if (ndppkt->retransmitted()) {
                            _bytes_sent_retransmit += pkt->size();
                        }
                    } else {
                        _header_bytes_delivered = _header_bytes_delivered + pkt->size();
                    }
                    // send it to the sink
                    PacketSink *sink = pkt->get_flow_sink();
                    assert(sink);
                    sink->receivePacket(*pkt);
                } else {
                    // send it to the source
                    _header_bytes_delivered = _header_bytes_delivered + pkt->size();

                    PacketSink *src = pkt->get_flow_src();
                    assert(src);
                    src->receivePacket(*pkt);
                }

                break;
            }
            case TCP: {
                _bytes_delivered = _bytes_delivered + pkt->size(); // increment packet delivered
                PacketSink *sink = pkt->get_flow_sink();
                assert(sink);
                sink->receivePacket(*pkt);
                break;
            }
            case NDPACK:
            case NDPNACK:
            case NDPPULL:
            case TCPACK: {
                _header_bytes_delivered = _header_bytes_delivered + pkt->size();

                PacketSink *src = pkt->get_flow_src();
                assert(src);
                src->receivePacket(*pkt);
                break;
            }
        }
    } else {
        // we'll be delivering to a ToR queue
        DebruijnTopology *top = pkt->get_topology();

        if (pkt->get_crtToR() < 0) {
            pkt->set_crtToR(pkt->get_src_ToR());
            if (pkt->size() > 64) {// not a header
                _bytes_delivered = _bytes_delivered + pkt->size();
                if (pkt->type() == NDP) {
                    auto ndppkt = dynamic_cast<NdpPacket *>(pkt);
                    if (ndppkt->retransmitted()) {
                        _bytes_sent_retransmit += pkt->size();
                    }
                }
            } else {
                _header_bytes_delivered = _header_bytes_delivered + pkt->size();
            }
        } else {
            // Update here the location of the packet
            int nextToR = top->get_next_tor(pkt->get_crtToR(), pkt->get_crtport());
            if (nextToR >= 0) {// the rotor switch is up
                pkt->set_crtToR(nextToR);
                if (pkt->size() > 64) {// not a header
                    _bytes_delivered = _bytes_delivered + pkt->size();
                    if (pkt->type() == NDP) {
                        auto ndppkt = dynamic_cast<NdpPacket *>(pkt);
                        if (ndppkt->retransmitted()) {
                            _bytes_sent_retransmit += pkt->size();
                        }
                    }
                } else {
                    _header_bytes_delivered = _header_bytes_delivered + pkt->size();
                }
            } else { // the rotor switch is down, "drop" the packet
                switch (pkt->type()) {
                    case NDP: {
                        cout << "!!! NDP packet clipped in pipe (rotor switch down)" << endl;
                        cout << "    time = " << timeAsUs(eventlist().now()) << " us";
                        // cout << "    current slice = " << slice << endl;
                        cout << "    nextToR= " << nextToR << ", currentTor= " << pkt->get_crtToR() << ":"
                             << pkt->get_crtport() << endl;
                        cout << "    src = " << top->get_firstToR(pkt->get_src()) << ", dst = " << pkt->get_dst()
                             << endl;
                        // pkt->free();
                        break;
                    }
                    case NDPACK: {
                        cout << "!!! NDP ACK clipped in pipe (rotor switch down)" << endl;
                        cout << "    time = " << timeAsUs(eventlist().now()) << " us";
                        // cout << "    current slice = " << slice << endl;
                        cout << "    nextToR= " << nextToR << ", currentTor= " << pkt->get_crtToR() << ":"
                             << pkt->get_crtport() << endl;
                        cout << "    src = " << pkt->get_src() << ", dst = " << pkt->get_dst() << endl;
                        // pkt->free();
                        break;
                    }
                    case NDPNACK: {
                        cout << "!!! NDP NACK clipped in pipe (rotor switch down)" << endl;
                        cout << "    time = " << timeAsUs(eventlist().now()) << " us";
                        // cout << "    current slice = " << slice << endl;
                        cout << "    nextToR= " << nextToR << ", currentTor= " << pkt->get_crtToR() << ":"
                             << pkt->get_crtport() << endl;
                        cout << "    src = " << pkt->get_src() << ", dst = " << pkt->get_dst() << endl;
                        // pkt->free();
                        break;
                    }
                    case NDPPULL: {
                        cout << "!!! NDP PULL clipped in pipe (rotor switch down)" << endl;
                        cout << "    time = " << timeAsUs(eventlist().now()) << " us";
                        // cout << "    current slice = " << slice << endl;
                        cout << "    nextToR= " << nextToR << ", currentTor= " << pkt->get_crtToR() << ":"
                             << pkt->get_crtport() << endl;
                        cout << "    src = " << pkt->get_src() << ", dst = " << pkt->get_dst() << endl;
                        // pkt->free();
                        break;
                    }
                    case TCP: {
                        cout << "!!! TCP packet clipped in pipe (rotor switch down)" << endl;
                        cout << "    time = " << timeAsUs(eventlist().now()) << " us";
                        // cout << "    current slice = " << slice << endl;
                        cout << "    nextToR= " << nextToR << ", currentTor= " << pkt->get_crtToR() << ":"
                             << pkt->get_crtport() << endl;
                        cout << "    src = " << top->get_firstToR(pkt->get_src()) << ", dst = " << pkt->get_dst()
                             << endl;
                        // pkt->free();
                        break;
                    }
                    case TCPACK: {
                        cout << "!!! TCPACK packet clipped in pipe (rotor switch down)" << endl;
                        cout << "    time = " << timeAsUs(eventlist().now()) << " us";
                        // cout << "    current slice = " << slice << endl;
                        cout << "    nextToR= " << nextToR << ", currentTor= " << pkt->get_crtToR() << ":"
                             << pkt->get_crtport() << endl;
                        cout << "    src = " << top->get_firstToR(pkt->get_src()) << ", dst = " << pkt->get_dst()
                             << endl;
                        // pkt->free();
                        break;
                    }
                }

                // Add packet back to queue
                top->get_queue_tor(pkt->get_crtToR(), pkt->get_crtport())->receivePacket(*pkt);
                return;
            }
        }

        if (pkt->is_vlb()) {
            if (pkt->get_crtToR() == pkt->get_dst()) {  // If we have intermediate ToR, the destination is a ToR here
                if (pkt->get_crtToR() != pkt->get_real_dst_tor()) { // We have reached the intermediate destination
                    pkt->set_dst(pkt->get_real_dst());
                }
            }
        }

        //cout << "Pipe: the packet is delivered at " << timeAsUs(eventlist().now()) << " us" << endl;
        //cout << "   The curret slice is " << currentslice << endl;
        //cout << "   Upcoming ToR is " << pkt->get_crtToR() << endl;

        pkt->inc_crthop(); // increment the hop

        if (is_da_pipe) {
            pkt->inc_hops_da();
        } else if (pkt->get_real_crthop() > 0 && !pkt->is_lasthop()) {
            pkt->inc_hops_static();
        }
        get_hop_count(pkt);

        /*
         * FINDING THE PORT FOR NEXT HOP STARTS HERE
         */
        int candidate_port;
        if (segregated_routing) {
            if (pkt->get_src_ToR() == pkt->get_crtToR()) {
                candidate_port = top->get_port_segregated_routing_for_destination_tor(
                        pkt->get_crtToR(),
                        pkt->get_dst_ToR()
                );
            } else {
                candidate_port = top->get_port_static(pkt->get_crtToR(), pkt->get_dst());
            }
        } else {
            candidate_port = top->get_port(pkt->get_crtToR(), pkt->get_dst());
        }

        pkt->set_crtport(candidate_port);
        //cout << "   Upcoming port = " << pkt->get_crtport() << endl;

        Queue *nextqueue = top->get_queue_tor(pkt->get_crtToR(), candidate_port);
        nextqueue->receivePacket(*pkt);
        //cout << "pipe delivered to ToR " << pkt->get_crtToR() << ", port " << pkt->get_crtport() << endl;
    }
    //}
}


//////////////////////////////////////////////
//      Aggregate utilization monitor       //
//////////////////////////////////////////////


UtilMonitor::UtilMonitor(DebruijnTopology *top, EventList &eventlist)
        : EventSource(eventlist, "utilmonitor"), _top(top) {
    _H = _top->no_of_nodes(); // number of hosts
    _N = _top->no_of_tors(); // number of racks
    _hpr = _top->no_of_hpr(); // number of hosts per rack
    _uplinks = _top->no_of_uplinks();
    uint64_t rate = 10000000000 / 8; // bytes / second
    rate = rate * _H;
    //rate = rate / 1500; // total packets per second

    _max_agg_Bps = rate;

    // debug:
    //cout << "max bytes per second = " << rate << endl;

}

void UtilMonitor::start(simtime_picosec period) {
    _period = period;
    _max_B_in_period = _max_agg_Bps * timeAsSec(_period);

    // debug:
    //cout << "_max_pkts_in_period = " << _max_pkts_in_period << endl;

    eventlist().sourceIsPending(*this, _period);
}

void UtilMonitor::doNextEvent() {
    printAggUtil();
}

void UtilMonitor::printAggUtil() {

    uint64_t B_sum = 0;

    /* Print ToR-to-ToR and ToR-to-Host data and header sent in last period; print avg. hop count as observed in pipes */
    std::stringstream  mystream, pipe_util_header, pipe_avg_hop_count;
    for (int tor = 0; tor < _N; tor++) {
        mystream << "T" << tor << " ";
        pipe_util_header << "HT" << tor << " ";
        pipe_avg_hop_count << "HC" << tor << " ";
        for (int downlink = 0; downlink < _hpr + _uplinks; downlink++) {
            Pipe *pipe = _top->get_pipe_tor(tor, downlink);
            uint64_t bytes = pipe->reportBytes();
            mystream << "Q" << downlink << "=" << bytes << " ";
            pipe_util_header << "Q" << downlink << "=" << pipe->reportHeaderBytes() << " ";
            pipe_avg_hop_count << "Q" << downlink << "=" << pipe->reportAvgHopCount() << " ";
            if (downlink >= _uplinks) B_sum = B_sum + bytes;
        }
        mystream << endl;
        pipe_util_header << endl;
        pipe_avg_hop_count << endl;
    }
    double util = (double) B_sum / (double) _max_B_in_period;
    cout << "Util " << fixed << util << " " << timeAsMs(eventlist().now()) << endl;
    cout << mystream.str();
    cout << pipe_util_header.str();
    cout << pipe_avg_hop_count.str();

    /* Print Host-to-ToR data and header sent in last period */
    std::stringstream  sh_pipe_data, sh_pipe_header;
    for (int node = 0; node < _top->no_of_nodes(); node++) {
        Pipe *pipe = _top->get_pipe_serv_tor(node);
        sh_pipe_data << "H" << node << "=" << pipe->reportBytes() << " ";
        sh_pipe_header << "H" << node << "=" << pipe->reportHeaderBytes() << " ";
    }

    cout << "SendingHostsPipeUsageData " << timeAsMs(eventlist().now()) << " " << sh_pipe_data.str() << endl;
    cout << "SendingHostsPipeUsageHeader " << timeAsMs(eventlist().now()) << " " << sh_pipe_header.str() << endl;

    /* Print retransmitted sent in last period */
    /*
    std::stringstream  mystream_rtx;
    for (int tor = 0; tor < _N; tor++) {
        mystream_rtx << "RT" << tor << " ";
        for (int downlink = 0; downlink < _hpr + _uplinks; downlink++) {
            Pipe *pipe = _top->get_pipe_tor(tor, downlink);
            uint64_t bytes = pipe->reportBytesRtx();
            mystream_rtx << "Q" << downlink << "=" << bytes << " ";
        }
        mystream_rtx << endl;
    }
    cout << "RTX Utilization" << endl;
    cout << mystream_rtx.str();
    */

    // Collect and dump queue level infos
    std::stringstream mystream_queues, queues_local, queues_nonlocal, queues_high, queues_avg_estimator;
    for (int tor = 0; tor < _N; tor++) {
        mystream_queues << "QT" << tor << " ";
        queues_high << "QHT" << tor << " ";
        queues_local << "QLT" << tor << " ";
        queues_nonlocal << "QNLT" << tor << " ";
        for (int downlink = 0; downlink < _hpr + _uplinks; downlink++) {
            auto *queue = dynamic_cast<CompositeQueue*>(_top->get_queue_tor(tor, downlink));
            uint64_t bytes = queue->queuesize();
            mystream_queues << "Q" << downlink << "=" << bytes << " ";
            queues_high << "Q" << downlink << "=" << queue->queuesize_high() << " ";
            queues_local << "Q" << downlink << "=" << queue->queuesize_local_traffic() << " ";
            queues_nonlocal << "Q" << downlink << "=" << queue->queuesize_nonlocal_traffic() << " ";

            auto enhanced_queue = dynamic_cast<EnhancedCompositeQueue*>(queue);
            if (enhanced_queue != nullptr) {
                queues_avg_estimator << "Q" << downlink << "=" << enhanced_queue->average_queue_length() << " ";
            } else {
                queues_avg_estimator << "Q" << downlink << "=-1";
            }

        }
        for (int downlink = 0; downlink < _hpr; downlink++) {
            Queue *queue = _top->get_queue_serv_tor(tor*_hpr+downlink);
            uint64_t bytes = queue->queuesize();
            mystream_queues << "H" << downlink << "=" << bytes << " ";
        }
        mystream_queues << endl;
        queues_local << endl;
        queues_nonlocal << endl;
        queues_high << endl;
        queues_avg_estimator << endl;
    }

    cout << "Queue-Occupancy" << timeAsMs(eventlist().now()) << endl;
    cout << mystream_queues.str();
    cout << "Queue-HighPrio-Occupancy" << timeAsMs(eventlist().now()) << endl;
    cout << queues_high.str();

    cout << "Queue-Local-Occupancy " << timeAsMs(eventlist().now()) << endl;
    cout << queues_local.str();
    cout << "Queue-Nonlocal-Occupancy " << timeAsMs(eventlist().now()) << endl;
    cout << queues_nonlocal.str();

    cout << "Queue-Avg-Estimation " << timeAsMs(eventlist().now()) << endl;
    cout << queues_avg_estimator.str();

    /* Print per rack statistics */
    std::stringstream mystream_queues_received_bytes, mystream_header_bytes;
    for (int tor = 0; tor < _N; tor++) {
        mem_b summed_bytes_at_dst = 0;
        mem_b summed_header_bytes_at_dst = 0;
        for (int downlink = _uplinks; downlink < _hpr + _uplinks; downlink++) {
            auto *queue = dynamic_cast<CompositeQueue *>(_top->get_queue_tor(tor, downlink));
            summed_bytes_at_dst += queue->report_bytes_at_dst();
            summed_header_bytes_at_dst += queue->report_header_bytes_at_dst();
        }
        mystream_queues_received_bytes << tor << "=" << summed_bytes_at_dst << " ";
        mystream_header_bytes << tor << "=" << summed_header_bytes_at_dst << " ";
    }
    cout << "DataBytesReceivedAtDstToR " << timeAsMs(eventlist().now()) << " " << mystream_queues_received_bytes.str() << endl;
    cout << "HeaderBytesReceivedAtDstToR " << timeAsMs(eventlist().now()) << " " << mystream_header_bytes.str() << endl;

    std::stringstream mystream_queues_to_host_dropped;
    for (int tor = 0; tor < _N; tor++) {
        uint num_dropped = 0;
        for (int downlink = _uplinks; downlink < _hpr + _uplinks; downlink++) {
            auto *queue = dynamic_cast<CompositeQueue *>(_top->get_queue_tor(tor, downlink));
            num_dropped += queue->bytes_stripped();
            queue->reset_stripped();
        }
        mystream_queues_to_host_dropped << tor << "=" << num_dropped << " ";
    }
    cout << "PktsToHostsDropped " << timeAsMs(eventlist().now()) << " " << mystream_queues_to_host_dropped.str() << endl;
    cout << "GlobalReceivedBytes " << timeAsMs(eventlist().now()) << " "
        << TcpSink::get_global_received_bytes()+NdpSink::get_global_received_bytes() << endl;

    if (eventlist().now() + _period < eventlist().getEndtime())
        eventlist().sourceIsPendingRel(*this, _period);

}
