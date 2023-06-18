//
// Created by Johannes Zerwas <johannes.zerwas@tum.de> on 5/19/22.
//

#ifndef OPERA_SIM_FIB_H
#define OPERA_SIM_FIB_H


#include <bitset>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <random>
#include <map>
#include <optional>

typedef uint32_t debruijn_address_type_t;
typedef std::pair<debruijn_address_type_t, int> NextHopPair;


class AbstractNextHop {
public:
    virtual NextHopPair select() = 0;

    virtual std::vector<NextHopPair> get_all() = 0;
};


/*
 * Multiple candidate next hops, first is returned, when select is called.
 */
class FirstEntryMultiNextHop : public AbstractNextHop {
public:
    explicit FirstEntryMultiNextHop(std::vector<NextHopPair> candidates);

    NextHopPair select() override;

    std::vector<NextHopPair> get_all() override;

private:
    std::vector<NextHopPair> m_candidates;
};


typedef std::tuple<debruijn_address_type_t, debruijn_address_type_t, uint32_t, uint8_t, std::unique_ptr<AbstractNextHop> > FibEntryTuple;


class FibEntry : public FibEntryTuple {
public:
    FibEntry(debruijn_address_type_t suffix, debruijn_address_type_t mask, uint32_t mask_len, uint8_t priority,
             std::unique_ptr<AbstractNextHop> nh) :
            FibEntryTuple(suffix, mask, mask_len, priority, std::move(nh)) {}

    debruijn_address_type_t suffix() {
        return std::get<0>(*this);
    }

    debruijn_address_type_t mask() {
        return std::get<1>(*this);
    }

    uint32_t mask_len() {
        return std::get<2>(*this);
    }

    uint8_t priority() {
        return std::get<3>(*this);
    }

    AbstractNextHop &nexthop() {
        return *(std::get<4>(*this));
    }

    bool match(debruijn_address_type_t address) {
        return (address & mask()) == suffix();
    }
};


class FibTable {
public:
    FibTable(uint32_t address_length, uint32_t symbol_length);

    debruijn_address_type_t get_address_mask();

    void add_fib_entry(std::unique_ptr<FibEntry> entry);

    void remove_fib_entry(FibEntry *entry);

    FibEntry* get_matching_fib_entry(debruijn_address_type_t address);

    FibEntry* get_matching_fib_entry_with_min_distance(debruijn_address_type_t address, uint32_t min_dist);

    uint32_t get_distance(debruijn_address_type_t address);

    std::optional<FibEntry*> get_fib_entry_for_length(debruijn_address_type_t prefix, uint32_t mask_len);

    AbstractNextHop &get_next_hop(debruijn_address_type_t address);

private:
    // Map: prefix_len -> (prefix -> entry)
    std::map<uint32_t, std::map<debruijn_address_type_t, std::unique_ptr<FibEntry>>> m_fib;
    uint32_t m_address_length, m_symbol_length;
    debruijn_address_type_t m_address_mask;
};

#endif //OPERA_SIM_FIB_H
