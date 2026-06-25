#include "store.h"
#include <iostream>
using namespace std;
//  Constructor

Store::Store(int capacity) : capacity_(capacity) {}

//  Private helpers

bool Store::is_expired(const Entry& entry) {
    if (!entry.has_expiry) return false;
    return std::chrono::steady_clock::now() > entry.expires_at;
}

void Store::touch(const std::string& key) {
    // If key is already in the LRU list, remove it from its current position
    auto it = lru_map_.find(key);
    if (it != lru_map_.end()) {
        lru_list_.erase(it->second);  // O(1) because we have the iterator
    }
    // Push key to front (most recently used position)
    lru_list_.push_front(key);
    // Update map to point to the new front position
    lru_map_[key] = lru_list_.begin();
}

void Store::remove(const std::string& key) {
    // Remove from data store
    data_.erase(key);

    // Remove from LRU list using stored iterator (O(1))
    auto it = lru_map_.find(key);
    if (it != lru_map_.end()) {
        lru_list_.erase(it->second);
        lru_map_.erase(it);
    }
}

void Store::evict_if_needed() {
    while ((int)data_.size() > capacity_) {
        // The back of the list is the LEAST recently used key
        std::string lru_key = lru_list_.back();
        std::cout << "[lru] Evicting key: " << lru_key << "\n";
        remove(lru_key);
    }
}

void Store::set_entry(const std::string& key, Entry entry) {
    // If key already exists, remove old LRU record first
    if (data_.count(key)) {
        remove(key); 
    }

    data_[key] = entry;
    touch(key);          // mark as most recently used
    evict_if_needed();   // kick out LRU key if over capacity
}


//  Public methods

void Store::set(const std::string& key, const std::string& value) {
    set_entry(key, Entry{value, false, {}});
}

void Store::set_with_expiry(const std::string& key, const std::string& value, int seconds) {
    Entry e;
    e.value      = value;
    e.has_expiry = true;
    e.expires_at = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    set_entry(key, e);
}

std::optional<std::string> Store::get(const std::string& key) {
    auto it = data_.find(key);
    if (it == data_.end()) return std::nullopt;

    // Lazy expiry check
    if (is_expired(it->second)) {
        remove(key);
        return std::nullopt;
    }

    touch(key);  // accessing a key counts as "using" it — move to front
    return it->second.value;
}

bool Store::del(const std::string& key) {
    if (!data_.count(key)) return false;
    remove(key);
    return true;
}

bool Store::exists(const std::string& key) {
    auto it = data_.find(key);
    if (it == data_.end()) return false;
    if (is_expired(it->second)) {
        remove(key);
        return false;
    }
    return true;
}

bool Store::expire(const std::string& key, int seconds) {
    auto it = data_.find(key);
    if (it == data_.end()) return false;
    if (is_expired(it->second)) {
        remove(key);
        return false;
    }
    it->second.has_expiry = true;
    it->second.expires_at = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    touch(key);  // counts as an access
    return true;
}

std::unordered_map<std::string, Entry> Store::get_all() {
    std::unordered_map<std::string, Entry> result;
    for (auto& [key, entry] : data_) {
        if (!is_expired(entry)) {
            result[key] = entry;
        }
    }
    return result;
}

void Store::load(const std::unordered_map<std::string, Entry>& incoming) {
    // Clear existing state
    data_.clear();
    lru_list_.clear();
    lru_map_.clear();

    // Re-insert through set_entry so LRU structures are populated correctly
    for (auto& [key, entry] : incoming) {
        set_entry(key, entry);
    }
}