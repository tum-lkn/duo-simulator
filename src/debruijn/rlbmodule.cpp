// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        

#include "rlbmodule.h"

// ????????

#include <sstream>
#include <cmath>
#include <algorithm>
#include <set>
#include "queue.h"
#include "ndppacket.h"
#include "rlbpacket.h" // added
#include "queue_lossless.h"

#include "rlb.h"
#include "compositequeue.h"
#include "pipe.h"


//////////////////////////////////
//          RLB Module          //
//////////////////////////////////

RlbModule::RlbModule(DebruijnTopology* top, EventList &eventlist, int node)
        : EventSource(eventlist,"rlbmodule"), _node(node), _top(top)
{
    _H = _top->no_of_nodes(); // number of hosts

    _F = 1; // Tuning parameter: scales back how much RLB traffic to send

    _Ncommit_queues = _top->no_of_hpr(); // number of commit queues = number of hosts per rack
    _mss = 1436; // packet payload length (bytes)
    _hdr = 64; // header length (bytes)
    _link_rate = 10000000000 / 8;
    // _slot_time = timeAsSec((_Ncommit_queues - 1) * _top->get_slicetime(3) + _top->get_slicetime(0)); // seconds (was 0.000281;)
    // ^^^ note: we try to send RLB traffic right up to the reconfiguration point (one pkt serialization & propagation before reconfig.)
    // in general, we have two options:
    // 1. design everything conservatively so we never have to retransmit an RLB packet
    // 2. if we have to retransmit an RLB packet for some other reason anyway, might as well "swing for the fences" (i.e. max out sending time)

    // debug:
    //cout << " slot time = " << _slot_time << " seconds" << endl;
    //cout << "   ( = " << (_Ncommit_queues - 1) << " * " << timeAsSec(_top->get_slicetime(3)) << " + " << timeAsSec(_top->get_slicetime(0)) << endl;

    _good_rate = _link_rate * _mss / (_mss + _hdr); // goodput rate, Bytes / second
    _good_rate = _good_rate / _mss; // goodput rate, Packets / seconds
    _pkt_ser_time = 1 / _good_rate;

    // subtract serialization time of _Ncommit_queues packets, in case they all sent at the same time to the same ToR port

    // !! removed to get a little more bandwidth at the rist of dropping packets.
    //_slot_time = _slot_time - (_Ncommit_queues * _pkt_ser_time);

    // test, to try to drop packets
    //_slot_time = 1.2 * _slot_time; // yes, packets were dropped!

    _max_send_rate = (_F / _Ncommit_queues) * _good_rate;
    // _max_pkts = _max_send_rate * _slot_time;

    // NOTE: we're working in packets / second
    //cout << "Initialization:" << endl;
    //cout << "  max_send_rate set to " << _max_send_rate << " packets / second." << endl;
    //cout << "  slot_time set to " << _slot_time << " seconds." << endl;
    //cout << "  max_pkts set to " << _max_pkts << " packets." << endl;

    _rlb_queues.resize(2); // allocate
    for (uint i = 0; i < 2; i++)
        _rlb_queues[i].resize(_H);

    _rlb_queue_sizes.resize(2); // allocate & init
    for (uint i = 0; i < 2; i++) {
        _rlb_queue_sizes[i].resize(_H);
        for (int j = 0; j < _H; j++)
            _rlb_queue_sizes[i][j] = 0;
    }

    _commit_queues.resize(_Ncommit_queues); // allocate

    _have_packets = false; // we don't start with any packets to send
    _skip_empty_commits = 0; // we don't start with any skips

}

void RlbModule::doNextEvent() {
    clean_commit();
}

void RlbModule::receivePacket(Packet& pkt, int flag)
{
    //cout << "RLBmodule - packet received." << endl;

    // this is either from an Rlb sender or from the network
    if (pkt.get_real_src() == _node) { // src matches, it's from the RLB sender (or it's being returned by a ToR)
        // check the "real" dest, and put it in the corresponding LOCAL Rlb queue.
        int queue_ind = pkt.get_real_dst();
        if (flag == 0)
            _rlb_queues[1][queue_ind].push_back(&pkt);
        else if (flag == 1) {
            _rlb_queues[1][queue_ind].push_front(&pkt);

            // debug:
            //cout << "RLBmodule[node" << _node << "] - received a bounced packet at " << timeAsUs(eventlist().now()) << " us" << endl;
        }
        else
            abort(); // undefined flag
        _rlb_queue_sizes[1][queue_ind]++; // increment number of packets in queue

        // debug:
        //cout << "RLBmodule[node" << _node << "] - put packet in local queue at " << timeAsUs(eventlist().now()) <<
        //    " us for dst = " << queue_ind << endl;
        //cout << "   queuesize = " << _rlb_queue_sizes[1][queue_ind] << " packets." << endl;

        // debug:
        //RlbPacket *p = (RlbPacket*)(&pkt);
        //if (p->seqno() == 1)
        //    cout << "> marked packet local queued at node " << _node << endl;

    } else {
        // if it's from the network, we either need to enqueue or send to Rlb sink
        if (pkt.get_dst() == _node) {
            if (pkt.get_real_dst() == _node) {

                // debug:
                //cout << "RLBmodule[node" << _node << "] - received a packet to sink at " << timeAsUs(eventlist().now()) << " us" << endl;

                RlbSink* sink = pkt.get_rlbsink();
                assert(sink);
                sink->receivePacket(pkt); // should this be pkt, *pkt, or &pkt ??? !!!
            } else {
                // check the "real" dest, and put it in the corresponding NON-LOCAL Rlb queue.
                int queue_ind = pkt.get_real_dst();
                pkt.set_src(_node); // change the "source" to this node for routing purposes
                _rlb_queues[0][queue_ind].push_back(&pkt);
                _rlb_queue_sizes[0][queue_ind]++; // increment number of packets in queue

                // debug:
                //cout << "RLBmodule[node" << _node << "] - indirect packet received at " << timeAsUs(eventlist().now()) <<
                //    " us, put in queue " << queue_ind << endl;
                // debug:
                //RlbPacket *p = (RlbPacket*)(&pkt);
                //if (p->seqno() == 1)
                //    cout << "> marked packet nonlocal queued at node " << _node << endl;

            }
        } else {
            // a nonlocal packet was returned by the ToR (must've been delayed by high-priority traffic)
            if (pkt.get_src() == _node) {
                // we check pkt.get_src() to at least make sure it was sent from this node
                assert(flag == 1); // we also require the "enqueue_front" flag be set

                // debug:
                //cout << "bounced NON-LOCAL RLB packet returned to RlbModule at node " << _node << endl;
                //cout << "   real_dst = " << pkt.get_real_dst() << endl;
                //cout << "   time = " << timeAsUs(eventlist().now()) << " us" << endl;

                // check the "real" dest, and put it in the corresponding NON-LOCAL Rlb queue.
                int queue_ind = pkt.get_real_dst();
                _rlb_queues[0][queue_ind].push_front(&pkt);
                _rlb_queue_sizes[0][queue_ind]++; // increment number of packets in queue

            } else {
                cout << "RLB packet received at wrong host: get_dst() = " << pkt.get_dst() << ", _node = " << _node << endl;
                cout << "... get_real_dst() = " << pkt.get_real_dst() << ", get_real_src() = " << pkt.get_real_src() << ", get_src() = " << pkt.get_src() << endl;
                abort();
            }
        }
    }
}

Packet* RlbModule::NICpull()
{
    // NIC is requesting to pull a packet from the commit queues

    // debug:
    //cout << "   Rlbmodule[node" << _node << "] - the NIC pulled a packet at " << timeAsUs(eventlist().now()) << " us." << endl;

    Packet* pkt;

    //cout << "_pull_cnt0 = " << _pull_cnt << endl;

    assert(_have_packets); // assert that we have packets to send

    // start pulling at commit_queues[_pull_cnt]
    bool found_queue = false;
    while (!found_queue) {
        if (!_commit_queues[_pull_cnt].empty()) {
            // the queue is not empty
            pkt = _commit_queues[_pull_cnt].front();
            _commit_queues[_pull_cnt].pop_front();

            if (!pkt->is_dummy()) {

                // debug:
                //if (_node == 0)
                //    cout << " node 0: pull_cnt = " << _pull_cnt << ", a packet was pulled with dst = " << pkt->get_dst() << endl;

                // it's a "real" packet
                _pull_cnt++;
                _pull_cnt = _pull_cnt % _Ncommit_queues;
                found_queue = true;

                //cout << " real packet" << endl;

                // debug:
                //RlbPacket *p = (RlbPacket*)(pkt);
                //if (p->seqno() == 1)
                //    cout << "< marked packet pulled by NIC at node " << _node << endl;

            } else if (_skip_empty_commits > 0) {

                // debug:
                //if (_node == 0)
                //    cout << " node 0: pull_cnt = " << _pull_cnt << ", skipping a dummy packet" << endl;

                // it's a dummy packet, but we can skip it
                _pull_cnt++;
                _pull_cnt = _pull_cnt % _Ncommit_queues;
                _push_cnt--;
                _push_cnt = _push_cnt % _Ncommit_queues;
                if (_push_cnt < 0)
                    _push_cnt = (-1) * _push_cnt;

                _skip_empty_commits--;

                //cout << " skipped dummy packet" << endl;

            } else {

                // debug:
                //if (_node == 0)
                //    cout << " node 0: pull_cnt = " << _pull_cnt << ", can't skip a dummy packet" << endl;

                // it's a dummy packet, and we can't skip it
                _pull_cnt++;
                _pull_cnt = _pull_cnt % _Ncommit_queues;
                found_queue = true;

                //cout << " dummy packet" << endl;

            }
        } else if (_skip_empty_commits > 0) {

            // debug:
            //if (_node == 0)
            //    cout << " node 0: pull_cnt = " << _pull_cnt << ", queue empty, skipping..." << endl;

            // the queue is empty, but we can skip it
            _pull_cnt++;
            _pull_cnt = _pull_cnt % _Ncommit_queues;
            _push_cnt--;
            _push_cnt = _push_cnt % _Ncommit_queues;
            if (_push_cnt < 0)
                _push_cnt = (-1) * _push_cnt;

            _skip_empty_commits--;

            //cout << " skipped empty queue" << endl;

        } else {

            // debug:
            //if (_node == 0)
            //    cout << " node 0: pull_cnt = " << _pull_cnt << ", queue empty, can't skip, giving a dummy packet " << endl;

            // the queue is empty, and we can't skip it
            // make a new dummy packet
            pkt = RlbPacket::newpkt(_mss); // make a dummy packet
            pkt->set_dummy(true);

            _pull_cnt++;
            _pull_cnt = _pull_cnt % _Ncommit_queues;
            found_queue = true;

            //cout << " empty queue = dummy packet" << endl;
        }
    }

    // check if we need to update _have_packets and doorbell the NIC:
    check_all_empty();

    // we can't save _skip_empty_commits, otherwise we'd send too fast later
    // if we have extra, we need to push that many packets back into the rlb queues
    if (_have_packets) {
        if (_skip_empty_commits > 0)
            commit_push(_skip_empty_commits);
    } else { // there already aren't any packets left, so no need to commit_push
        _skip_empty_commits = 0;
    }

    return pkt;

}

void RlbModule::check_all_empty()
{
    bool any_have = false;
    for (uint i = 0; i < _Ncommit_queues; i++) {
        if (!_commit_queues[i].empty()){
            any_have = true;
            break;
        }
    }
    if (!any_have) {
        _have_packets = false;

        //cout << "All queues empty." << endl;

        // !! doorbell the NIC here that there are no packets
        auto nic = dynamic_cast<PriorityQueue*>(_top->get_queue_serv_tor(_node)); // returns pointer to nic queue
        nic->doorbell(false);
    }
}

void RlbModule::NICpush()
{
    if (_have_packets) {
        _skip_empty_commits++;
        // only keep up to _Ncommit_queues - 1 skips (this is all we need)
        int extra_skips = _skip_empty_commits - (_Ncommit_queues - 1);
        if (extra_skips > 0)
            commit_push(extra_skips);
    }
}

void RlbModule::commit_push(int num_to_push)
{
    // push packets back into rlb queues at _commit_queues[_push_cnt]
    for (int i = 0; i < num_to_push; i++) {
        if (!_commit_queues[_push_cnt].empty()) {
            Packet* pkt = _commit_queues[_push_cnt].back();
            if (!pkt->is_dummy()) { // it's a "real" packet

                // figure out if it's a local packet or nonlocal packet
                // and push back to the appropriate queue

                if(pkt->get_real_src() == _node) { // it's a local packet
                    _rlb_queues[1][pkt->get_real_dst()].push_front(pkt);
                    _rlb_queue_sizes[1][pkt->get_real_dst()] ++;
                    _commit_queues[_push_cnt].pop_back();

                    // debug:
                    //if (pkt->get_time_sent() == 342944606400 && pkt->get_real_src() == 177 && pkt->get_real_dst() == 423) {
                    //    cout << "debug @ rlbmodule commit_push:" << endl;
                    //    cout << " returned packet to local queue " << pkt->get_real_dst() << endl;
                    //}

                } else { // it's a nonlocal packet
                    _rlb_queues[0][pkt->get_real_dst()].push_front(pkt);
                    _rlb_queue_sizes[0][pkt->get_real_dst()] ++;
                    _commit_queues[_push_cnt].pop_back();

                    // debug:
                    //if (pkt->get_time_sent() == 342944606400 && pkt->get_real_src() == 177 && pkt->get_real_dst() == 423) {
                    //    cout << "debug @ rlbmodule commit_push:" << endl;
                    //    cout << " returned packet to nonlocal queue " << pkt->get_real_dst() << endl;
                    //}

                }

            } else { // it's a dummy packet
                _commit_queues[_push_cnt].pop_back();
                pkt->free(); // delete the dummy packet
            }
        }
        _skip_empty_commits--;
        _push_cnt--;
        _push_cnt = _push_cnt % _Ncommit_queues;
        if (_push_cnt < 0)
            _push_cnt = (-1) * _push_cnt;
    }
    check_all_empty();
}

void RlbModule::enqueue_commit(int slice, int current_commit_queue, int Nsenders, vector<int> pkts_to_send, vector<vector<int>> q_inds, vector<int> dst_labels)
{

    // convert #packets_to_be_sent into sending_rates:

    vector<double> sending_rates;
    for (int i = 0; i < Nsenders; i++) {
        sending_rates.push_back(pkts_to_send[i] / _slot_time);

        // debug:
        //if (_node == 0)
        //    cout << "_node " << _node << ", sender " << i << ", " << pkts_to_send[i] << " packets to send in " << _slot_time << " seconds" << endl;
    }

    // debug:
    //cout << "RLBmodule[node" << _node << "] - enqueuing commit queue[" << current_commit_queue << "] at " << timeAsUs(eventlist().now()) << " us." << endl;

    // push this commit_queue to the history
    _commit_queue_hist.push_back(current_commit_queue);

    // if we previously didn't have any packets, reset the _pull_cnt & _push_cnt accordingly
    // also, doorbell the NIC
    if (!_have_packets) {
        _have_packets = true;
        _pull_cnt = current_commit_queue;
        _push_cnt = current_commit_queue-1;
        _push_cnt = _push_cnt % _Ncommit_queues; // wrap around
        if (_push_cnt < 0)
            _push_cnt = (-1) * _push_cnt;

        // !!! doorbell to the priority queue (NIC) that we have packets now.
        auto nic = dynamic_cast<PriorityQueue*>(_top->get_queue_serv_tor(_node)); // returns pointer to nic queue
        nic->doorbell(true);

    }

    // use rates to get send times:
    vector<std::deque<double>> send_times; // vector of queues containing send times (seconds)
    send_times.resize(Nsenders);
    for (int i = 0; i < Nsenders; i++) {
        double inter_time = 1 / sending_rates[i];
        int cnt = 0;
        double time = cnt * inter_time; // seconds
        while (time < _slot_time) {
            send_times[i].push_back(time);
            cnt++;
            time = cnt * inter_time;
        }
    }
    // make sure we have at least one time past the end time:
    // this ensures we never pop the last send time, which would cause problems during enqueue
    for (int i = 0; i < Nsenders; i++)
        send_times[i].push_back(2 * _slot_time);


    // debug:
    //cout << "The queue is:" << endl;

    // use send times to fill the queue
    for (int i = 0; i < _max_pkts; i++){
        bool found_queue = false;
        while (!found_queue) {
            double end_slot_time = (i + 1) * (_Ncommit_queues * _pkt_ser_time); // send_time must be less than this to send in this slot
            int first_sender = -1; // find the queue that sends first
            for (int j = 0; j < Nsenders; j++) {
                if (send_times[j].front() < end_slot_time) {
                    first_sender = j;
                    end_slot_time = send_times[j].front(); // new time to beat
                }
            }
            if (first_sender != -1) { // there was a queue that can send
                // now check if the queue has packets
                if (_rlb_queue_sizes[q_inds[0][first_sender]][q_inds[1][first_sender]] > 0) {
                    // not empty, pop the send_time, move the packet, and break
                    send_times[first_sender].pop_front();

                    // debug:
                    //cout << fst_sndr << " ";

                    // debug:
                    //cout << " Rlbmodule - committing from queue[" << q_inds[0][fst_sndr] << "][" << q_inds[1][fst_sndr] << "], chaning destination to " << dst_labels[fst_sndr] << endl;

                    // get the packet, and set its destination:
                    Packet* pkt;
                    pkt = _rlb_queues[q_inds[0][first_sender]][q_inds[1][first_sender]].front();
                    pkt->set_dst(dst_labels[first_sender]);

                    // debug:
                    //if (pkt->get_time_sent() == 342944606400 && pkt->get_real_src() == 177 && pkt->get_real_dst() == 423) {
                    //    cout << "debug @ rlbmodule commit:" << endl;
                    //    cout << " _node = " << _node << endl;
                    //    cout << " pkt->get_dst() = " << pkt->get_dst() << endl;
                    //    cout << " slice = " << slice << endl;
                    //}

                    // we need to timestamp the packet for routing when it is committed to be sent:
                    pkt->set_slice_sent(slice); // "timestamp" the packet

                    // debug:
                    //if (_node == 0)
                    //    cout << "   packet committed from sending queue: " << fst_sndr << ", for dest: " << dst_labels[fst_sndr] << endl;

                    // debug:
                    //RlbPacket *p = (RlbPacket*)(pkt);
                    //if (p->seqno() == 1)
                    //    cout << "* marked packet committed at node: " << _node << " for dst: " << pkt->get_dst() << " in slice: " << slice << endl;


                    // put the packet in the commit queue
                    _commit_queues[current_commit_queue].push_back( pkt );
                    _rlb_queues[q_inds[0][first_sender]][q_inds[1][first_sender]].pop_front(); // remove the packet from the rlb queue
                    _rlb_queue_sizes[q_inds[0][first_sender]][q_inds[1][first_sender]]--; // decrement queue size by one packet
                    found_queue = true;
                } else // the queue was empty, pop the send time and keep looking
                    send_times[first_sender].pop_front();
            } else { // no queues can send right now; put a dummy packet into the commit queue and go to next commit_queue slot

                // debug:
                //cout << "_ ";

                Packet* p = RlbPacket::newpkt(_mss); // make a dummy packet
                p->set_dummy(true);

                _commit_queues[current_commit_queue].push_back(p);
                found_queue = true;
            }
        }
    }

    // debug:
    //cout << endl;

    // debug:
    //if (_node == 0){
    //    for (int i = 0; i < _Ncommit_queues; i++)
    //        cout << " _node 0: commit_queue: " << i << ". Length = " << _commit_queues[i].size() << " packets" << endl;
    //}

    // create a delayed event that cleans this queue after the slot ends
    eventlist().sourceIsPendingRel(*this, timeFromSec(_slot_time));
}

void RlbModule::clean_commit()
{
    // clean up the oldest commit queue
    int oldest = _commit_queue_hist.front();

    // debug:
    //cout << "Cleaning commit queue[" << oldest << "] at " << timeAsUs(eventlist().now()) << " us." << endl;

    // while there are packets in the queue
    // return them to the FRONT of the rlb queues
    while (!_commit_queues[oldest].empty()) {
        Packet* pkt = _commit_queues[oldest].back();
        if (!pkt->is_dummy()) {
            // it's a "real" packet
            // !!! note - hardcoded to push to a local queue
            //_rlb_queues[1][pkt->get_dst()].push_front( _commit_queues[oldest].back() );
            //_rlb_queue_sizes[1][pkt->get_dst()] ++;
            //_commit_queues[oldest].pop_back();

            // debug:
            //if (pkt->get_time_sent() == 342944606400 && pkt->get_real_src() == 177 && pkt->get_real_dst() == 423) {
            //    cout << "debug @ rlbmodule clean_commit:" << endl;
            //    cout << " wanted to return packet to local queue " << pkt->get_dst() << endl;
            //    cout << " fixed to return packet to nonlocal queue " << pkt->get_real_dst() << endl;
            //}

            if (pkt->get_real_src() == _node) {
                // put it back in the local queue
                _rlb_queues[1][pkt->get_real_dst()].push_front( _commit_queues[oldest].back() );
                _rlb_queue_sizes[1][pkt->get_real_dst()] ++;
                _commit_queues[oldest].pop_back();
            } else {
                // put it back in the nonlocal queue
                _rlb_queues[0][pkt->get_real_dst()].push_front( _commit_queues[oldest].back() );
                _rlb_queue_sizes[0][pkt->get_real_dst()] ++;
                _commit_queues[oldest].pop_back();
            }

        } else {
            // it's a dummy packet
            _commit_queues[oldest].pop_back();
            pkt->free(); // delete the dummy packet
        }
    }

    // check if we need to doorbell NIC that there are no more packets
    // we only do this if we just had some packets, to prevent doorbelling the NIC twice
    if (_have_packets)
        check_all_empty();

    // pop this commit_queue from the history
    _commit_queue_hist.pop_front();
}




DeBruijnOCSMaster::DeBruijnOCSMaster(DebruijnTopology* top, EventList &eventlist, uint32_t day_us, uint32_t night_us,
                                     DaLinkAlgorithm algorithm,
                                     uint32_t circuit_reservation_duration_us, mem_b heavy_hitter_threshold)
  : EventSource(eventlist,"rlbmaster"), _top(top), day_ps(timeFromUs(day_us)), night_ps(timeFromUs(night_us)),
    _circuit_reservation_duration_ps(timeFromUs(circuit_reservation_duration_us)), _heavy_hitter_volume_threshold(heavy_hitter_threshold),
    _algorithm(algorithm)
{
    _next_call_is_day = false;
    _rng = default_random_engine (1948);
}

void DeBruijnOCSMaster::start() {
    // set it up to start "ticking" at time 0 (the first rotor reconfiguration)
    eventlist().sourceIsPending(*this, timeFromNs(1)); // Start at t=1ns
}

void DeBruijnOCSMaster::doNextEvent() {
    // newMatching();
    if (_next_call_is_day) {
        _next_call_is_day = false;

        for (auto circuit : _pending_circuits) {
            _top->connect_da_link(std::get<0>(circuit), std::get<1>(circuit), std::get<2>(circuit));
        }
        _pending_circuits.clear();

        // Set up next event when the day duration is over
        eventlist().sourceIsPending(*this, eventlist().now() + day_ps);
    } else {
        _next_call_is_day = true;
        switch (_algorithm) {
            default:
                run_da_link_one_hop();
                change_unused_links_to_default();
                match_unused_da_links_randomly();
        }

        // Set up next event when the night duration (reconfiguration) is over
        eventlist().sourceIsPending(*this, eventlist().now() + night_ps);
    }

}


void DeBruijnOCSMaster::gather_demand_matrix() {
    _demand_matrix.clear();
    //TODO can we make this more efficient?
    /*for (uint i=0; i < _top->no_of_nodes(); i++) {
        if (_top->get_queue_serv_tor(i)->queuesize() > 0)
            cout << "Host: " << i << ": q=" << _top->get_queue_serv_tor(i)->queuesize() << endl;
    }*/

    cout << "Demand Matrix " << timeAsMs(eventlist().now()) << endl;
    for (uint src = 0; src < _top->no_of_tors(); src++) {
        cout << "DM " << src << " ";
        for (uint dst = 0; dst < _top->no_of_tors(); dst++) {
            /*mem_b demand_this_sd_pair = 0;
            for (uint port = 0; port < _top->no_of_uplinks(); port++) {
                demand_this_sd_pair += dynamic_cast<CompositeQueue *>(
                        _top->get_queue_tor(src, port))->queuesize_per_destination(dst);

            }*/
            mem_b demand_this_sd_pair = _top->demand_matrix()[src][dst];
            cout << demand_this_sd_pair << ",";
            if (demand_this_sd_pair > 0) {
                _demand_matrix.emplace_back(
                        tuple<mem_b, uint32_t, uint32_t>({demand_this_sd_pair, src, dst}));
            }
        }
        cout << endl;
    }
}


void DeBruijnOCSMaster::run_da_link_one_hop() {
    gather_demand_matrix();
    std::sort(_demand_matrix.begin(), _demand_matrix.end(), std::greater<>());
    mem_b demand;
    uint32_t src, dst;

    check_reservations();

    auto active_circuits = _top->active_circuits();
    bool found_circuit = true;
    while (found_circuit) {
        found_circuit = false;
        for (auto &traffic: _demand_matrix) {
            std::tie(demand, src, dst) = traffic;
            // cout << src << " -> " << dst << ": " << demand<< std::endl;

            if (demand < _heavy_hitter_volume_threshold) {
                // Not a heavy hitter
                break;
            }

            cout << "Found heavy hitter " << src << " -> " << dst << ": " << demand << endl;

            for (uint port = 0; port < _top->no_da_links(); port++) {
                if (
                        (_temp_allocated_source_ports.find(
                                pair<uint, uint>{src, port}) !=
                         _temp_allocated_source_ports.end())
                        ) {
                    // Source cannot use this OCS
                    continue;
                }
                if (
                        (_temp_allocated_destination_ports.find(
                                pair<uint, uint>{dst, port}) !=
                         _temp_allocated_destination_ports.end())
                        ) {
                    // Destination cannot use this OCS
                    continue;
                }

                cout << "Found shortcut. Setting circuit between " << src << " and " << dst << " via OCS " << port
                     << "(" << port + _top->no_static_links() << ")" << endl;
                found_circuit = true;
                tuple<int, int, int> circuit({src, port, dst});

                if (active_circuits.find(circuit) == active_circuits.end()) {
                    // Only add circuit if it is new, i.e., do not interrupt circuit if we would set the same again...
                    _top->clear_source_port(src, port);
                    _top->clear_destination_port(dst, port);
                    _pending_circuits.push_back(circuit);
                }
                auto reservation_expiration_time = eventlist().now() + _circuit_reservation_duration_ps;
                _temp_allocated_source_ports[{src, port}] = reservation_expiration_time;
                _temp_allocated_destination_ports[{dst, port}] = reservation_expiration_time;
                // std::cout << "Reserving " << src.get_node_name() << "-" << dst.get_node_name() << " via " << ocs->get_node_name() << " until " << reservation_expiration_time << std::endl;
                break;
            }
        }
    }
}


void DeBruijnOCSMaster::check_reservations() {
    if (_circuit_reservation_duration_ps == 0) {
        _temp_allocated_source_ports.clear();
        _temp_allocated_destination_ports.clear();
        return;
    }
    auto current_time = eventlist().now();

    for (auto &link : _top->active_circuits()) {
        int src, port_idx, dst;
        std::tie(src, port_idx,dst) = link;

        if (!_top->get_pipe_tor(dst, port_idx)->has_packets_inflight()) {
            // Circuit is empty
            continue;
        }
        _temp_allocated_source_ports[{src, port_idx}] = current_time + _circuit_reservation_duration_ps;
        _temp_allocated_destination_ports[{dst, port_idx}] = current_time + _circuit_reservation_duration_ps;
    }

    for (auto it = _temp_allocated_source_ports.cbegin(); it != _temp_allocated_source_ports.cend()  /* not hoisted */; /* no increment */) {
        if (it->second <= current_time) {
            _temp_allocated_source_ports.erase(it++);    // or "it = m.erase(it)" since C++11
        } else {
            ++it;
        }
    }

    for (auto it = _temp_allocated_destination_ports.cbegin(); it != _temp_allocated_destination_ports.cend()  /* not hoisted */; /* no increment */) {
        if (it->second <= current_time) {
            _temp_allocated_destination_ports.erase(it++);    // or "it = m.erase(it)" since C++11
        } else {
            ++it;
        }
    }
}

void DeBruijnOCSMaster::change_unused_links_to_default() {
    uint32_t list_roll_index;
    auto reservation_expiration_time = eventlist().now() + _circuit_reservation_duration_ps;
    auto active_circuits = _top->active_circuits();

    vector<vector<int>> src_static_neighbors(_top->no_of_tors()), dst_static_neighbors(_top->no_of_tors());
    for (int port=0; port < _top->no_da_links(); port++) {
        // Iterate over links that are incoming to OCS. These are the source ports of th ToRs for DA links.
        for (int src = 0; src < _top->no_of_tors(); src ++) {
            if (_temp_allocated_source_ports.find({src, port}) !=
                _temp_allocated_source_ports.end()) {
                // Src port is reserved
                continue;
            }

            if (src_static_neighbors[src].empty()) {
                src_static_neighbors[src] = _top->get_static_neighbors_out(src);
            }

            list_roll_index = 0;
            for (auto static_dst : src_static_neighbors[src]) {
                if (static_dst < 0) continue;
                ++list_roll_index;
                if (_temp_allocated_destination_ports.find({static_dst, port}) !=
                    _temp_allocated_destination_ports.end()) {
                    // Port at destination is not free
                    continue;
                }

                cout << "Setting default link (source) " << src << " -> " << static_dst
                     << " via OCS-" << port << std::endl;
                tuple<int, int, int> circuit({src, port, static_dst});
                if (active_circuits.find(circuit) == active_circuits.end()) {
                    // Only add circuit if it is new, i.e., do not interrupt circuit if we would set the same again...
                    _top->clear_source_port(src, port);
                    _top->clear_destination_port(static_dst, port);
                    _pending_circuits.push_back(circuit);
                }
                _temp_allocated_source_ports[{src, port}] = reservation_expiration_time;
                _temp_allocated_destination_ports[{static_dst, port}] = reservation_expiration_time;

                // Roll list of static links
                std::rotate(src_static_neighbors[src].begin(),
                            src_static_neighbors[src].begin()+list_roll_index,src_static_neighbors[src].end());
                break;
            }
        }
        // Iterate over phys links that are outgoing from OCS. These are the destination ports of th ToRs for DA links.
        for (int dst = 0; dst < _top->no_of_tors(); dst ++) {
            if (_temp_allocated_destination_ports.find({dst, port}) !=
                _temp_allocated_destination_ports.end()) {
                // Link is reserved
                continue;
            }
            // Check if static link belonging to dst idx can be set
            if (dst_static_neighbors[dst].empty()) {
                dst_static_neighbors[dst] = _top->get_static_neighbors_in(dst);
            }
            list_roll_index = 0;
            for (auto static_src: dst_static_neighbors[dst]) {
                if (static_src < 0) continue;
                ++list_roll_index;
                if (_temp_allocated_source_ports.find({static_src, port}) !=
                    _temp_allocated_source_ports.end()) {
                    // Port at destination is not free
                    continue;
                }

                tuple<int, int, int> circuit({static_src, port, dst});
                if (active_circuits.find(circuit) == active_circuits.end()) {
                    // Only add circuit if it is new, i.e., do not interrupt circuit if we would set the same again...
                    _top->clear_source_port(static_src, port);
                    _top->clear_destination_port(dst, port);
                    _pending_circuits.push_back(circuit);
                    cout << "Setting default link (destination):  " << static_src << " -> "
                        << dst << " via OCS-" << port << std::endl;
                }
                _temp_allocated_source_ports[{static_src, port}] = reservation_expiration_time;
                _temp_allocated_destination_ports[{dst, port}] = reservation_expiration_time;

                // Roll list of static links
                std::rotate(dst_static_neighbors[dst].begin(),
                            dst_static_neighbors[dst].begin()+list_roll_index,dst_static_neighbors[dst].end());
                break;
            }
        }
    }
}

void DeBruijnOCSMaster::match_unused_da_links_randomly() {
    auto reservation_expiration_time = eventlist().now() + _circuit_reservation_duration_ps;
    auto active_circuits = _top->active_circuits();


    for (int port=0; port < _top->no_da_links(); port++) {
        // Iterate over links that are incoming to OCS. These are the source ports of th ToRs for DA links.


        vector<int> candidate_source_ports, candidate_destination_ports;
        // Iterate over links that are incoming to OCS. These are the source ports of th ToRs for DA links.
        for (int src = 0; src < _top->no_of_tors(); src ++) {
            if (_temp_allocated_source_ports.find({src, port}) !=
                _temp_allocated_source_ports.end()) {
                // Src port is reserved
                continue;
            }
            candidate_source_ports.push_back(src);
        }
        // Iterate over phys links that are outgoing from OCS. These are the destination ports of th ToRs for DA links.
        for (int dst = 0; dst < _top->no_of_tors(); dst ++) {
            if (_temp_allocated_destination_ports.find({dst, port}) !=
                _temp_allocated_destination_ports.end()) {
                // Link is reserved
                continue;
            }
            candidate_destination_ports.push_back(dst);
        }

        std::shuffle(candidate_destination_ports.begin(), candidate_destination_ports.end(), _rng);
        uint32_t num_attempts = 0;
        for (int  src : candidate_source_ports) {
            if (candidate_destination_ports.empty()) break;

            int dst = candidate_destination_ports.back();

            num_attempts = 0;
            while (src == dst && num_attempts < 10) {
                std::shuffle(candidate_destination_ports.begin(), candidate_destination_ports.end(), _rng);
                num_attempts++;
                dst = candidate_destination_ports.back();
            }
            // We could not find a valid pair after 10 attempts
            if (src == dst) {
                cout << "Could not find meaningful matching for " << src << " after 10 attempts. Skipping." << std::endl;
                continue;
            }
            candidate_destination_ports.pop_back();
            tuple<int, int, int> circuit({src, port, dst});
            if (active_circuits.find(circuit) == active_circuits.end()) {
                // Only add circuit if it is new, i.e., do not interrupt circuit if we would set the same again...
                _top->clear_source_port(src, port);
                _top->clear_destination_port(dst, port);
                _pending_circuits.push_back(circuit);
                cout << "Setting random link " << src << " -> " << dst << " via OCS-" << port << std::endl;
            }
            _temp_allocated_source_ports[{src, port}] = reservation_expiration_time;
            _temp_allocated_destination_ports[{dst, port}] = reservation_expiration_time;
        }
    }
}


vector<int> DeBruijnOCSMaster::fairshare1d(vector<int> input, int cap1, bool extra) {
    vector<int> sent;
    sent.resize(input.size());
    for (uint i = 0; i < input.size(); i++)
        sent[i] = input[i];

    int nelem = 0;
    for (auto i : input)
        if (i > 0) nelem++;

    if (nelem != 0) {
        bool cont = true;
        while (cont) {
            int f = cap1 / nelem; // compute the fair share
            int min = 0;
            for (uint i = 0; i < input.size(); i++) {
                if (input[i] > 0) {
                    input[i] -= f;
                    if (input[i] < 0)
                        min = input[i];
                }
            }
            cap1 -= f * nelem;
            if (min < 0) { // some elements got overserved
                //cap1 = 0;
                for (uint i = 0; i < input.size(); i++)
                    if (input[i] < 0) {
                        cap1 += (-1) * input[i];
                        input[i] = 0;
                    }
                nelem = 0;
                for (uint i = 0; i < input.size(); i++)
                    if (input[i] > 0)
                        nelem++;
                if (nelem == 0) {
                    cont = false;
                }
            } else {
                cont = false;
            }
        }
    }

    for (uint i = 0; i < input.size(); i++)
        sent[i] -= input[i];

    return sent; // return what was sent
}

vector<vector<int>> DeBruijnOCSMaster::fairshare2d(vector<vector<int>> input, vector<int> cap0, vector<int> cap1) {

    // if we take `input` as an N x M matrix (N rows, M columns)
    // then cap0[i] is the capacity of the sum of the i-th row
    // and cap1[i] is the capacity of the sum of the i-th column

    // build output
    vector<vector<int>> sent;
    sent.resize(input.size());
    for (uint i = 0; i < input.size(); i++) {
        sent[i].resize(input[0].size());
        for (uint j=0; j < input[0].size(); j++)
            sent[i][j] = 0;
    }

    int maxiter = 5;
    int iter = 0;

    int nelem = 0;
    for (uint i = 0; i < input.size(); i++)
        for (uint j=0; j < input[0].size(); j++)
            if (input[i][j] > 0)
                nelem++;

    if (nelem == 0) {
        for (uint i = 0; i < sent.size(); i++) {
            for (uint j = 0; j < sent[0].size(); j++) {
                sent[i][j] = cap0[i]/sent[0].size();
            }
        }
    }

    while (nelem != 0 && iter < maxiter) {
        // temporary matrix:
        vector<vector<int>> sent_temp;
        sent_temp.resize(input.size());
        for (uint i = 0; i < input.size(); i++) {
            sent_temp[i].resize(input[0].size());
            for (uint j=0; j < input[0].size(); j++)
                sent_temp[i][j] = 0;
        }

        // sweep rows (i): (cols j)
        for (uint i = 0; i < input.size(); i++) {
            int prev_alloc = 0;
            for (uint j = 0; j < input[0].size(); j++)
                prev_alloc += sent[i][j];
            sent_temp[i] = fairshare1d(input[i], cap0[i] - prev_alloc, true);
        }

        // sweep columns (i): (rows j)
        for (uint i = 0; i < input[0].size(); i++) {
            int prev_alloc = 0;
            vector<int> temp_vect;
            for (uint j = 0; j < input.size(); j++) {
                prev_alloc += sent[j][i];
                temp_vect.push_back(sent_temp[j][i]);
            }
            temp_vect = fairshare1d(temp_vect, cap1[i] - prev_alloc, true);
            for (uint j = 0; j < input.size(); j++)
                sent_temp[j][i] = temp_vect[j];
        }

        // update the `sent` matrix with the `sent_temp` matrix:
        for (uint i = 0; i < input.size(); i++)
            for (uint j=0; j < input[0].size(); j++)
                sent[i][j] += sent_temp[i][j];

        // update the input matrix:
        for (uint i = 0; i < input.size(); i++) {
            for (uint j=0; j < input[0].size(); j++) {
                input[i][j] -= sent_temp[i][j];
                if (input[i][j] < 0)
                    input[i][j] = 0;
            }
        }

        // work our way "backwards", checking if cap1[] and cap0[] have been used up

        // cap1[] used up? if so, set the column to zero
        // (sweep columns = i)
        for (uint i = 0; i < input[0].size(); i++) {
            int remain = cap1[i];
            for (uint j = 0; j < input.size(); j++)
                remain -= sent[j][i];

            if (remain <= 0) {
                for (uint j = 0; j < input.size(); j++)
                    input[j][i] = 0;
            }
        }

        // cap0[] used up? if so, set the row to zero
        // (sweep rows = i)
        for (uint i = 0; i < input.size(); i++) {
            int remain = cap0[i];
            for (uint j = 0; j < input[0].size(); j++)
                remain -= sent[i][j];

            if (remain <= 0) {
                for (uint j = 0; j < input.size(); j++)
                    input[i][j] = 0;
            }
        }

        // get number of remaining elements:
        nelem = 0;
        for (uint i = 0; i < input.size(); i++)
            for (uint j=0; j < input[0].size(); j++)
                if (input[i][j] > 0)
                    nelem++;
        iter++;
    }
    return sent; // return what was sent
}
