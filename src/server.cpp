#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <csignal>
#include <thread>

#include "store.h"
#include "parser.h"
#include "persistence.h"

// ---- Config ----
const int    PORT          = 6379;
const int    MAX_CLIENTS   = 5;       // max queued connections
const int    BUFFER_SIZE   = 4096;
const std::string SNAPSHOT_FILE = "dump.rdb";

// Global store so the signal handler can save it on Ctrl+C
Store g_store;

// ---- Signal handler: save snapshot on Ctrl+C then exit ----
void handle_shutdown(int) {
    std::cout << "\n[server] Shutting down — saving snapshot...\n";
    save_snapshot(g_store, SNAPSHOT_FILE);
    exit(0);
}

// ---- Execute a parsed command against the store, return response string ----
std::string execute(const Command& cmd, Store& store) {
    if (!cmd.valid) {
        return "-ERR " + cmd.error + "\r\n";
    }

    if (cmd.name == "PING") {
        return "+PONG\r\n";
    }

    if (cmd.name == "SET") {
        // SET key value [EX seconds]
        // Check for optional EX flag: SET name rahul EX 60
        if (cmd.args.size() == 4 &&
            (cmd.args[2] == "EX" || cmd.args[2] == "ex")) {
            try {
                int seconds = std::stoi(cmd.args[3]);
                store.set_with_expiry(cmd.args[0], cmd.args[1], seconds);
            } catch (...) {
                return "-ERR EX value must be an integer\r\n";
            }
        } else {
            store.set(cmd.args[0], cmd.args[1]);
        }
        return "+OK\r\n";
    }

    if (cmd.name == "GET") {
        auto val = store.get(cmd.args[0]);
        if (!val.has_value()) return "$-1\r\n";       // nil (key not found)
        return "$" + std::to_string(val->size()) + "\r\n" + *val + "\r\n";
    }

    if (cmd.name == "DEL") {
        bool removed = store.del(cmd.args[0]);
        return removed ? ":1\r\n" : ":0\r\n";         // 1 if deleted, 0 if not found
    }

    if (cmd.name == "EXISTS") {
        return store.exists(cmd.args[0]) ? ":1\r\n" : ":0\r\n";
    }

    if (cmd.name == "EXPIRE") {
        try {
            int seconds = std::stoi(cmd.args[1]);
            bool ok = store.expire(cmd.args[0], seconds);
            return ok ? ":1\r\n" : ":0\r\n";
        } catch (...) {
            return "-ERR seconds must be an integer\r\n";
        }
    }

    if (cmd.name == "QUIT") {
        return "+BYE\r\n";
    }

    if (cmd.name == "DBSIZE") {
        return ":" + std::to_string(store.size()) + "\r\n";
    }

    return "-ERR unknown command\r\n";
}

// ---- Handle one connected client (runs in its own thread later) ----
void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    std::string leftover; // incomplete line from last read

    std::cout << "[server] Client connected (fd=" << client_fd << ")\n";

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

        if (bytes <= 0) {
            // 0 = client disconnected cleanly, -1 = error
            std::cout << "[server] Client disconnected (fd=" << client_fd << ")\n";
            break;
        }

        // Accumulate incoming data — a client might send a partial line
        leftover += std::string(buffer, bytes);

        // Process all complete lines (ending in \n)
        size_t pos;
        while ((pos = leftover.find('\n')) != std::string::npos) {
            std::string line = leftover.substr(0, pos);
            leftover = leftover.substr(pos + 1);

            // Strip \r if present (Windows line endings)
            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (line.empty()) continue;

            std::cout << "[server] Command: " << line << "\n";

            Command cmd = parse(line);
            std::string response = execute(cmd, g_store);

            send(client_fd, response.c_str(), response.size(), 0);

            // QUIT command: send goodbye then close
            if (cmd.name == "QUIT") break;
        }
    }

    close(client_fd);
}

// ---- Main: set up TCP server ----
int main() {
    // Register signal handler so Ctrl+C triggers a clean snapshot save
    signal(SIGINT, handle_shutdown);

    // Load any existing snapshot
    load_snapshot(g_store, SNAPSHOT_FILE);

    // 1. Create the TCP socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    // Allow port reuse immediately after restart (avoids "address already in use" error)
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 2. Define the address: all interfaces, port 6379
    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(PORT);

    // 3. Bind socket to address
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind");
        return 1;
    }

    // 4. Start listening
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        return 1;
    }

    std::cout << "[server] mini-redis listening on port " << PORT << "\n";
    std::cout << "[server] Commands: SET GET DEL EXISTS EXPIRE PING QUIT\n";
    std::cout << "[server] Press Ctrl+C to save and exit\n\n";

    // 5. Accept loop — handle clients one at a time (single-threaded for now)
    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            perror("accept");
            continue;  // don't crash on a bad accept
        }
        // Handle this client (blocking — next client waits until this one disconnects)
        // In multithreaded phase, this becomes: std::thread(handle_client, client_fd).detach();
        handle_client(client_fd);
    }

    close(server_fd);
    return 0;
}