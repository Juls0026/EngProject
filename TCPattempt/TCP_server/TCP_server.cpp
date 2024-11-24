#include <iostream>
#include <cstring>
#include <vector>
#include <thread>
#include <mutex>
#include <unistd.h>
#include <arpa/inet.h>
#include <alsa/asoundlib.h>
#include <algorithm> 


#define SAMPLE_RATE 48000
#define CHANNELS 2
#define BUFFER_SIZE 1024  // Frames per buffer
#define PORT 12345

std::vector<int> client_sockets;
std::mutex client_mutex;

// Broadcast audio data to all clients except the sender
void broadcastAudio(const int16_t* buffer, size_t buffer_size, int sender_socket) {
    std::lock_guard<std::mutex> lock(client_mutex);

    for (int client_socket : client_sockets) {
        if (client_socket != sender_socket) {
            send(client_socket, buffer, buffer_size, 0);
        }
    }
}

// Handle an individual client
void handleClient(int client_socket) {
    int16_t buffer[BUFFER_SIZE * CHANNELS];
    ssize_t bytes_received;

    while (true) {
        // Receive data from client
        bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            {
                std::lock_guard<std::mutex> lock(client_mutex);
                auto it = std::find(client_sockets.begin(), client_sockets.end(), client_socket);
                if (it != client_sockets.end()) {
                    client_sockets.erase(it);
                }
            }
            close(client_socket);
            break;
        }

        // If the received data is a timestamp, echo it back for latency measurement
        if (bytes_received == sizeof(uint64_t)) {
            send(client_socket, buffer, bytes_received, 0);
        }

        // Otherwise, broadcast audio data to other clients
        else {
            broadcastAudio(buffer, bytes_received, client_socket);
        }
    }
}



int main() {
    int server_fd, client_socket;
    sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Create server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cerr << "Socket creation failed\n";
        return 1;
    }

    // Bind socket to address and port
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Listen on all network interfaces
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Bind failed\n";
        close(server_fd);
        return 1;
    }

    // Start listening for incoming connections
    if (listen(server_fd, 5) < 0) {
        std::cerr << "Listen failed\n";
        close(server_fd);
        return 1;
    }

    std::cout << "Server is listening on port " << PORT << "...\n";

    // Accept and handle multiple clients
    std::vector<std::thread> threads;

    while (true) {
        client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            std::cerr << "Failed to accept connection\n";
            continue;
        }

        // Extract and log client IP
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "New client connected from IP: " << client_ip 
                  << " and port: " << ntohs(client_addr.sin_port) << "\n";

        // Add client socket to the list
        {
            std::lock_guard<std::mutex> lock(client_mutex);
            client_sockets.push_back(client_socket);
        }

        // Start a thread to handle the new client
        threads.emplace_back(std::thread(handleClient, client_socket));
    }

    // Clean up threads
    for (auto& thread : threads) {
        if (thread.joinable()) thread.join();
    }

    close(server_fd);
    return 0;
}
