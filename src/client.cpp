#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

const int PORT        = 6379;
const int BUFFER_SIZE = 4096;

int main() {
    // 1. Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    // 2. Specify server address (localhost:6379)
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    // 3. Connect to server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Could not connect to server on port " << PORT << "\n";
        std::cerr << "Is mini-redis-server running?\n";
        return 1;
    }

    std::cout << "Connected to mini-redis on port " << PORT << "\n";
    std::cout << "Type commands (e.g. SET name rahul, GET name, QUIT to exit)\n\n";

    char buffer[BUFFER_SIZE];
    std::string input;

    // 4. REPL: read command → send → print response
    while (true) {
        std::cout << "mini-redis> ";
        std::getline(std::cin, input);

        if (input.empty()) continue;

        // Send command to server (append newline as line delimiter)
        std::string to_send = input + "\n";
        send(sock, to_send.c_str(), to_send.size(), 0);

        // Read response from server
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            std::cout << "Server disconnected.\n";
            break;
        }

        // Parse and pretty-print the response
        std::string resp(buffer, bytes);

        if (resp.empty()) {
            std::cout << "(no response)\n";
        } else if (resp[0] == '+') {
            // +OK or +PONG — simple status
            std::cout << resp.substr(1);
        } else if (resp[0] == '-') {
            // -ERR ... — error message
            std::cout << "(error) " << resp.substr(1);
        } else if (resp[0] == ':') {
            // :1 or :0 — integer response
            std::cout << "(integer) " << resp.substr(1);
        } else if (resp[0] == '$') {
            // $-1 = nil; $N\r\nvalue = bulk string
            if (resp.substr(0, 3) == "$-1") {
                std::cout << "(nil)\n";
            } else {
                // Find the actual value after the length line
                size_t value_start = resp.find("\r\n") + 2;
                std::cout << "\"" << resp.substr(value_start) << "\"\n";
            }
        } else {
            std::cout << resp;
        }

        // QUIT: exit after server confirms
        if (input == "QUIT" || input == "quit") break;
    }

    close(sock);
    return 0;
}
