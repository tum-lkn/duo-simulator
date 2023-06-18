//
// Created by Johannes Zerwas <johannes.zerwas@tum.de> on 5/19/22.
//

#include "fib.h"
#include "stdexcept"
#include <sstream>


FirstEntryMultiNextHop::FirstEntryMultiNextHop(std::vector<NextHopPair> candidates) : m_candidates(
        std::move(candidates)) {}

NextHopPair FirstEntryMultiNextHop::select() {
    return m_candidates[0];
}

std::vector<NextHopPair> FirstEntryMultiNextHop::get_all() {
    return m_candidates;
}


FibTable::FibTable(uint32_t address_length, uint32_t symbol_length) :
        m_address_length(address_length),
        m_symbol_length(symbol_length) {
    m_address_mask = -1;
    m_address_mask = m_address_mask >> (32 - m_address_length * m_symbol_length);
}

debruijn_address_type_t FibTable::get_address_mask() {
    return m_address_mask;
}

void FibTable::add_fib_entry(std::unique_ptr<FibEntry> entry) {
    if (m_fib.find(entry->mask_len()) == m_fib.end()) {
        m_fib.insert({entry->mask_len(), std::map<debruijn_address_type_t, std::unique_ptr<FibEntry>>()});
    }
    if (m_fib.at(entry->mask_len()).find(entry->suffix()) != m_fib.at(entry->mask_len()).end()) {
        // FIXME Assume for now that we have only one entry per prefix
        throw std::runtime_error("Entry with suffix already exists");
    }
    auto mask_len = entry->mask_len();
    m_fib.at(mask_len).insert({entry->suffix(), std::move(entry)});
}

void FibTable::remove_fib_entry(FibEntry *entry) {
    m_fib.at(entry->mask_len()).erase(entry->suffix());
}

std::optional<FibEntry *> FibTable::get_fib_entry_for_length(debruijn_address_type_t prefix, uint32_t mask_len) {
    if (m_fib.find(mask_len) == m_fib.end()) {
        // No entries with this mask length
        return std::nullopt;
    }
    if (m_fib.at(mask_len).find(prefix) != m_fib.at(mask_len).end()) {
        return m_fib.at(mask_len).at(prefix).get();
    }
    return std::nullopt;
}

FibEntry *FibTable::get_matching_fib_entry(debruijn_address_type_t address) {
    debruijn_address_type_t current_mask, current_prefix;
    uint32_t current_length;
    current_mask = m_address_mask;

    for (current_length = m_address_length; current_length > 0; current_length--) {
        current_prefix = address & current_mask;
        if (m_fib.find(current_length) == m_fib.end()) {
            // No entries with this mask length
            current_mask = (current_mask << m_symbol_length) & m_address_mask;
            continue;
        }
        // std::cout << "Prefix: " << std::bitset<32>(current_prefix) << " mask " << std::bitset<32>(current_mask) << std::endl;
        if (m_fib.at(current_length).find(current_prefix) != m_fib.at(current_length).end()) {
            auto matching_entry = m_fib.at(current_length).at(current_prefix).get();
            // std::cout << matching_entry.suffix() << " " << matching_entry.mask() << " " << matching_entry.nexthop().first.get_node_name() << std::endl;
            return matching_entry;
        }
        current_mask = (current_mask << m_symbol_length) & m_address_mask;
    }
    std::stringstream error;
    error << "No NextHop found for " << address << ". This should not happen!!!";
    throw std::runtime_error(error.str());
}

FibEntry *FibTable::get_matching_fib_entry_with_min_distance(debruijn_address_type_t address, uint32_t min_dist) {
    debruijn_address_type_t current_mask, current_prefix;
    uint32_t current_length;
    current_mask = m_address_mask;
    for (int i = 0; i < min_dist; i++) {
        current_mask = (current_mask << m_symbol_length) & m_address_mask;
    }
    if (min_dist < 1) throw std::runtime_error("Mindist must be >= 1");
    for (current_length = m_address_length + 1 - min_dist; current_length > 0; current_length--) {
        current_prefix = address & current_mask;
        if (m_fib.find(current_length) == m_fib.end()) {
            // No entries with this mask length
            current_mask = (current_mask << m_symbol_length) & m_address_mask;
            continue;
        }
        // std::cout << "Prefix: " << std::bitset<32>(current_prefix) << " mask " << std::bitset<32>(current_mask) << std::endl;
        if (m_fib.at(current_length).find(current_prefix) != m_fib.at(current_length).end()) {
            auto matching_entry = m_fib.at(current_length).at(current_prefix).get();
            // std::cout << matching_entry.suffix() << " " << matching_entry.mask() << " " << matching_entry.nexthop().first.get_node_name() << std::endl;
            return matching_entry;
        }
        current_mask = (current_mask << m_symbol_length) & m_address_mask;
    }
    std::stringstream error;
    error << "No NextHop found for " << address << ". This should not happen!!!";
    throw std::runtime_error(error.str());
}

uint32_t FibTable::get_distance(debruijn_address_type_t address) {
    debruijn_address_type_t current_mask, current_prefix;
    uint32_t current_length;
    current_mask = m_address_mask;

    for (current_length = m_address_length; current_length > 0; current_length--) {
        current_prefix = address & current_mask;
        if (m_fib.find(current_length) == m_fib.end()) {
            // No entries with this mask length
            current_mask = (current_mask << m_symbol_length) & m_address_mask;
            continue;
        }
        // std::cout << "Prefix: " << std::bitset<32>(current_prefix) << " mask " << std::bitset<32>(current_mask) << std::endl;
        if (m_fib.at(current_length).find(current_prefix) != m_fib.at(current_length).end()) {
            return m_address_length - current_length;
        }
        current_mask = (current_mask << m_symbol_length) & m_address_mask;
    }
    std::stringstream error;
    error << "No NextHop found for " << address << ". This should not happen!!!";
    throw std::runtime_error(error.str());
}

AbstractNextHop &FibTable::get_next_hop(debruijn_address_type_t address) {
    return get_matching_fib_entry(address)->nexthop();
}