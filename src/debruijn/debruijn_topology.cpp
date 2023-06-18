// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-
#include "debruijn_topology.h"
#include <vector>
#include <cstring>
#include <sstream>
#include <iostream>
#include <fstream> // to read from file
#include <algorithm>
#include "main.h"
#include "queue.h"
#include "pipe.h"
#include "compositequeue.h"
#include "enhanced_compositequeue.h"
//#include "prioqueue.h"

#include "rlbmodule.h"

extern uint32_t delay_host2ToR; // nanoseconds, host-to-tor link
extern uint32_t delay_ToR2ToR; // nanoseconds, tor-to-tor link

string ntoa(double n);

string itoa(uint64_t n);

DebruijnTopology::DebruijnTopology(mem_b queuesize, Logfile *lg, EventList *ev, queue_type q, string topfile) {
    _queuesize = queuesize;
    logfile = lg;
    eventlist = ev;
    qt = q;

    read_params(std::move(topfile));

    set_params();

    init_network();
}

// read the topology info from file (generated in Matlab)
void DebruijnTopology::read_params(string topfile) {

    ifstream input(topfile);

    if (input.is_open()) {
        // read the first line of basic parameters:
        string line;
        getline(input, line);
        stringstream stream(line);
        stream >> _no_of_nodes;
        stream >> _ndl;
        stream >> _nul;
        stream >> _ntor;
        stream >> _num_symbols;
        stream >> _address_length;
        stream >> _symbol_length;
        _num_da_links = _nul - _num_symbols;
        // debug:
        cout << "Loaded topology configuration." << endl;
        return;
    }
    assert(0);
}

// set number of possible pipes and queues
void DebruijnTopology::set_params() {

    pipes_serv_tor.resize(_no_of_nodes); // servers to tors
    queues_serv_tor.resize(_no_of_nodes);

    _tor_id_to_debruijn_address.resize(_ntor);
    _adjacency_static_outgoing.resize(_ntor, vector<int>(_nul));
    _adjacency_static_incoming.resize(_ntor, vector<int>(_nul));
    _fibs.resize(_ntor);
    _fibs_static.resize(_ntor);

    rlb_modules.resize(_no_of_nodes);

    pipes_tor.resize(_ntor, vector<Pipe *>(_ndl + _nul)); // tors
    queues_tor.resize(_ntor, vector<Queue *>(_ndl + _nul));

    _active_circuits_dst_to_src.resize(_ntor, vector<int>(_num_da_links));
    _active_circuits_src_to_dst.resize(_ntor, vector<int>(_num_da_links));

    _demand_matrix.resize(_ntor, vector<mem_b>(_ntor, 0));
    _demand_matrix_hosts.resize(_no_of_nodes, vector<mem_b>(_no_of_nodes, 0));
}

Queue *DebruijnTopology::alloc_src_queue(DebruijnTopology *top, QueueLogger *queueLogger, int node) {
    return new PriorityQueue(top, speedFromMbps((uint64_t) HOST_NIC), memFromPkt(FEEDER_BUFFER), *eventlist,
                             queueLogger, node);
}

Queue *DebruijnTopology::alloc_queue(QueueLogger *queueLogger, mem_b queuesize, int tor, int port) {
    return alloc_queue(queueLogger, HOST_NIC, queuesize, tor, port);
}

Queue *DebruijnTopology::alloc_queue(QueueLogger *queueLogger, uint64_t speed, mem_b queuesize, int tor, int port) {
    if (qt == COMPOSITE) {
        return new CompositeQueue(speedFromMbps(speed), queuesize, *eventlist, queueLogger, tor, port, _ntor);
    } else if (qt == ENHANCED_COMPOSITE) {
        return new EnhancedCompositeQueue(speedFromMbps(speed), queuesize, *eventlist, queueLogger, tor, port, _ntor);
    }
    assert(0);
}


void DebruijnTopology::build_static_debruijn_adjacency() {
    // Construct left-shifted de Bruijn graph
    uint neighbor_rack_id;
    for (uint rack_id = 0; rack_id < _ntor; rack_id++) {
        _adjacency_static_outgoing[rack_id].assign(_num_symbols, -1);
        _adjacency_static_incoming[rack_id].assign(_num_symbols, -1);
    }
    for (uint rack_id = 0; rack_id < _ntor; rack_id++) {
        // Note that ToRs have _nul uplinks. The remaining _nul - _num_symbols links are DA
        for (uint neighbor_id = 0; neighbor_id < _num_symbols; neighbor_id++) {
            neighbor_rack_id = (_num_symbols * rack_id + neighbor_id) % _ntor;
            if (neighbor_rack_id == rack_id) {
                // No self-loops. Happens on nodes with ids where all symbols are the same.
                continue;
            }
            _adjacency_static_outgoing[rack_id][neighbor_id] = neighbor_rack_id;
            _adjacency_static_incoming[neighbor_rack_id][neighbor_id] = rack_id;
        }
    }
}

debruijn_address_type_t DebruijnTopology::convert_tor_address(uint tor_id) {
    debruijn_address_type_t converted_address = 0;
    if (tor_id >= pow(_num_symbols, _address_length + 1)) {
        throw std::runtime_error("Number to large for address length and alphabet size");
    }

    debruijn_address_type_t remainder = tor_id, digit;
    for (uint32_t i = _address_length; i > 0; i--) {
        digit = remainder / pow(_num_symbols, i - 1);
        converted_address += digit << ((i - 1) * _symbol_length);
        remainder = remainder - digit * pow(_num_symbols, i - 1);
    }
    return converted_address;
}

void DebruijnTopology::initialize_single_routing_table(uint tor_idx, FibTable *fib) {
    uint32_t len;
    debruijn_address_type_t lambda, tor_address, l_prime, l_two_prime, suffix_mask, next_len_suffix_mask, neighbor_address,
            prefix, prefix_mask;

    tor_address = _tor_id_to_debruijn_address[tor_idx];
    // cout << "tor_address " << std::bitset<32>(tor_address) << " mask "
    //                 << std::bitset<32>(fib->get_address_mask()) << std::endl;
    suffix_mask = 0;
    // len from 0 ... m - 1
    for (len = 0; len < _address_length; len++) {
        // cout << "len=" << len << " suffix_mask " << std::bitset<32>(suffix_mask) << std::endl;
        l_prime = tor_address & suffix_mask;
        // cout << "l_prime " << std::bitset<32>(l_prime) << " = " << l_prime << std::endl;

        next_len_suffix_mask = (suffix_mask << _symbol_length) | ((1 << _symbol_length) - 1);
        for (lambda = 0; lambda < _num_symbols; lambda++) {
            // cout << "lambda " << lambda << std::endl;
            l_two_prime = (l_prime << _symbol_length) + lambda;
            // cout << "l_two_prime " << std::bitset<32>(l_two_prime) << " = " << l_two_prime << std::endl;

            if (l_two_prime == (tor_address & next_len_suffix_mask)) {
                continue;
            }
            neighbor_address = ((tor_address << _symbol_length) + lambda) & fib->get_address_mask();
            // cout << "neighbor_address " << std::bitset<32>(neighbor_address) << " = " << neighbor_address
            //                << std::endl;
            auto next_hop = _debruijn_address_to_tor_id[neighbor_address];
            // cout << "NH:" << next_hop << std::endl;
            prefix = l_two_prime << (_symbol_length * (_address_length - 1 - len));
            prefix_mask = fib->get_address_mask() & (-1 << (_symbol_length * (_address_length - 1 - len)));
            // cout << "Prefix: " << std::bitset<32>(prefix) << " mask " << std::bitset<32>(prefix_mask)
            //                 << std::endl;

            fib->add_fib_entry(
                    std::make_unique<FibEntry>(
                            prefix,
                            prefix_mask,
                            len + 1,
                            0,
                            std::make_unique<FirstEntryMultiNextHop>(std::vector<NextHopPair>(
                                    {NextHopPair(next_hop, lambda)})
                            )
                    )
            );
            // cout << "Added FIB Entry " << std::bitset<32>(prefix) << "," << std::bitset<32>(prefix_mask)
            //                << "," << len + 1 << "," << 0 << "," << next_hop << std::endl;
        }
        // Prepare suffix mask for next length
        suffix_mask = next_len_suffix_mask;
    }
}

void DebruijnTopology::initialize_routing_tables() {
    for (uint tor_id = 0; tor_id < _ntor; tor_id++) {
        _fibs[tor_id] = new FibTable(_address_length, _symbol_length);
        initialize_single_routing_table(tor_id, _fibs[tor_id]);

        _fibs_static[tor_id] = new FibTable(_address_length, _symbol_length);
        initialize_single_routing_table(tor_id, _fibs_static[tor_id]);
    }
    cout << "Populated all ToRs' FIB tables" << endl;
}


// initializes all the pipes and queues in the Topology
void DebruijnTopology::init_network() {
    QueueLoggerSampling *queueLogger;

    // initialize server to ToR pipes / queues
    for (uint j = 0; j < _no_of_nodes; j++) { // sweep nodes
        rlb_modules[j] = nullptr;
        queues_serv_tor[j] = nullptr;
        pipes_serv_tor[j] = nullptr;
    }

    // create server to ToR pipes / queues
    for (uint j = 0; j < _no_of_nodes; j++) { // sweep nodes
        queueLogger = new QueueLoggerSampling(timeFromMs(1000), *eventlist);
        logfile->addLogger(*queueLogger);

        queues_serv_tor[j] = alloc_src_queue(this, queueLogger, j);
        //queues_serv_tor[j][k]->setName("Queue-SRC" + ntoa(k + j*_ndl) + "->TOR" +ntoa(j));
        //logfile->writeName(*(queues_serv_tor[j][k]));

        // rlb_modules[j] = new RlbModule(this, *eventlist, j);

        pipes_serv_tor[j] = new Pipe(timeFromNs(delay_host2ToR), *eventlist);
        //pipes_serv_tor[j][k]->setName("Pipe-SRC" + ntoa(k + j*_ndl) + "->TOR" + ntoa(j));
        //logfile->writeName(*(pipes_serv_tor[j][k]));
    }

    // initialize ToR outgoing pipes / queues
    for (uint j = 0; j < _ntor; j++) {// sweep ToR switches
        for (uint k = 0; k < _nul + _ndl; k++) { // sweep ports
            queues_tor[j][k] = nullptr;
            pipes_tor[j][k] = nullptr;
        }

        // Cache conversion to debruijn address
        _tor_id_to_debruijn_address[j] = convert_tor_address(j);
        _debruijn_address_to_tor_id[_tor_id_to_debruijn_address[j]] = j;

        _active_circuits_src_to_dst[j].assign(_num_da_links, -1);
        _active_circuits_dst_to_src[j].assign(_num_da_links, -1);
    }

    // create ToR outgoing pipes / queues
    for (uint j = 0; j < _ntor; j++) { // sweep ToR switches
        for (uint k = 0; k < _nul + _ndl; k++) { // sweep ports

            if (k >= _nul) {
                // it's a downlink to a server
                queueLogger = new QueueLoggerSampling(timeFromMs(1000), *eventlist);
                logfile->addLogger(*queueLogger);
                queues_tor[j][k] = alloc_queue(queueLogger, _queuesize, j, k);
                //queues_tor[j][k]->setName("Queue-TOR" + ntoa(j) + "->DST" + ntoa(k + j*_ndl));
                //logfile->writeName(*(queues_tor[j][k]));

                pipes_tor[j][k] = new Pipe(timeFromNs(delay_host2ToR), *eventlist);
                //pipes_tor[j][k]->setName("Pipe-TOR" + ntoa(j)  + "->DST" + ntoa(k + j*_ndl));
                //logfile->writeName(*(pipes_tor[j][k]));
            } else {
                // it's a link to another ToR
                queueLogger = new QueueLoggerSampling(timeFromMs(1000), *eventlist);
                logfile->addLogger(*queueLogger);
                queues_tor[j][k] = alloc_queue(queueLogger, _queuesize, j, k);
                //queues_tor[j][k]->setName("Queue-TOR" + ntoa(j) + "->uplink" + ntoa(k - _ndl));
                //logfile->writeName(*(queues_tor[j][k]));

                bool is_da = k >= no_static_links();
                pipes_tor[j][k] = new Pipe(timeFromNs(delay_ToR2ToR), *eventlist, is_da);
                //pipes_tor[j][k]->setName("Pipe-TOR" + ntoa(j)  + "->uplink" + ntoa(k - _ndl));
                //logfile->writeName(*(pipes_tor[j][k]));
            }
        }
    }

    build_static_debruijn_adjacency();
    initialize_routing_tables();
}


int DebruijnTopology::get_next_tor(int current_tor, int current_port) {
    // TODO update
    int next_tor;
    if (current_port >= (int) _num_symbols) {
         next_tor = _active_circuits_src_to_dst[current_tor][current_port-no_static_links()];
    } else {
        next_tor = _adjacency_static_outgoing[current_tor][current_port];
    }
    // cout << "Getting next ToR..." << endl;
    // cout << "   ToR = " << current_tor << " uplink = " << current_port << endl;
    // cout << "   next ToR = " << next_tor << endl;
    return next_tor;
}

vector<int> DebruijnTopology::get_all_ports_for_destination_tor(int current_tor, int destination_tor) {
    if (current_tor == destination_tor) {
        // Return port to first host.
        return {(int) no_of_uplinks()};
    }

    vector<NextHopPair> port;
    // Check cache
#ifdef USE_ROUTE_CACHE
    port = destination_route_cache[current_tor][destination_tor];
    if (port.empty()) {
        FibTable *fib = _fibs[current_tor];
        port = fib->get_next_hop(_tor_id_to_debruijn_address[destination_tor]).get_all();
        destination_route_cache[current_tor][destination_tor] = port;
        if (port.empty()) {
            cout << "No ports. must not happen." << endl;
        }
    }
#else
    FibTable *fib = _fibs[current_tor];
    port = fib->get_next_hop(_tor_id_to_debruijn_address[destination_tor]).get_all();
#endif
    if (port.empty()) {
        cout << "No ports. must not happen." << endl;
    }
    vector<int> out;
    for (auto i : port) {
        out.push_back(i.second);
    }
    return out;
}


int DebruijnTopology::get_port_for_destination_tor(int current_tor, int destination_tor) {
    auto port = get_all_ports_for_destination_tor(current_tor, destination_tor);
    int path_index = random() % port.size();

    //cout << "Getting port..." << endl;
    //cout << "   Inputs: currentTor = " << current_tor << ", dstToR = " << destination_tor << endl;
    //cout << "   Port = " << _port[path_index].second << endl;
    return port[path_index];
}


int DebruijnTopology::get_port(int current_tor, int destination) {
    int destination_tor = get_firstToR(destination);
    if (current_tor == destination_tor) {
        // Find correct port for Host
        return get_port_to_server(destination);
    }
    return get_port_for_destination_tor(current_tor, destination_tor);
}

int DebruijnTopology::get_distance_between_nodes(int node_a, int node_b) {
    uint32_t  address_a = _tor_id_to_debruijn_address[node_a];
    uint32_t  address_b = _tor_id_to_debruijn_address[node_b];
    int distance = 0;
    uint32_t mask = -1;
    mask = mask >> (32 - _address_length * _symbol_length);

    for (int i=0; i < _address_length; i ++) {
        if ((address_a & mask) == (address_b & mask)) {
            break;
        }
        distance += 1;
        for (int j = 0; j < _symbol_length; j++) {
            mask = mask >> _symbol_length;
            address_b = address_b >> _symbol_length;
        }
    }
    return distance;
}


vector<int> DebruijnTopology::get_ports_sorted_by_distance(int current_tor, int destination) {
    int destination_tor = get_firstToR(destination);
    if (current_tor == destination_tor) {
        // Return port to first host.
        return {get_port_to_server(destination)};
    }

    vector<int> ports;
    // Check cache

    ports = sorted_ports_cache[current_tor][destination_tor];
    if (!ports.empty()) {
        return ports;
    }

    vector<pair<int,int>> tmp_ports_with_distance;
    for (int port = 0; port < no_of_uplinks(); port++) {
        int distance = get_distance_to_tor_via_port(current_tor, port, destination_tor);
        if (distance == -1) continue;
        tmp_ports_with_distance.push_back({distance, port});
    }
    std::sort(tmp_ports_with_distance.begin(), tmp_ports_with_distance.end());

    for (auto port : tmp_ports_with_distance) {
        ports.push_back(port.second);
    }

    sorted_ports_cache[current_tor][destination_tor] = ports;
    return ports;
}

int DebruijnTopology::get_distance_to_tor_via_port(int current_tor, int port, int destination_tor) {
    int nexttor = get_next_tor(current_tor, port);
    if (nexttor == -1) return -1;    // No connection on this port.
    int distance = get_distance_between_nodes(nexttor, destination_tor);
    return distance;
}


int DebruijnTopology::get_port_static_for_destination_tor(int current_tor, int destination_tor) {
    vector<NextHopPair> port;

    FibTable *fib = _fibs_static[current_tor];
    port = fib->get_next_hop(_tor_id_to_debruijn_address[destination_tor]).get_all();

    if (port.empty()) {
        cout << "No ports. must not happen." << endl;
    }
    int path_index = random() % port.size();
    return port[path_index].second;
}

int DebruijnTopology::get_port_static(int current_tor, int destination) {
    int destination_tor = get_firstToR(destination);
    if (current_tor == destination_tor) {
        // Find correct port for Host
        return get_port_to_server(destination);
    }

    return get_port_static_for_destination_tor(current_tor, destination_tor);
}


int DebruijnTopology::get_port_segregated_routing_for_destination_tor(int current_tor, int destination_tor) {
    // Check if direct DA link exists
    vector<int> candidate_ports;
    for (int i = 0; i < no_da_links(); i++) {
        if (_active_circuits_src_to_dst.at(current_tor).at(i) == destination_tor) {
            candidate_ports.push_back(i + no_static_links());
        }
    }
    if (candidate_ports.empty()) {
        // No direct DA link, route via static
        return get_port_static_for_destination_tor(current_tor, destination_tor);
    }
    return candidate_ports[random() % candidate_ports.size()];
}


vector<int> DebruijnTopology::get_path_between_tors(int src, int destination, bool only_static) {
    if (src == destination) {
        return {destination};
    }
    vector<int> path({src});
    int current_node = src;
    int next_port;
    while(current_node != destination) {
        if (only_static) {
            next_port = get_port_static_for_destination_tor(current_node, destination);
        } else {
            next_port = get_port_for_destination_tor(current_node, destination);
        }
        current_node = get_next_tor(current_node, next_port);
        path.push_back(current_node);
    }
    return path;
}

vector<vector<pair<int, int>>> DebruijnTopology::get_all_paths_between_tors(int src, int destination, int max_depth) {
    if (src == destination) {
        return {{{destination, _nul}}};
    }
    if (max_depth == 0) {
        throw std::runtime_error("Endless loop");
    }

    FibTable *fib = _fibs[src];
    auto ports = fib->get_next_hop(_tor_id_to_debruijn_address[destination]).get_all();
    vector<vector<pair<int, int>>> paths;
    for (auto port : ports) {
        auto new_paths = get_all_paths_between_tors(port.first, destination, max_depth -1);
        for (auto new_path : new_paths) {
            new_path.push_back({src, port.second});
            paths.push_back(new_path);
        }
    }
    return paths;
}


bool DebruijnTopology::is_last_hop(int port) {
    //cout << "Checking if it's the last hop..." << endl;
    //cout << "   Port = " << port << endl;

    // Ports 0 ... _nul are Debruijn links to other ToRs. Above _nul is downlink to host
    if ((port >= _nul)) // it's a ToR downlink
        return true;
    return false;
}

bool DebruijnTopology::port_dst_match(int port, int crtToR, int dst) {
    //cout << "Checking for port / dst match..." << endl;
    //cout << "   Port = " << port << ", dst = " << dst << ", current ToR = " << crtToR << endl;
    if (port + crtToR * _ndl == dst + _nul)
        return true;
    return false;
}

void DebruijnTopology::update_routing_table_connect_da_link(uint src, uint idx_da, uint dst) {
    auto src_fib = _fibs[src];
    uint32_t len = _address_length;
    uint32_t prefix = _tor_id_to_debruijn_address[dst];
    uint32_t prefix_mask;
    // DEBUG(m_logger) << "Updating table on ToR " << src_node.get_node_name() << std::endl;

    while (len > 0) {
        auto matching_fib_entry_opt = src_fib->get_fib_entry_for_length(prefix, len);
        prefix_mask = src_fib->get_address_mask() & (-1 << (_symbol_length * (_address_length - len)));

        if (matching_fib_entry_opt) {
            auto matching_fib_entry = matching_fib_entry_opt.value();
            // Collision. Add a port group
            auto ports = matching_fib_entry->nexthop().get_all();
            ports.push_back({
                                    dst,
                                    idx_da + no_static_links()
                            }
            );
            src_fib->remove_fib_entry(matching_fib_entry);
            src_fib->add_fib_entry(
                    std::make_unique<FibEntry>(
                            prefix,
                            prefix_mask,
                            len,
                            0,
                            std::make_unique<FirstEntryMultiNextHop>(ports)
                    )
            );
            // DEBUG(m_logger) << "Modified FIB Entry on ToR " << src_node.get_node_name() << ": "
            //                << std::bitset<32>(prefix) << "," << std::bitset<32>(prefix_mask)
            //                << ","
            //                << len << "," << 0 << ",{" << dest_node.get_node_name() << ","
            //                << ports[0].first.get_node_name() << "}" << std::endl;
        } else {
            // New rule is more or less specific. In any case, insert it
            src_fib->add_fib_entry(
                    std::make_unique<FibEntry>(
                            prefix,
                            prefix_mask,
                            len,
                            0,
                            std::make_unique<FirstEntryMultiNextHop>(
                                    std::vector<NextHopPair>(
                                            {NextHopPair(dst, idx_da + no_static_links())})
                            )
                    )
            );
            // DEBUG(m_logger) << "Added FIB Entry on ToR " << src_node.get_node_name() << ": "
            //                 << std::bitset<32>(prefix) << "," << std::bitset<32>(prefix_mask)
            //                 << ","
            //                << len << "," << 0 << "," << dest_node.get_node_name() << std::endl;
        }
        prefix = src_fib->get_address_mask() & (prefix << _symbol_length);
        len--;
    }
}

void DebruijnTopology::update_routing_table_disconnect_da_link(uint src, uint idx_da, uint dst) {
    auto src_fib = _fibs[src];
    uint32_t len = _address_length;
    uint32_t prefix = _tor_id_to_debruijn_address[dst];
    uint32_t prefix_mask;
    // DEBUG(m_logger) << "Restoring table on ToR " << src_node.get_node_name() << std::endl;
    std::vector<std::unique_ptr<FibEntry>> entries_to_add;

    while (len > 0) {
        auto matching_fib_entry_opt = src_fib->get_fib_entry_for_length(prefix, len);

        prefix_mask = src_fib->get_address_mask() & (-1 << (_symbol_length * (_address_length - len)));

        if (matching_fib_entry_opt) {
            // We had a collision so restore previous state. We might keep a port group but with only one member.
            auto matching_fib_entry = matching_fib_entry_opt.value();
            auto ports = matching_fib_entry->nexthop().get_all();
            std::vector<NextHopPair> myports;
            for (auto cport: ports) {
                if ((cport.first == dst) && (cport.second == idx_da + no_static_links())) {
                    // found relevant port. Do not add it.
                    continue;
                }
                myports.push_back(cport);
            }

            src_fib->remove_fib_entry(matching_fib_entry);
            if (!myports.empty()) {
                // Add FibEntry without my port.
                src_fib->add_fib_entry(
                        std::make_unique<FibEntry>(
                                prefix,
                                prefix_mask,
                                len,
                                0,
                                std::make_unique<FirstEntryMultiNextHop>(myports)
                        )
                );
                // DEBUG(m_logger) << "Restored FIB Entry " << std::bitset<32>(prefix) << "," << std::bitset<32>(prefix_mask)
                //                << ","
                //                << len << "," << 0 << ",{" << myports[0].first.get_node_index() << "}" << std::endl;
            } else {
                // DEBUG(m_logger) << "Removed FIB Entry " << std::bitset<32>(prefix) << "," << std::bitset<32>(prefix_mask)
                //                << ","
                //                << len << "," << 0 << std::endl;
            }
        }
        prefix = src_fib->get_address_mask() & (prefix << _symbol_length);
        len--;
    }
}

void DebruijnTopology::connect_da_link_adj(uint src, uint idx_da, uint dst) {
    // (1) Check if ports are free
    assert(_active_circuits_src_to_dst[src][idx_da] == -1);
    assert(_active_circuits_dst_to_src[dst][idx_da] == -1);

    // (2) Note assignment
    _active_circuits_src_to_dst[src][idx_da] = dst;
    _active_circuits_dst_to_src[dst][idx_da] = src;
    _active_circuits.insert({src, idx_da, dst});
    update_routing_table_connect_da_link(src, idx_da, dst);
#ifdef USE_ROUTE_CACHE
    // flow_route_cache[src].clear();
    destination_route_cache[src].clear();
#endif
    sorted_ports_cache[src].clear();
    flow_to_port_cache.clear();
}

void DebruijnTopology::connect_da_link(uint src, uint idx_da, uint dst) {
    connect_da_link_adj(src, idx_da, dst);
    cout << "DA-Link Connected " << src << " " << dst << " OCS-" << idx_da << " " << timeAsMs(eventlist->now()) << endl;
    auto queue = dynamic_cast<CompositeQueue*>(queues_tor[src][idx_da+no_static_links()]);
    queue->notify_connect_da_link();
}

void DebruijnTopology::disconnect_da_link(uint src, uint idx_da, uint dst) {
    // (1) Check if circuit exists
    assert(_active_circuits_src_to_dst[src][idx_da] == (int) dst);
    assert(_active_circuits_dst_to_src[dst][idx_da] == (int) src);

    // (2) Clear circuit
    _active_circuits_src_to_dst[src][idx_da] = -1;
    _active_circuits_dst_to_src[dst][idx_da] = -1;
    _active_circuits.erase({src, idx_da, dst});
#ifdef USE_ROUTE_CACHE
    // flow_route_cache[src].clear();
    destination_route_cache[src].clear();
#endif
    sorted_ports_cache[src].clear();
    flow_to_port_cache.clear();

    update_routing_table_disconnect_da_link(src, idx_da, dst);
    // dynamic_cast<CompositeQueue*>(get_queue_tor(src, idx_da+no_static_links()))->strip_packets_after_da_disconnect();
    dynamic_cast<CompositeQueue*>(get_queue_tor(src, idx_da+no_static_links()))->clear_queue();
    cout << "DA-Link Disconnected " << src << " " << dst << " OCS-" << idx_da << " " << timeAsMs(eventlist->now())
        << " Queuesize: " << get_queue_tor(src, idx_da)->queuesize()  << endl;

}

void DebruijnTopology::clear_source_port(uint src, uint idx_da) {
    if (_active_circuits_src_to_dst[src][idx_da] > -1) {
        disconnect_da_link(src, idx_da, _active_circuits_src_to_dst[src][idx_da]);
    }
}

void DebruijnTopology::clear_destination_port(uint dst, uint idx_da) {
    if (_active_circuits_dst_to_src[dst][idx_da] > -1) {
        disconnect_da_link(_active_circuits_dst_to_src[dst][idx_da], idx_da, dst);
    }
}

void DebruijnTopology::add_new_demand(uint src, uint dst, mem_b volume) {
    _demand_matrix[get_firstToR(src)][get_firstToR(dst)] += volume;
    _demand_matrix_hosts[src][dst] += volume;
}

void DebruijnTopology::reduce_received_demand(uint src, uint dst, mem_b volume) {
    _demand_matrix[get_firstToR(src)][get_firstToR(dst)] -= volume;
    if (_demand_matrix[get_firstToR(src)][get_firstToR(dst)] < 0)
        _demand_matrix[get_firstToR(src)][get_firstToR(dst)] = 0;

    _demand_matrix_hosts[src][dst] -= volume;
    if (_demand_matrix_hosts[src][dst] < 0)
        _demand_matrix_hosts[src][dst] = 0;
}

DebruijnTopology *DebruijnTopology::get_routing_copy() {
    auto copy = new DebruijnTopology();

    // TODO: We copy everything. This is potentially costly.
    copy->_ndl = _ndl;
    copy->_nul = _nul;
    copy->_ntor = _ntor;
    copy->_no_of_nodes = _no_of_nodes;

    copy->_symbol_length = _symbol_length;
    copy->_address_length = _address_length;
    copy->_num_symbols = _num_symbols;
    copy->_num_da_links = _num_da_links;

    copy->_adjacency_static_outgoing = _adjacency_static_outgoing;
    copy->_adjacency_static_incoming = _adjacency_static_incoming;

    copy->_tor_id_to_debruijn_address = _tor_id_to_debruijn_address;
    copy->_debruijn_address_to_tor_id = _debruijn_address_to_tor_id;

    copy->_fibs.resize(_ntor);
    copy->_fibs_static.resize(_ntor);

    copy->_active_circuits_dst_to_src.resize(_ntor, vector<int>(_num_da_links, -1));
    copy->_active_circuits_src_to_dst.resize(_ntor, vector<int>(_num_da_links, -1));

    // Initialize fibs;
    copy->initialize_routing_tables();

    return copy;
}

vector<int> DebruijnTopology::get_all_neighbors_in(int dst) {
    auto neighbors = _adjacency_static_incoming[dst];
    for (auto i : _active_circuits_dst_to_src[dst]) {
        if (i > -1) neighbors.push_back(i);
    }
    return neighbors;
}

vector<int> DebruijnTopology::get_all_neighbors_out(int src) {
    auto neighbors = _adjacency_static_outgoing[src];
    for (auto i : _active_circuits_src_to_dst[src]) {
        if (i > -1) neighbors.push_back(i);
    }
    return neighbors;
}
