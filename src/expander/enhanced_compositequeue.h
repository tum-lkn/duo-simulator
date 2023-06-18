// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#ifndef ENHANCED_COMPOSITE_QUEUE_H
#define ENHANCED_COMPOSITE_QUEUE_H

/*
 * A composite queue that transforms packets into headers when there is no space and services headers with priority. 
 */

// !!! NOTE: this separates NDP flows from flows with other transport protocols.

// !!! NOTE: this has been modified to also include a  mid priority queue for NDP data packets


#define QUEUE_ENC_INVALID 0
#define QUEUE_ENC_LOW 2
#define QUEUE_ENC_MID 3 // new compared to CompositeQueue
#define QUEUE_ENC_HIGH 4


#include <list>
#include "queue.h"
#include "config.h"
#include "eventlist.h"
#include "network.h"
#include "loggertypes.h"
#include "compositequeue.h"

class EnhancedCompositeQueue : public CompositeQueue {
 public:
    EnhancedCompositeQueue(linkspeed_bps bitrate, mem_b maxsize,
		   EventList &eventlist, QueueLogger* logger);
    void receivePacket(Packet& pkt) override;
    void doNextEvent() override;

    // should really be private, but loggers want to see
    mem_b _queuesize_mid;

    mem_b queuesize() override;

    void update_avg_queue_length(Packet &pkt);
    uint32_t DropEarly(Packet &pkt, uint32_t qSize);
    double CalculatePNew();
    double ModifyP(double p, uint32_t size);

    double average_queue_length() { return m_qAvg; }
    bool check_red_dropping(Packet &pkt);

    void set_mean_packet_size(mem_b packet_size) { mean_packet_size = packet_size; }
    void set_queue_weight(double qw) { queue_weight = qw; }
    void set_min_th_pkt(double qw) { m_minTh_pkts = qw; }
    void set_max_th_pkt(double qw) { m_maxTh_pkts = qw; }

    static mem_b mean_packet_size;
    static double queue_weight;

 protected:
    // Mechanism
    void beginService() override; // start serving the item at the head of the queue
    void completeService() override; // wrap up serving the item at the head of the queue

    int _ratio_mid;

    list<Packet*> _enqueued_mid;

    // Per-destination queue size
    vector<mem_b> _queuesize_mid_per_destination, _queuesize_mid_by_origin;

    // Avg. queue length estimation for RED like functions
    static double m_minTh_pkts;
    static double m_maxTh_pkts;

    // ** Variables maintained by RED
    double m_vA;
    double m_vB;
    double m_vProb;
    uint32_t m_countBytes;
    uint32_t m_old;
    uint32_t m_idle;
    double m_ptc;
    double m_qAvg;
    uint32_t m_count;
    simtime_picosec m_idleTime;
};

#endif  // ENHANCED_COMPOSITE_QUEUE_H
