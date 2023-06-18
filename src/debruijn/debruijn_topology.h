#ifndef DEBRUIJN
#define DEBRUIJN

#include "main.h"

#include "config.h"
#include "loggers.h" // mod

#include "topology.h"
#include "logfile.h" // mod
#include "eventlist.h"
#include <ostream>
#include <vector>
#include <set>

#include "fib.h"

#ifndef QT
#define QT
    typedef enum {
        COMPOSITE,
        ENHANCED_COMPOSITE
    } queue_type;
#endif

class Queue;
class Pipe;
class Logfile;
class RlbModule;

class DebruijnTopology : public Topology {
public:

    // basic topology elements: pipes and queues:

    // Host uplinks
    vector<Pipe *> pipes_serv_tor; // vector of pointers to pipe
    vector<Queue *> queues_serv_tor; // vector of pointers to queue

    vector<RlbModule *> rlb_modules; // each host has an rlb module

    // ToR egress queues
    vector<vector<Pipe *>> pipes_tor; // matrix of pointers to pipe
    vector<vector<Queue *>> queues_tor; // matrix of pointers to queue

    // these functions are used for label switching
    Pipe *get_pipe_serv_tor(int node) { return pipes_serv_tor[node]; }

    Queue *get_queue_serv_tor(int node) { return queues_serv_tor[node]; }

    Pipe *get_pipe_tor(int tor, int port) { return pipes_tor[tor][port]; }

    Queue *get_queue_tor(int tor, int port) { return queues_tor.at(tor).at(port); }

    RlbModule *get_rlb_module(int host) { return rlb_modules[host]; }

    // Return the idx of the ToR this node belongs to. Host 0->_ndl belong to ToR 0 etc
    [[nodiscard]] int get_firstToR(int node) const { return (int) (node / _ndl); }

    // TODO usage so far not clear
    [[nodiscard]] int get_port_to_server(int dst) const {
        return (int) (_nul + (dst % _ndl));
    }

    // defined in source file

    // Returns the ToR id that the packet will arrive at based on current location of the packet and the port that it
    // will use (internally uses the adjacency list of the topology)
    int get_next_tor(int current_tor, int current_port);

    // Get next port to use from FIB.
    vector<int> get_all_ports_for_destination_tor(int current_tor, int destination_tor);

    int get_port_for_destination_tor(int current_tor, int destination_tor);

    int get_port(int current_tor, int destination);

    int get_port_static_for_destination_tor(int current_tor, int destination_tor);

    int get_port_static(int current_tor, int destination);

    int get_port_segregated_routing_for_destination_tor(int current_tor, int destination_tor);

    vector<int> get_path_between_tors(int src, int destination, bool only_static);

    vector<int> get_path_between_tors(int src, int destination) { return get_path_between_tors(src, destination, true); }

    vector<vector<pair<int, int>>> get_all_paths_between_tors(int src, int destination, int max_depth);

    vector<int> get_ports_sorted_by_distance(int src, int destination);

    int get_distance_to_tor_via_port(int current_tor, int port, int destination_tor);

    int get_distance_between_nodes(int node_a, int node_b);

    bool is_last_hop(int port);

    bool port_dst_match(int port, int crtToR, int dst);

    Logfile *logfile;
    EventList *eventlist;
    int failed_links;
    queue_type qt;

    DebruijnTopology(mem_b queuesize, Logfile *log, EventList *ev, queue_type q, string topfile);

    DebruijnTopology() = default;

    DebruijnTopology* get_routing_copy();

    void init_network();

    void initialize_single_routing_table(uint tor_idx, FibTable *);

    void initialize_routing_tables();

    void update_routing_table_connect_da_link(uint src, uint idx_da, uint dst);
    void update_routing_table_disconnect_da_link(uint src, uint idx_da, uint dst);

    void build_static_debruijn_adjacency();

    void connect_da_link(uint src, uint idx_da, uint dst);
    void connect_da_link_adj(uint src, uint idx_da, uint dst);
    void disconnect_da_link(uint src, uint idx_da, uint dst);

    void clear_source_port(uint src, uint idx_da);
    void clear_destination_port(uint dst, uint idx_da);

    set<tuple<int,int,int>>& active_circuits() { return _active_circuits; };

    Queue *alloc_src_queue(DebruijnTopology *top, QueueLogger *q, int node);

    Queue *alloc_queue(QueueLogger *q, mem_b queuesize, int tor, int port);

    Queue *alloc_queue(QueueLogger *q, uint64_t speed, mem_b queuesize, int tor, int port);

    [[nodiscard]] int no_of_nodes() const override { return (int) _no_of_nodes; } // number of servers
    [[nodiscard]] uint32_t no_of_tors() const { return _ntor; } // number of racks
    [[nodiscard]] uint32_t no_of_hpr() const { return _ndl; } // number of hosts per rack = number of downlinks
    [[nodiscard]] uint32_t no_of_uplinks() const { return _nul; }
    [[nodiscard]] uint32_t no_da_links() const { return _num_da_links; }
    [[nodiscard]] uint32_t no_static_links() const { return _num_symbols; }
    vector<vector<mem_b>> demand_matrix() {return _demand_matrix; }
    vector<vector<mem_b>> demand_matrix_hosts() {return _demand_matrix_hosts; }
    vector<int> get_static_neighbors_out(int src) { return _adjacency_static_outgoing[src]; }
    vector<int> get_static_neighbors_in(int dst) { return _adjacency_static_incoming[dst]; }

    vector<int> get_all_neighbors_in(int dst);
    vector<int> get_all_neighbors_out(int src);

    void add_new_demand(uint src, uint dst, mem_b volume); // Adds the demand (volume of a flow) that just arrived. Idea is to use this similar to demand matrix in flow-level sim.
    void reduce_received_demand(uint src, uint dst, mem_b volume); // Reduce element in demand matrix when a data packet has successfully been received.

    debruijn_address_type_t convert_tor_address(uint tor_id);

private:
    map<Queue *, int> _link_usage;

    void read_params(string topfile);

    void set_params();

    // Tor-to-Tor according to static DeBruijn graph
    // indexing: [tor (0..._ntor)][uplink (indexed from 0 to _nul-1)]
    vector<vector<int>> _adjacency_static_outgoing, _adjacency_static_incoming;
    vector<debruijn_address_type_t> _tor_id_to_debruijn_address;
    map<debruijn_address_type_t, int> _debruijn_address_to_tor_id;

    map<std::pair<int, int>, int> flow_to_port_cache;

    // Ports 0 - (_num_symbols-1) are static, _num_symbols - (_nul-1) are DA, _nul - _nul+_ndl are ports to servers
    uint32_t _ndl, _nul, _ntor, _no_of_nodes; // number down links, number uplinks, number ToRs, number servers

    vector<FibTable *> _fibs;

    vector<FibTable *> _fibs_static; // Only based on static topo. To be used when searching shortcuts

    // DeBruijn parametrization. _num_symbols indicates number of static DB links. The rest (_nul-_num_symbols) will be used as DA links.
    uint32_t _address_length, _num_symbols, _symbol_length, _num_da_links;

    mem_b _queuesize; // queue sizes

    vector<vector<int>> _active_circuits_src_to_dst; // dims: Tors x num. DA links, elements are the destinations
    vector<vector<int>> _active_circuits_dst_to_src; // dims: Tors x num. DA links, elements are the sources
    set<tuple<int,int,int>> _active_circuits;

    vector<vector<mem_b>> _demand_matrix, _demand_matrix_hosts;

#ifdef USE_ROUTE_CACHE
    // map<int, map< uint32_t, int> > flow_route_cache;
    map<int, map<int, vector<NextHopPair>>> destination_route_cache;
#endif
    map<int, map<int, vector<int>>> sorted_ports_cache;
};

#endif
