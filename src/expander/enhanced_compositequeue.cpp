// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#include "enhanced_compositequeue.h"
#include <cmath>

#include <iostream>

#include "tcppacket.h"
#include "tcp.h"

// !!! NOTE: this one fully separates NDP flows from flows with other transport protocols

// !!! NOTE: this has been modified to also include a mid priority queue for NDP data packets.

mem_b EnhancedCompositeQueue::mean_packet_size = 1500;
double EnhancedCompositeQueue::queue_weight = 0.002;
double EnhancedCompositeQueue::m_maxTh_pkts = 40;
double EnhancedCompositeQueue::m_minTh_pkts = 10;


EnhancedCompositeQueue::EnhancedCompositeQueue(linkspeed_bps bitrate, mem_b maxsize, EventList& eventlist, 
			       QueueLogger* logger)
  : CompositeQueue(bitrate, maxsize, eventlist, logger)
{
  _ratio_mid = 4500; // bytes (3 1500B packets)

  _queuesize_mid = 0;
  _serv = QUEUE_ENC_INVALID;

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

	} else {
		assert(0);
		_serv = QUEUE_ENC_INVALID;
	}
}

void EnhancedCompositeQueue::completeService() {
	Packet* pkt;
    
    if (_serv == QUEUE_ENC_LOW) {
        assert(!_enqueued_low.empty());
        pkt = _enqueued_low.back();
        _enqueued_low.pop_back();
        _queuesize_low -= pkt->size();
        _num_packets++;
    } else if (_serv == QUEUE_ENC_MID) {
        assert(!_enqueued_mid.empty());
        pkt = _enqueued_mid.back();
        _enqueued_mid.pop_back();
        _queuesize_mid -= pkt->size();

        if (pkt->type() == NDPACK)
            _num_acks++;
        else if (pkt->type() == NDPNACK)
            _num_nacks++;
        else if (pkt->type() == NDPPULL)
            _num_pulls++;
        else {
            //cout << "Hdr: type=" << pkt->type() << endl;
            _num_headers++;
        }
	} else if (_serv == QUEUE_ENC_HIGH) {
        assert(!_enqueued_high.empty());
        pkt = _enqueued_high.back();
        _enqueued_high.pop_back();
        _queuesize_high -= pkt->size();
        if (pkt->type() == NDPACK)
            _num_acks++;
        else if (pkt->type() == NDPNACK)
            _num_nacks++;
        else if (pkt->type() == NDPPULL)
            _num_pulls++;
        else {
            //cout << "Hdr: type=" << pkt->type() << endl;
            _num_headers++;
        }
  	} else {
    	assert(0);
  	}

    pkt->flow().logTraffic(*pkt, *this, TrafficLogger::PKT_DEPART);
    if (_logger) _logger->logQueue(*this, QueueLogger::PKT_SERVICE, *pkt);
    pkt->sendOn();
    
  	_serv = QUEUE_ENC_INVALID;
    if (!_enqueued_high.empty() || !_enqueued_low.empty() || !_enqueued_mid.empty()){
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
    update_avg_queue_length(pkt);
    /*if (pkt.size() > 64) {
        if (check_red_dropping(pkt)) {
            return;
        }
    }*/

	switch (pkt.type()) {
        case NDP:
        case NDPACK:
        case NDPNACK:
        case NDPPULL: {
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

                            booted_pkt->strip_payload();
                            _num_stripped++;

                            booted_pkt->flow().logTraffic(*booted_pkt,*this,TrafficLogger::PKT_TRIM);
                            if (_logger)
                                _logger->logQueue(*this, QueueLogger::PKT_TRIM, pkt);

                            if (_queuesize_high + booted_pkt->size() > _maxsize) {
                                if (booted_pkt->reverse_route() && booted_pkt->bounced() == false) {
                                    //return the packet to the sender
                                    if (_logger) _logger->logQueue(*this, QueueLogger::PKT_BOUNCE, *booted_pkt);
                                    booted_pkt->flow().logTraffic(pkt, *this, TrafficLogger::PKT_BOUNCE);
                                    //XXX what to do with it now?

#if 0
                                    printf("Bounce2 at %s\n", _nodename.c_str());
                                    printf("Fwd route:\n");
                                    print_route(*(booted_pkt->route()));
                                    printf("nexthop: %d\n", booted_pkt->nexthop());
#endif

                                    booted_pkt->bounce();

#if 0
                                    printf("\nRev route:\n");
                                    print_route(*(booted_pkt->reverse_route()));
                                    printf("nexthop: %d\n", booted_pkt->nexthop());
#endif
                                    _num_bounced++;
                                    booted_pkt->sendOn();

                                    // debug:
                                    //cout << "RTS" << endl;

                                } else {
                                    cout << "Dropped\n";
                                    booted_pkt->flow().logTraffic(*booted_pkt, *this, TrafficLogger::PKT_DROP);
                                    booted_pkt->free();
                                    if (_logger) _logger->logQueue(*this, QueueLogger::PKT_DROP, pkt);
                                }
                            } else {
                                _enqueued_high.push_front(booted_pkt);
                                _queuesize_high += booted_pkt->size();
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
                        if (_serv==QUEUE_ENC_INVALID) {
                            beginService();
                        }
                        return;
                    } else {
                        // the packet wouldn't fit if we booted the existing packet
                        pkt.strip_payload();
                        _num_stripped++;
                    }
                } else {
                    //strip payload on the arriving packet - low priority queue is full
                    pkt.strip_payload();
                    _num_stripped++;
                }
            }

            assert(pkt.header_only());

            if (_queuesize_high + pkt.size() > _maxsize){
                //drop header
                //cout << "drop!\n";
                if (pkt.reverse_route() && pkt.bounced() == false) {
                    //return the packet to the sender
                    if (_logger) _logger->logQueue(*this, QueueLogger::PKT_BOUNCE, pkt);
                    pkt.flow().logTraffic(pkt, *this, TrafficLogger::PKT_BOUNCE);
                    //XXX what to do with it now?

#if 0
                    printf("Bounce1 at %s\n", _nodename.c_str());
                    printf("Fwd route:\n");
                    print_route(*(pkt.route()));
                    printf("nexthop: %d\n", pkt.nexthop());
#endif

                    pkt.bounce();

#if 0
                    printf("\nRev route:\n");
                    print_route(*(pkt.reverse_route()));
                    printf("nexthop: %d\n", pkt.nexthop());
#endif

                    _num_bounced++;
                    pkt.sendOn();

                    // debug:
                    //cout << "RTS" << endl;

                    return;
                } else {
                    if (_logger) _logger->logQueue(*this, QueueLogger::PKT_DROP, pkt);
                    pkt.flow().logTraffic(pkt, *this, TrafficLogger::PKT_DROP);
                    cout << "B[ " << _enqueued_low.size() << " " << _enqueued_high.size() << " ] DROP "
                         << pkt.flow().id << endl;
                    pkt.free();
                    _num_drops++;
                    return;
                }
            }

            _enqueued_high.push_front(&pkt);
            _queuesize_high += pkt.size();
            /*if (pkt.size() > 64) {  // Not a header
                data_bytes_received_at_dst_tor += pkt.size();
            } else {
                header_bytes_received_at_dst_tor += pkt.size();
            }*/

            break;
        }
        case TCP: {
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
            break;
            
        }
        case TCPACK: {
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
