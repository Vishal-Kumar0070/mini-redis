#include "parser.h"
#include <sstream>
#include <algorithm>

Command parse(const std::string& raw) {
    Command cmd;

    // Split the raw string by spaces into tokens
    std::istringstream iss(raw);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }

    if (tokens.empty()) {
        cmd.valid = false;
        cmd.error = "empty command";
        return cmd;
    }

    // Command name is always the first token, uppercase it
    cmd.name = tokens[0];
    std::transform(cmd.name.begin(), cmd.name.end(), cmd.name.begin(), ::toupper);

    // Everything after the command name is arguments
    cmd.args = std::vector<std::string>(tokens.begin() + 1, tokens.end());

    // Validate: check each command has the right number of arguments
    if (cmd.name == "GET" || cmd.name == "DEL" || cmd.name == "EXISTS") {
        if (cmd.args.size() != 1) {
            cmd.valid = false;
            cmd.error = cmd.name + " requires exactly 1 argument (key)";
        }
    } else if (cmd.name == "SET") {
        if (cmd.args.size() < 2) {
            cmd.valid = false;
            cmd.error = "SET requires at least 2 arguments: SET key value";
        }
    } else if (cmd.name == "EXPIRE") {
        if (cmd.args.size() != 2) {
            cmd.valid = false;
            cmd.error = "EXPIRE requires 2 arguments: EXPIRE key seconds";
        }
    } else if (cmd.name == "PING" || cmd.name == "QUIT" || cmd.name == "DBSIZE") {
        // no args needed
    } else {
        cmd.valid = false;
        cmd.error = "unknown command: " + cmd.name;
    }

    return cmd;
}