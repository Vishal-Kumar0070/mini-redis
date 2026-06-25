#pragma once
#include <string>
#include <vector>

struct Command {
    std::string name;              // e.g. "SET", "GET", "DEL"
    std::vector<std::string> args; // e.g. ["username", "rahul"]
    bool valid = true;             // false if input was garbage
    std::string error;             // reason if invalid
};

// Takes a raw string like "SET name rahul" and returns a Command struct
Command parse(const std::string& raw);
