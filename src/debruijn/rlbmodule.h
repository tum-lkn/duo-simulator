// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#ifndef RLBMODULE_H
#define RLBMODULE_H

//#include <list>
#include "config.h"
#include "eventlist.h"
#include "network.h"
//#include "loggertypes.h"

#include <vector>
#include <deque>

enum DaLinkAlgorithm {ONE_HOP, NONE};


// TODO Remove this class and all its references
class RlbModule : public EventSource {
public:

    RlbModule(DebruijnTopology* top, EventList &eventlist, int node);

    void doNextEvent() override;

    void receivePacket(Packet& pkt, int flag); // pass a packet into the RLB module. Flag = 0 -> append queue. Flag = 1 -> prepend queue.
    Packet* NICpull(); // pull a packet out of the RLB module to be sent by NIC
    void check_all_empty(); // check if there aren't any more packets committed to be sent by RLB module
    void NICpush(); // notification from the NIC that a high priority packet went out (blocking one of RLB's packets)
    void commit_push(int num_to_push); // push a committed packet back into the RLB queues in reponse to a NICpush()
    void enqueue_commit(int slice, int current_commit_queue, int Nsenders, vector<int> pkts_to_send, vector<vector<int>> q_inds, vector<int> dst_labels);
    void clean_commit(); // clean up the oldest commit queue, returning enqueued packets back to the rlb queues

    vector<vector<int>> get_queue_sizes() {return _rlb_queue_sizes;} // returns a pointer to queue_sizes matrix

    double get_max_send_rate() const {return _max_send_rate;}
    double get_slot_time() const {return _slot_time;}
    int get_max_pkts() const {return _max_pkts;}

private:

    DebruijnTopology* _top; // so we can get the pointer to the NIC and the RlbSink

    int _node; // the ID (number) of the host we're running on

    int _H; // total number of hosts in network

    double _link_capacity;

    double _F; // fraction of goodput rate available to RLB (should be 1-[amount reserved for low priority])
    // i.e. when all RLB_commit_queues are sending at max rate, this is the sum of those rates relative to link goodput
    int _Ncommit_queues; // number of commit queues ( = number of rotors = number of ToR uplinks)
    double _mss; // segment size, bytes
    double _hdr; // header size, bytes
    double _link_rate; // link data rate in Bytes / second
    double _slot_time; // seconds

    double _good_rate; // goodput rate, Bytes / second
    double _pkt_ser_time;

    double _max_send_rate; // max. rate at which EACH commit_queue can send, Packets / second
    int _max_pkts; // maximum number of packets EACH commit_queue can send, packets

    vector<vector<deque<Packet*>>> _rlb_queues; // dimensions: 2 (nonlocal, local) x _H (# hosts) x <?> (# packets)
    vector<vector<int>> _rlb_queue_sizes; // dimensions: 2 (nonlocal, local) x _H (# hosts in network)

    vector<deque<Packet*>> _commit_queues; // dimensions: _Ncommit_queues x <?> (# packets)

    int _pull_cnt; // keep track of which queue we're pulling from
    int _push_cnt; // keep track of which queue we're pushing back from (back into rlb_queues)
    int _skip_empty_commits; // we increment this when the NIC tells us to push a packet.

    bool _have_packets; // do ANY of the commit queues have packets to send?

    deque<int> _commit_queue_hist; // keep track of the order of commit queues

};


class DeBruijnOCSMaster : public EventSource {
 public:
    DeBruijnOCSMaster(DebruijnTopology* top, EventList &eventlist, uint32_t day_us, uint32_t night_us,
                      DaLinkAlgorithm algorithm,
                      uint32_t circuit_reservation_duration_us, mem_b heavy_hitter_threshold);

    void start();
    void doNextEvent() override;

    void run_da_link_one_hop();

    void gather_demand_matrix();
    void check_reservations();
    void change_unused_links_to_default();  // Default means parallel to the static DeBruijn topology.
    void match_unused_da_links_randomly();  // After setting DA and default links, the reamining DA ports are used with random matchings

    // void calculate_host_rate_limits(double duration_ms);

    vector<int> fairshare1d(vector<int> input, int cap1, bool extra);
    vector<vector<int>> fairshare2d(vector<vector<int>> input, vector<int> cap0, vector<int> cap1);

    DebruijnTopology* _top;
    bool _next_call_is_day;

    uint64_t day_ps, night_ps, _circuit_reservation_duration_ps;   // scheduling period is day_ps+night_ps; night means reconfiguration
    mem_b _heavy_hitter_volume_threshold;

    DaLinkAlgorithm _algorithm;

    vector<tuple<int, int, int>> _pending_circuits;  // each circuit consist of src, id of DA link (port number), dst
    vector<tuple<mem_b, uint32_t , uint32_t>> _demand_matrix; // Demand matrix in bytes per sd pair (demand, s, d)
    vector<tuple<mem_b, uint32_t , uint32_t>> _queue_occupancy_matrix; // Demand matrix in bytes per sd pair (demand, s, d)

    map<pair<uint32_t , uint32_t >, uint64_t> _temp_allocated_source_ports, _temp_allocated_destination_ports;

    map<pair<int, int>, uint32_t> _distance_via_da_links;

    default_random_engine _rng;
};

#endif
