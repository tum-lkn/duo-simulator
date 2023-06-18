// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#include "enhanced_compositequeue.h"
#include <cmath>

#include <iostream>

#include "debruijn_topology.h"
#include "rlbpacket.h" // added for debugging
#include "rlbmodule.h"
#include "tcppacket.h"
#include "tcp.h"

// !!! NOTE: this one fully separates NDP flows from flows with other transport protocols

// !!! NOTE: this has been modified to also include a mid priority queue for NDP data packets.

mem_b EnhancedCompositeQueue::mean_packet_size = 1500;
double EnhancedCompositeQueue::queue_weight = 0.002;
double EnhancedCompositeQueue::m_maxTh_pkts = 40;
double EnhancedCompositeQueue::m_minTh_pkts = 10;


EnhancedCompositeQueue::EnhancedCompositeQueue(linkspeed_bps bitrate, mem_b maxsize, EventList& eventlist, 
			       QueueLogger* logger, int tor, int port, uint32_t num_tors)
  : CompositeQueue(bitrate, maxsize, eventlist, logger, tor, port, num_tors)
{
  _ratio_mid = 4500; // bytes (3 1500B packets)

  _queuesize_mid = 0;
  _serv = QUEUE_ENC_INVALID;

  _queuesize_mid_per_destination.resize(num_tors);
  _queuesize_mid_by_origin.resize(2);

  m_ptc = bitrate / (mean_packet_size * 8);

  double th_diff = (m_maxTh_pkts - m_minTh_pkts);
  if (th_diff == 0) {
      th_diff = 1.0;
  }
  m_vA = 1.0 / th_diff;
  m_vB = - m_minTh_pkts / th_diff;

}

void EnhancedCompositeQueue::beginService(){
	if (!_enqueued_high.empty() && !_enqueued_mid.empty() && !_enqueued_low.empty()){

		if (_crt >= (_ratio_high+_ratio_mid+_ratio_low))
			_crt = 0;

		if (_crt< _ratio_high) {
            _serv = QUEUE_ENC_HIGH;
            eventlist().sourceIsPendingRel(*this, drainTime(_enqueued_high.back()));
            _crt = _crt + 64; // !!! hardcoded header size for now...

            // debug:
            //if (_tor == 0 && _port == 6)
            //	cout << "composite_queue sending a header (full packets in queue)" << endl;
        } else if (_crt < _ratio_mid) {
            _serv = QUEUE_ENC_MID;
            eventlist().sourceIsPendingRel(*this, drainTime(_enqueued_mid.back()));
            _crt = _crt + _enqueued_mid.back()->size();
		} else {
			assert(_crt < _ratio_high+_ratio_mid+_ratio_low);
			_serv = QUEUE_ENC_LOW;
			eventlist().sourceIsPendingRel(*this, drainTime(_enqueued_low.back()));
			int sz = _enqueued_low.back()->size();
			_crt = _crt + sz;
		}
		return;
	}

	if (!_enqueued_high.empty()) {
        _serv = QUEUE_ENC_HIGH;
        eventlist().sourceIsPendingRel(*this, drainTime(_enqueued_high.back()));

        // debug:
        //if (_tor == 0 && _port == 6)
        //	cout << "composite_queue sending a header (no packets in queue)" << endl;}
    } else if (!_enqueued_mid.empty()) {
            _serv = QUEUE_ENC_MID;
            eventlist().sourceIsPendingRel(*this, drainTime(_enqueued_mid.back()));

            // debug:
            //if (_tor == 0 && _port == 6)
            //	cout << "composite_queue sending a header (no packets in queue)" << endl;
	} else if (!_enqueued_low.empty()) {
		_serv = QUEUE_ENC_LOW;
		eventlist().sourceIsPendingRel(*this, drainTime(_enqueued_low.back()));

		// debug:
		//if (_tor == 0 && _port == 6)
		//	cout << "composite_queue sending a full packet: " << (_enqueued_low.back())->size() << " bytes (no headers in queue)" << endl;

	} else if (!_enqueued_rlb.empty()) {
		_serv = QUEUE_ENC_RLB;
		eventlist().sourceIsPendingRel(*this, drainTime(_enqueued_rlb.back()));
	} else {
		assert(0);
		_serv = QUEUE_ENC_INVALID;
	}
}

void EnhancedCompositeQueue::completeService() {
	Packet* pkt;
	bool sendingpkt = true;
    bool pipe_is_down;
    
    if (_serv == QUEUE_ENC_LOW) {
        assert(!_enqueued_low.empty());
        pkt = _enqueued_low.back();
        _enqueued_low.pop_back();
        _queuesize_low -= pkt->size();
        _queuesize_low_per_destination[pkt->get_dst_ToR()] -= pkt->size();

        if (pkt->get_src_ToR() == pkt->get_crtToR()) {
            _queuesize_low_by_origin[0] -= pkt->size();
        } else {
            _queuesize_low_by_origin[1] -= pkt->size();
        }

        pipe_is_down = sendFromQueue(pkt);
        if (pipe_is_down) {
            _enqueued_low.push_back(pkt);
            _queuesize_low += pkt->size();
            _queuesize_low_per_destination[pkt->get_dst_ToR()] += pkt->size();

            if (pkt->get_src_ToR() == pkt->get_crtToR()) {
                _queuesize_low_by_origin[0] += pkt->size();
            } else {
                _queuesize_low_by_origin[1] += pkt->size();
            }

        } else {
            _num_packets++;
        }
    } else if (_serv == QUEUE_ENC_MID) {
        assert(!_enqueued_mid.empty());
        pkt = _enqueued_mid.back();
        _enqueued_mid.pop_back();
        _queuesize_mid -= pkt->size();
        _queuesize_mid_per_destination[pkt->get_dst_ToR()] -= pkt->size();

        if (pkt->get_src_ToR() == pkt->get_crtToR()) {
            _queuesize_mid_by_origin[0] -= pkt->size();
        } else {
            _queuesize_mid_by_origin[1] -= pkt->size();
        }

        pipe_is_down = sendFromQueue(pkt);
        if (pipe_is_down) {
            _enqueued_mid.push_back(pkt);
            _queuesize_mid += pkt->size();
            _queuesize_mid_per_destination[pkt->get_dst_ToR()] += pkt->size();

            if (pkt->get_src_ToR() == pkt->get_crtToR()) {
                _queuesize_mid_by_origin[0] += pkt->size();
            } else {
                _queuesize_mid_by_origin[1] += pkt->size();
            }

        } else {
            _num_packets++;
        }
	} else if (_serv == QUEUE_ENC_HIGH) {
		assert(!_enqueued_high.empty());
		pkt = _enqueued_high.back();
		_enqueued_high.pop_back();
		_queuesize_high -= pkt->size();
        _queuesize_high_per_destination[pkt->get_dst_ToR()] -= pkt->size();

        if (pkt->get_src_ToR() == pkt->get_crtToR()) {
            _queuesize_high_by_origin[0] -= pkt->size();
        } else {
            _queuesize_high_by_origin[1] -= pkt->size();
        }

        pipe_is_down = sendFromQueue(pkt);
        if (pipe_is_down) {
            _enqueued_high.push_back(pkt);
            _queuesize_high += pkt->size();
            _queuesize_high_per_destination[pkt->get_dst_ToR()] += pkt->size();

            if (pkt->get_src_ToR() == pkt->get_crtToR()) {
                _queuesize_high_by_origin[0] += pkt->size();
            } else {
                _queuesize_high_by_origin[1] += pkt->size();
            }
        } else {
            if (pkt->type() == NDPACK)
                _num_acks++;
            else if (pkt->type() == NDPNACK)
                _num_nacks++;
            else if (pkt->type() == NDPPULL)
                _num_pulls++;
            else {
                _num_headers++;
            }
        }
  	} else {
    	assert(0);
  	}
    
  	_serv = QUEUE_ENC_INVALID;
    if (!pipe_is_down && (!_enqueued_high.empty() || !_enqueued_low.empty() || !_enqueued_mid.empty() || !_enqueued_rlb.empty())){
        m_idle = 0;
        beginService();
    } else {
        m_idle = 1;
        m_idleTime = eventlist().now();
    }


}

void EnhancedCompositeQueue::doNextEvent() {
	completeService();
}

void EnhancedCompositeQueue::receivePacket(Packet& pkt) {
    // debug:
	//if (pkt.been_bounced() == true && pkt.bounced() == false) {
	//	cout << "ToR " << _tor << " received a previously bounced packet" << endl;
	//	cout << "    src = " << pkt.get_src() << endl;
	//} else if (pkt.bounced() == true) {
	//	cout << "ToR " << _tor << " received a currently bounced packet" << endl;
	//	cout << "    src = " << pkt.get_src() << endl;
	//}
    
    update_avg_queue_length(pkt);
    /*
    if (pkt.size() > 64) {
        if (check_red_dropping(pkt)) {
            return;
        }
    }*/

	switch (pkt.type()) {
        case RLB:
        {
            _enqueued_rlb.push_front(&pkt);
            _queuesize_rlb += pkt.size();
            break;
        }
        case NDP:
        case NDPACK:
        case NDPNACK:
        case NDPPULL: {
            if (pkt.get_crtToR() == pkt.get_dst_ToR()) {
                if (pkt.size() > 64) {  // Not a header
                    data_bytes_received_at_dst_tor += pkt.size();
                } else {
                    header_bytes_received_at_dst_tor += pkt.size();
                }
            }

            if (!pkt.header_only()){
                if (_queuesize_mid + pkt.size() <= _maxsize || drand()<0.5) {
                    //regular packet; don't drop the arriving packet

                    // we are here because either the queue isn't full or,
                    // it might be full and we randomly chose an
                    // enqueued packet to trim
                    bool chk = true;

                    if (_queuesize_mid + pkt.size() > _maxsize) {
                        // we're going to drop an existing packet from the queue

                        if (_enqueued_mid.empty()){
                            assert(0);
                        }
                        //take last packet from mid prio queue, make it a header and place it in the high prio queue
                        Packet* booted_pkt = _enqueued_mid.front();

                        // added a check to make sure that the booted packet makes enough space in the queue
                        // for the incoming packet
                        if (booted_pkt->size() >= pkt.size()) {
                            chk = true;

                            _enqueued_mid.pop_front();
                            _queuesize_mid -= booted_pkt->size();
                            _queuesize_mid_per_destination[booted_pkt->get_dst_ToR()] -= booted_pkt->size();

                            if (booted_pkt->get_src_ToR() == booted_pkt->get_crtToR()) {
                                _queuesize_mid_by_origin[0] += booted_pkt->size();
                            } else {
                                _queuesize_mid_by_origin[1] += booted_pkt->size();
                            }

                            _bytes_stripped += booted_pkt->size() - 64;

                            booted_pkt->strip_payload();
                            _num_stripped++;

                            booted_pkt->flow().logTraffic(*booted_pkt,*this,TrafficLogger::PKT_TRIM);
                            if (_logger)
                                _logger->logQueue(*this, QueueLogger::PKT_TRIM, pkt);

                            if (_queuesize_high + booted_pkt->size() > _maxsize) {
                                // debug:
                                //cout << "!!! NDP - header queue overflow <booted> ..." << endl;

                                if (!booted_pkt->bounced()) {
                                    //return the packet to the sender
                                    DebruijnTopology* top = booted_pkt->get_topology();
                                    booted_pkt->bounce(); // indicate that the packet has been bounced
                                    _num_bounced++;

                                    // flip the source and dst of the packet:
                                    int s = booted_pkt->get_src();
                                    int d = booted_pkt->get_dst();
                                    booted_pkt->set_src(d);
                                    booted_pkt->set_dst(s);

                                    // get the current ToR, this will be the new src_ToR of the packet
                                    int new_src_ToR = booted_pkt->get_crtToR();

                                    if (new_src_ToR == booted_pkt->get_src_ToR()) {
                                        // the packet got returned at the source ToR
                                        // we need to send on a downlink right away
                                        booted_pkt->set_src_ToR(new_src_ToR);
                                        booted_pkt->set_crtport(top->get_port_to_server(booted_pkt->get_dst()));
                                        booted_pkt->set_maxhops(0);
                                    } else {
                                        booted_pkt->set_src_ToR(new_src_ToR);
                                        booted_pkt->set_crtport(
                                                top->get_port(booted_pkt->get_crtToR(),
                                                              booted_pkt->get_dst()));
                                    }

                                    booted_pkt->set_crthop(0);
                                    booted_pkt->set_crtToR(new_src_ToR);
                                    // debug:
                                    //cout << "   packet RTSed to node " << booted_pkt->get_dst() << " at ToR = " << new_src_ToR << endl;

                                    Queue* nextqueue = top->get_queue_tor(booted_pkt->get_crtToR(), booted_pkt->get_crtport());
                                    nextqueue->receivePacket(*booted_pkt);


                                } else {
                                    // debug:
                                    cout << "   ... this is an RTS packet. Dropped." << endl;
                                    booted_pkt->free();
                                }
                            } else {
                                _enqueued_high.push_front(booted_pkt);
                                _queuesize_high += booted_pkt->size();
                                _queuesize_high_per_destination[booted_pkt->get_dst_ToR()] -= booted_pkt->size();

                                if (booted_pkt->get_src_ToR() == booted_pkt->get_crtToR()) {
                                    _queuesize_high_by_origin[0] -= booted_pkt->size();
                                } else {
                                    _queuesize_high_by_origin[1] -= booted_pkt->size();
                                }
                            }
                        } else {
                            chk = false;
                        }
                    }

                    if (chk) {
                        // the new packet fit
                        assert(_queuesize_mid + pkt.size() <= _maxsize);
                        _enqueued_mid.push_front(&pkt);
                        _queuesize_mid += pkt.size();
                        _queuesize_mid_per_destination[pkt.get_dst_ToR()] += pkt.size();

                        if (pkt.get_src_ToR() == pkt.get_crtToR()) {
                            _queuesize_mid_by_origin[0] += pkt.size();
                        } else {
                            _queuesize_mid_by_origin[1] += pkt.size();
                        }

                        if (_serv==QUEUE_ENC_INVALID) {
                            beginService();
                        }
                        return;
                    } else {
                        _bytes_stripped += pkt.size() - 64;
                        // the packet wouldn't fit if we booted the existing packet
                        pkt.strip_payload();
                        _num_stripped++;
                    }
                } else {
                    _bytes_stripped += pkt.size() - 64;
                    //strip payload on the arriving packet - low priority queue is full
                    pkt.strip_payload();
                    _num_stripped++;
                }
            }

            assert(pkt.header_only());

            if (_queuesize_high + pkt.size() > _maxsize){
                if (!pkt.bounced()) {
                    //return the packet to the sender
                    DebruijnTopology* top = pkt.get_topology();
                    pkt.bounce();
                    _num_bounced++;

                    // flip the source and dst of the packet:
                    int s = pkt.get_src();
                    int d = pkt.get_dst();
                    pkt.set_src(d);
                    pkt.set_dst(s);

                    // get the current ToR, this will be the new src_ToR of the packet
                    int new_src_ToR = pkt.get_crtToR();

                    if (new_src_ToR == pkt.get_src_ToR()) {
                        // the packet got returned at the source ToR
                        // we need to send on a downlink right away
                        pkt.set_src_ToR(new_src_ToR);
                        pkt.set_crtport(top->get_port_to_server(pkt.get_dst()));
                        pkt.set_maxhops(0);
                    } else {
                        pkt.set_src_ToR(new_src_ToR);

                        pkt.set_crtport(top->get_port(pkt.get_crtToR(), top->get_firstToR(pkt.get_dst()))); // current hop = 0 (hardcoded)

                    }

                    // debug:
                    //cout << "   packet RTSed to node " << pkt.get_dst() << " at ToR = " << new_src_ToR << endl;

                    pkt.set_crthop(0);
                    pkt.set_crtToR(new_src_ToR);

                    Queue* nextqueue = top->get_queue_tor(pkt.get_crtToR(), pkt.get_crtport());
                    nextqueue->receivePacket(pkt);

                    return;
                } else {
                    // debug:
                    cout << "   ... this is an RTS packet. Dropped.\n";
                    pkt.free();
                    _num_drops++;
                    return;
                }
            }

            _enqueued_high.push_front(&pkt);
            _queuesize_high += pkt.size();
            _queuesize_high_per_destination[pkt.get_dst_ToR()] += pkt.size();

            if (pkt.get_src_ToR() == pkt.get_crtToR()) {
                _queuesize_high_by_origin[0] += pkt.size();
            } else {
                _queuesize_high_by_origin[1] += pkt.size();
            }
            /*if (pkt.size() > 64) {  // Not a header
                data_bytes_received_at_dst_tor += pkt.size();
            } else {
                header_bytes_received_at_dst_tor += pkt.size();
            }*/

            break;
        }
        case TCP: {
            if (pkt.get_crtToR() == pkt.get_dst_ToR()) {
                data_bytes_received_at_dst_tor += pkt.size();
            }

            auto tcp_pkt = dynamic_cast<TcpPacket*>(&pkt);
            if (_queuesize_low + pkt.size() > _maxsize) {
                // debug:
                // cout << "   Queue full. Dropped.\n";
                //if (_logger) _logger->logQueue(*this, QueueLogger::PKT_DROP, pkt);
                //pkt.flow().logTraffic(pkt,*this,TrafficLogger::PKT_DROP);
                dynamic_cast<TcpPacket *>(&pkt)->track_drop();
                pkt.free();
                _num_drops++;
                return;
            }
            // the new packet fit
            assert(_queuesize_low + pkt.size() <= _maxsize);
            _enqueued_low.push_front(&pkt);
            _queuesize_low += pkt.size();
            _queuesize_low_per_destination[pkt.get_dst_ToR()] += pkt.size();

            if (pkt.get_src_ToR() == pkt.get_crtToR()) {
                _queuesize_low_by_origin[0] += pkt.size();
            } else {
                _queuesize_low_by_origin[1] += pkt.size();
            }
            break;
            
        }
        case TCPACK: {
            if (pkt.get_crtToR() == pkt.get_dst_ToR()) {
                header_bytes_received_at_dst_tor += pkt.size();
            }
            auto tcp_pkt = dynamic_cast<TcpAck*>(&pkt);
            if (_queuesize_low + pkt.size() > _maxsize) {
                // debug:
                // cout << "   Queue full. Dropped.\n";
                //if (_logger) _logger->logQueue(*this, QueueLogger::PKT_DROP, pkt);
                //pkt.flow().logTraffic(pkt,*this,TrafficLogger::PKT_DROP);
                // dynamic_cast<TcpAck *>(&pkt)->track_drop();
                pkt.free();
                _num_drops++;
                return;
            }
            // the new packet fit
            assert(_queuesize_low + pkt.size() <= _maxsize);
            _enqueued_low.push_front(&pkt);
            _queuesize_low += pkt.size();
            _queuesize_low_per_destination[pkt.get_dst_ToR()] += pkt.size();

            if (pkt.get_src_ToR() == pkt.get_crtToR()) {
                _queuesize_low_by_origin[0] += pkt.size();
            } else {
                _queuesize_low_by_origin[1] += pkt.size();
            }
            break;
        }
    }
    
    if (_serv == QUEUE_ENC_INVALID) {
		beginService();
    }
}

mem_b EnhancedCompositeQueue::queuesize() {
    return _queuesize_low + _queuesize_high;
}

mem_b EnhancedCompositeQueue::queuesize_high() {
    return _queuesize_high + _queuesize_mid;
}

void EnhancedCompositeQueue::clear_queue() {
    _serv = QUEUE_ENC_INVALID;
    eventlist().cancelPendingSource(*this);
    if (!_enqueued_low.empty()) {
        _enqueued_low.clear();
        _queuesize_low = 0;

        _queuesize_low_per_destination.assign(_queuesize_low_per_destination.size(), 0);
        _queuesize_low_by_origin.assign(2, 0);
    }
    if (!_enqueued_mid.empty()) {
        _enqueued_mid.clear();
        _queuesize_mid = 0;

        _queuesize_mid_per_destination.assign(_queuesize_mid_per_destination.size(), 0);
        _queuesize_mid_by_origin.assign(2, 0);
    }
    
    if (!_enqueued_high.empty()) {
        _enqueued_high.clear();
        _queuesize_high = 0;
        _queuesize_high_per_destination.assign(_queuesize_high_per_destination.size(), 0);
        _queuesize_high_by_origin.assign(2, 0);
    }
}


mem_b EnhancedCompositeQueue::queuesize_per_destination(uint32_t tor_id) {
    return _queuesize_low_per_destination[tor_id] + _queuesize_mid_per_destination[tor_id] + 
        _queuesize_high_per_destination[tor_id];
}

mem_b EnhancedCompositeQueue::queuesize_local_traffic() {
    return _queuesize_low_by_origin[0] + _queuesize_mid_by_origin[0] + _queuesize_high_by_origin[0];
}

mem_b EnhancedCompositeQueue::queuesize_nonlocal_traffic() {
    return _queuesize_low_by_origin[1] + _queuesize_mid_by_origin[1] + _queuesize_high_by_origin[1];
}

void EnhancedCompositeQueue::update_avg_queue_length(Packet &pkt) {
    uint32_t num_queued = queuesize() / mean_packet_size;

    // simulate number of packets arrival during idle period
    uint32_t m = 0;

    if (m_idle == 1) {
        simtime_picosec now = eventlist().now();
        m = uint32_t(m_ptc * timeAsSec(now - m_idleTime));
        m_idle = 0;
    }

    m_qAvg = m_qAvg * std::pow(1.0 - queue_weight, m + 1);
    m_qAvg += queue_weight * num_queued;
}


bool EnhancedCompositeQueue::check_red_dropping(Packet &pkt) {
    if (m_maxTh_pkts == 0) {
        // RED is deactivated
        return false;
    }

    uint32_t num_queued = queuesize() / mean_packet_size;

    m_count++;
    m_countBytes += pkt.size();

    /* Partially taken from https://www.nsnam.org/doxygen/red-queue-disc_8cc_source.html */
    if (m_qAvg >= m_minTh_pkts && num_queued > 1) {
        if (m_qAvg >= m_maxTh_pkts) {
            pkt.free();
            _num_drops++;
            return true;
        } else if (m_old == 0) {
            /*
             * The average queue size has just crossed the
             * threshold from below to above m_minTh, or
             * from above m_minTh with an empty queue to
             * above m_minTh with a nonempty queue.
             */
            m_count = 1;
            m_countBytes = pkt.size();
            m_old = 1;
        } else if (DropEarly(pkt, num_queued)) {
            pkt.free();
            _num_drops++;
            return true;
        }
    } else {
        // No packets are being dropped
        m_old = 0;
    }
    return false;
}

// Check if packet p needs to be dropped due to probability mark
uint32_t EnhancedCompositeQueue::DropEarly(Packet &pkt, uint32_t qSize) {
    double prob1 = CalculatePNew();
    m_vProb = ModifyP(prob1, pkt.size());

    double u = rand()/RAND_MAX;

    if (u <= m_vProb) {
        // DROP or MARK
        m_count = 0;
        m_countBytes = 0;
        return 1; // drop
    }

    return 0; // no drop/mark
}

// Returns a probability using these function parameters for the DropEarly function
double EnhancedCompositeQueue::CalculatePNew() {
    double p;

    if (m_qAvg >= m_maxTh_pkts) {
        /*
         * OLD: p continues to range linearly above m_curMaxP as
         * the average queue size ranges above m_maxTh.
         * NEW: p is set to 1.0
         */
        p = 1.0;
    } else {
        /*
         * p ranges from 0 to m_curMaxP as the average queue size ranges from
         * m_minTh to m_maxTh
         */
        p = m_vA * m_qAvg + m_vB;
    }

    if (p > 1.0) {
        p = 1.0;
    }
    return p;
}


// Returns a probability using these function parameters for the DropEarly function
double EnhancedCompositeQueue::ModifyP(double p, uint32_t size) {
    auto count1 = (double)m_count;
    if (count1 * p < 1.0) {
        p /= (1.0 - count1 * p);
    } else {
        p = 1.0;
    }

    if (p > 1.0) {
        p = 1.0;
    }
    return p;
}
