#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT 12345
#define BUFFSIZE 1024

void handle_client(int client_sock) {
    char buffer[BUFFSIZE];
    int64_t prev_timestamp = 0;
    int total_bytes = 0;

    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        int bytes_received = recv(client_sock, buffer, BUFFSIZE, 0);
        if (bytes_received <= 0) {
            break; // Connection closed or error
        }

        // Extract timestamp from the received buffer
        int64_t client_timestamp;
        memcpy(&client_timestamp, buffer, sizeof(client_timestamp));

        // Send acknowledgment with the received timestamp
        if (send(client_sock, &client_timestamp, sizeof(client_timestamp), 0) == -1) {
            perror("Send acknowledgment failed");
            break;
        }

        total_bytes += bytes_received;

        // Log received data size and calculate bandwidth
        auto current_time = std::chrono::steady_clock::now();
        double elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();
        double bandwidth = (total_bytes / 1024.0) / elapsed_time; // Bandwidth in KB/s
        std::cout << "Bandwidth: " << bandwidth << " KB/s\n";
    }

    close(client_sock);
}

int main() {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1) {
        perror("Socket creation failed");
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Bind failed");
        close(server_sock);
        return 1;
    }

    if (listen(server_sock, 5) == -1) {
        perror("Listen failed");
        close(server_sock);
        return 1;
    }

    std::cout << "Server listening on port " << PORT << "\n";

    while (true) {
        int client_sock = accept(server_sock, nullptr, nullptr);
        if (client_sock == -1) {
            perror("Accept failed");
            continue;
        }

        std::cout << "Client connected.\n";
        std::thread(handle_client, client_sock).detach();
    }

    close(server_sock);
    return 0;
}

