#include "store.h"
#include <iostream>

// ============================================================
//  Constructor & Destructor
// ============================================================

Store::Store(int capacity)
    : capacity_(capacity), running_(true)
{
    // Start the background active-expiry thread.
    // It runs active_expiry_loop() which sweeps expired keys every 100ms.
    // This is what keeps O(1) for SET/GET — expiry cleanup happens here,
    // not inside the hot path of set_entry().
    expiry_thread_ = std::thread(&Store::active_expiry_loop, this);
}

Store::~Store() {
    // Signal the background thread to stop and wait for it to finish.
    // Without this, destroying the Store while the thread is still running
    // causes undefined behaviour (thread accesses destroyed data).
    running_ = false;
    if (expiry_thread_.joinable()) {
        expiry_thread_.join();
    }
}

// ============================================================
//  Background active-expiry thread
// ============================================================

void Store::active_expiry_loop() {
    // Runs every 100ms while the store is alive.
    // Same approach as real Redis: periodic sweep to clean up expired keys
    // that were never accessed (so lazy expiry never fired on them).
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Collect expired keys — cannot erase from map while iterating it
        std::vector<std::string> to_delete;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [key, entry] : data_) {
                if (is_expired(entry)) {
                    to_delete.push_back(key);
                }
            }
            for (auto& key : to_delete) {
                remove(key);
                std::cout << "[expiry] Background removed expired key: " << key << "\n";
            }
        } // mutex released here
    }
}

// ============================================================
//  Private helpers (called with mutex_ already held by caller)
// ============================================================

bool Store::is_expired(const Entry& entry) {
    if (!entry.has_expiry) return false;
    return std::chrono::steady_clock::now() > entry.expires_at;
}

void Store::touch(const std::string& key) {
    auto it = lru_map_.find(key);
    if (it != lru_map_.end()) {
        lru_list_.erase(it->second);
    }
    lru_list_.push_front(key);
    lru_map_[key] = lru_list_.begin();
}

void Store::remove(const std::string& key) {
    data_.erase(key);
    auto it = lru_map_.find(key);
    if (it != lru_map_.end()) {
        lru_list_.erase(it->second);
        lru_map_.erase(it);
    }
}

void Store::evict_if_needed() {
    // Pure O(1) LRU eviction — no expiry scanning here.
    // The background thread (active_expiry_loop) handles expired keys
    // every 100ms so this path stays O(1) as advertised.
    while ((int)data_.size() > capacity_) {
        std::string lru_key = lru_list_.back();
        std::cout << "[lru] Evicting key: " << lru_key << "\n";
        remove(lru_key);
    }
}

void Store::set_entry(const std::string& key, Entry entry) {
    if (data_.count(key)) {
        remove(key);
    }
    data_[key] = entry;
    touch(key);
    evict_if_needed();
}

// ============================================================
//  Public methods — each locks mutex_ for thread safety
// ============================================================

void Store::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    set_entry(key, Entry{value, false, {}});
}

void Store::set_with_expiry(const std::string& key, const std::string& value, int seconds) {
    Entry e;
    e.value      = value;
    e.has_expiry = true;
    e.expires_at = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    std::lock_guard<std::mutex> lock(mutex_);
    set_entry(key, e);
}

std::optional<std::string> Store::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = data_.find(key);
    if (it == data_.end()) return std::nullopt;
    if (is_expired(it->second)) {
        remove(key);
        return std::nullopt;
    }
    touch(key);
    return it->second.value;
}

bool Store::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!data_.count(key)) return false;
    remove(key);
    return true;
}

bool Store::exists(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = data_.find(key);
    if (it == data_.end()) return false;
    if (is_expired(it->second)) {
        remove(key);
        return false;
    }
    return true;
}

bool Store::expire(const std::string& key, int seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = data_.find(key);
    if (it == data_.end()) return false;
    if (is_expired(it->second)) {
        remove(key);
        return false;
    }
    it->second.has_expiry = true;
    it->second.expires_at = std::chrono::steady_clock::now()
                          + std::chrono::seconds(seconds);
    touch(key);
    return true;
}

std::unordered_map<std::string, Entry> Store::get_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<std::string, Entry> result;
    for (auto& [key, entry] : data_) {
        if (!is_expired(entry)) {
            result[key] = entry;
        }
    }
    return result;
}

void Store::load(const std::unordered_map<std::string, Entry>& incoming) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_.clear();
    lru_list_.clear();
    lru_map_.clear();
    for (auto& [key, entry] : incoming) {
        set_entry(key, entry);
    }
}