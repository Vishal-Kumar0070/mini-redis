#pragma once
#include <string>
#include <unordered_map>
#include <list>
#include <chrono>
#include <optional>

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

    void set(const std::string& key, const std::string& value);
    void set_with_expiry(const std::string& key, const std::string& value, int seconds);
    std::optional<std::string> get(const std::string& key);
    bool del(const std::string& key);
    bool exists(const std::string& key);
    bool expire(const std::string& key, int seconds);
    std::unordered_map<std::string, Entry> get_all();
    void load(const std::unordered_map<std::string, Entry>& data);

    // How many keys are currently stored
    int size() const { return (int)data_.size(); }

private:
    int capacity_;  // max keys allowed

    // The actual data: key → Entry
    std::unordered_map<std::string, Entry> data_;

    // ---- LRU tracking ----
    // A doubly linked list of keys, front = most recently used
    // std::list gives us O(1) insert/erase at any position
    std::list<std::string> lru_list_;

    // Maps each key → its position (iterator) in lru_list_
    // Storing the iterator lets us jump directly to any node = O(1) move
    std::unordered_map<std::string, std::list<std::string>::iterator> lru_map_;

    // ---- Private helpers ----
    bool is_expired(const Entry& entry);

    // Call this every time a key is accessed (GET) or written (SET)
    // Moves the key to the front of lru_list_
    void touch(const std::string& key);

    // Remove a key from both data_ and the LRU structures
    void remove(const std::string& key);

    // If over capacity, evict the least recently used key (back of list)
    void evict_if_needed();

    // Internal set used by both set() and set_with_expiry()
    void set_entry(const std::string& key, Entry entry);
};