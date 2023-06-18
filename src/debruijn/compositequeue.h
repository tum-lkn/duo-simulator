// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#ifndef COMPOSITE_QUEUE_H
#define COMPOSITE_QUEUE_H

/*
 * A composite queue that transforms packets into headers when there is no space and services headers with priority. 
 */

// !!! NOTE: this one does selective RLB packet dropping.

// !!! NOTE: this has been modified to also include a lower priority RLB queue


#define QUEUE_INVALID 0
#define QUEUE_RLB 1 // modified
#define QUEUE_LOW 2
#define QUEUE_HIGH 3


#include <list>
#include "queue.h"
#include "config.h"
#include "eventlist.h"
#include "network.h"
#include "loggertypes.h"

class CompositeQueue : public Queue {
 public:
    CompositeQueue(linkspeed_bps bitrate, mem_b maxsize, 
		   EventList &eventlist, QueueLogger* logger, int tor, int port, uint32_t num_tors);
    void receivePacket(Packet& pkt) override;
    void doNextEvent() override;

    // should really be private, but loggers want to see
    mem_b _queuesize_rlb, _queuesize_low, _queuesize_high;
    int num_headers() const { return _num_headers;}
    int num_packets() const { return _num_packets;}
    int num_stripped() const { return _num_stripped;}
    mem_b bytes_stripped() const { return _bytes_stripped;}
    int num_bounced() const { return _num_bounced;}
    int num_acks() const { return _num_acks;}
    int num_nacks() const { return _num_nacks;}
    int num_pulls() const { return _num_pulls;}

    mem_b queuesize() override;
    virtual mem_b queuesize_high();
    virtual mem_b queuesize_low();
    virtual mem_b queuesize_per_destination(uint32_t tor_id);
    virtual mem_b queuesize_local_traffic();        // Additional statistics for debugging: Size in queue that originated in this racks
    virtual mem_b queuesize_nonlocal_traffic();     // Additional statistics for debugging: Size in queue that originated in other racks

    void reset_stripped() { _num_stripped = 0; _bytes_stripped = 0; }
    virtual void strip_packets_after_da_disconnect(); // After disconnect, we update the routing and this port is (most likely)
    // wrong for the packets that are already in the queue. We strip them to save BW for data packets that this DA link is for.

    virtual void clear_queue();

    void setName(const string& name) override {
        Logged::setName(name);
        _nodename += name;
    }
    virtual const string& nodename() { return _nodename; }

    static void set_high_prio_flowsize(mem_b size) { high_prio_flowsize = size; }

    int _tor; // the ToR switch this queue belongs to
    int _port; // the port this queue belongs to

    int _num_packets;
    int _num_headers; // only includes data packets stripped to headers, not acks or nacks
    int _num_acks;
    int _num_nacks;
    int _num_pulls;
    int _num_stripped; // count of packets we stripped
    mem_b _bytes_stripped; // bytes that we stripped
    int _num_bounced;  // count of packets we bounced

    void notify_connect_da_link();  // Used to trigger sending after we set up an DA link. Previous disconnect might have stopped transmission here

    mem_b report_bytes_at_dst();
    mem_b report_header_bytes_at_dst();

    static mem_b high_prio_flowsize;
 protected:
    // Mechanism
    void beginService() override; // start serving the item at the head of the queue
    void completeService() override; // wrap up serving the item at the head of the queue

    int _serv;
    int _ratio_high, _ratio_low, _crt;

    list<Packet*> _enqueued_low;
    list<Packet*> _enqueued_high;

    list<Packet*> _enqueued_rlb; // rlb queue

    // Per-destination queue size
    vector<mem_b> _queuesize_low_per_destination, _queuesize_high_per_destination,
        _queuesize_high_by_origin, _queuesize_low_by_origin;

    mem_b data_bytes_received_at_dst_tor, header_bytes_received_at_dst_tor;
};

#endif
