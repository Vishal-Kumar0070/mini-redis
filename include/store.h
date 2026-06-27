#pragma once
#include <string>
#include <unordered_map>
#include <list>
#include <vector>
#include <chrono>
#include <optional>
#include <thread>
#include <mutex>
#include <atomic>

// Holds one value + optional expiry time
struct Entry {
    std::string value;
    bool has_expiry = false;
    std::chrono::steady_clock::time_point expires_at;
};

class Store {
public:
    // capacity = max number of keys before LRU eviction kicks in
    Store(int capacity = 100);

    // destructor stops the background expiry thread cleanly
    ~Store();

    void set(const std::string& key, const std::string& value);
    void set_with_expiry(const std::string& key, const std::string& value, int seconds);
    std::optional<std::string> get(const std::string& key);
    bool del(const std::string& key);
    bool exists(const std::string& key);
    bool expire(const std::string& key, int seconds);
    std::unordered_map<std::string, Entry> get_all();
    void load(const std::unordered_map<std::string, Entry>& data);

    int size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return (int)data_.size();
    }

private:
    int capacity_;

    std::unordered_map<std::string, Entry> data_;
    std::list<std::string> lru_list_;
    std::unordered_map<std::string, std::list<std::string>::iterator> lru_map_;

    // ── concurrency ──────────────────────────────────────────────────────────
    // mutex_ protects data_, lru_list_, lru_map_ from simultaneous access
    // by the main thread (SET/GET commands) and the background expiry thread.
    // mutable allows locking in const functions like size().
    mutable std::mutex mutex_;

    // background thread that sweeps expired keys every 100ms
    std::thread expiry_thread_;

    // atomic flag — set to false in destructor to signal thread to stop.
    // atomic because it is read by the background thread and written by
    // the main thread simultaneously — no mutex needed for a single bool.
    std::atomic<bool> running_;

    // ── private helpers (all called with mutex_ already held) ────────────────
    bool is_expired(const Entry& entry);
    void touch(const std::string& key);
    void remove(const std::string& key);
    void evict_if_needed();
    void set_entry(const std::string& key, Entry entry);

    // entry point for the background expiry thread
    void active_expiry_loop();
};