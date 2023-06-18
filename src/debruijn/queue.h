// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#ifndef QUEUE_H
#define QUEUE_H

/*
 * A simple FIFO queue
 */

#include <list>
#include "config.h"
#include "eventlist.h"
#include "network.h"
#include "loggertypes.h"


class Queue : public EventSource, public PacketSink {
 public:

    Queue(linkspeed_bps bitrate, mem_b maxsize, EventList &eventlist, 
	  QueueLogger* logger);
    void receivePacket(Packet& pkt) override;
    void doNextEvent() override;

    bool sendFromQueue(Packet* pkt);  // Returns true if packet could NOT be fowarded to pipe (ie. if the DA link is up) and should be readded to the queue (first position)

    // should really be private, but loggers want to see
    mem_b _maxsize; 

    inline simtime_picosec drainTime(Packet *pkt) { 
        return (simtime_picosec)(pkt->size() * _ps_per_byte); 
    }
    inline mem_b serviceCapacity(simtime_picosec t) {
        return (mem_b)(timeAsSec(t) * (double)_bitrate); 
    }
    virtual mem_b queuesize();
    simtime_picosec serviceTime();
    int num_drops() const {return _num_drops;}
    void reset_drops() {_num_drops = 0;}

    virtual void setRemoteEndpoint(Queue* q) {_remoteEndpoint = q;};
    virtual void setRemoteEndpoint2(Queue* q) {_remoteEndpoint = q;q->setRemoteEndpoint(this);};
    Queue* getRemoteEndpoint() {return _remoteEndpoint;}

    void setName(const string& name) override {
        Logged::setName(name);
        _nodename += name;
    }
    virtual void setLogger(QueueLogger* logger) {
        _logger = logger;
    }
    const string& nodename() override { return _nodename; }
    bool is_priority_flow() override { return false; }

    virtual vector<int> get_destinations_with_pkts() { throw std::runtime_error("Not available"); }
    virtual vector<mem_b> &get_tokens() { return _tokens_per_destination; }
    virtual void reset_token_zero() { _all_tokens_zero = false; }

 protected:
    // Housekeeping
    Queue* _remoteEndpoint;

    QueueLogger* _logger;

    // Mechanism
    // start serving the item at the head of the queue
    virtual void beginService(); 

    // wrap up serving the item at the head of the queue
    virtual void completeService(); 

    linkspeed_bps _bitrate; 
    simtime_picosec _ps_per_byte;  // service time, in picoseconds per byte
    mem_b _queuesize;
    list<Packet*> _enqueued;
    int _num_drops;
    string _nodename;

    // Per-destination rate limiting
    vector<mem_b>  _tokens_per_destination;
    bool _all_tokens_zero;
};

/* implement a 4-level priority queue */
// modified to include RLB
class PriorityQueue : public Queue {
 public:
    typedef enum {Q_RLB=0, Q_LO=1, Q_MID=2, Q_HI=3, Q_NONE=4} queue_priority_t;
    PriorityQueue(DebruijnTopology* top, linkspeed_bps bitrate, mem_b maxsize, EventList &eventlist,
		  QueueLogger* logger, int node);
    void receivePacket(Packet& pkt) override;
    mem_b queuesize() override;
    simtime_picosec serviceTime(Packet& pkt);

    void doorbell(bool rlbwaiting);
    void trigger_begin_service();

    int _bytes_sent; // keep track so we know when to send a push to RLB module

    DebruijnTopology* _top;
    int _node;
    int _crt_slice = -1; // the first packet to be sent will cause a path update

    vector<int> get_destinations_with_pkts() override;

    static void set_high_prio_flowsize(mem_b size) { high_prio_flowsize = size; }

 protected:
    // start serving the item at the head of the queue
    void beginService() override;
    // wrap up serving the item at the head of the queue
    void completeService() override;

    PriorityQueue::queue_priority_t getPriority(Packet& pkt);
    list <Packet*> _queue[Q_NONE];
    mem_b _queuesize[Q_NONE];
    queue_priority_t _servicing;
    int _state_send;

    // Per-destination rate limiting
    vector<uint32_t> _num_pkts_per_destination;

    static mem_b high_prio_flowsize;
};

#endif
