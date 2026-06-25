#include "persistence.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <chrono>

// File format (one key per line):
//   KEY <key> <value>             (no expiry)
//   EXPKEY <key> <value> <unix_ms_expiry>  (has expiry)

void save_snapshot(Store& store, const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[persistence] Could not open file for writing: " << filepath << "\n";
        return;
    }

    auto all = store.get_all();
    for (auto& [key, entry] : all) {
        if (!entry.has_expiry) {
            file << "KEY " << key << " " << entry.value << "\n";
        } else {
            // Convert expiry time to milliseconds since epoch so we can store it
            auto expiry_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                entry.expires_at.time_since_epoch()
            ).count();
            file << "EXPKEY " << key << " " << entry.value << " " << expiry_ms << "\n";
        }
    }

    std::cout << "[persistence] Snapshot saved to " << filepath << " (" << all.size() << " keys)\n";
}

void load_snapshot(Store& store, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        // Not an error — just means no snapshot exists yet (first run)
        std::cout << "[persistence] No snapshot found at " << filepath << " — starting fresh\n";
        return;
    }

    std::unordered_map<std::string, Entry> data;
    std::string line;
    int count = 0;

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string type, key, value;
        iss >> type >> key >> value;

        if (type == "KEY") {
            data[key] = Entry{value, false, {}};
            count++;
        } else if (type == "EXPKEY") {
            long long expiry_ms;
            iss >> expiry_ms;
            // Reconstruct the time_point from stored milliseconds
            auto expiry_tp = std::chrono::steady_clock::time_point(
                std::chrono::milliseconds(expiry_ms)
            );
            // Only load if it hasn't already expired
            if (expiry_tp > std::chrono::steady_clock::now()) {
                data[key] = Entry{value, true, expiry_tp};
                count++;
            }
        }
    }

    store.load(data);
    std::cout << "[persistence] Loaded " << count << " keys from " << filepath << "\n";
}
