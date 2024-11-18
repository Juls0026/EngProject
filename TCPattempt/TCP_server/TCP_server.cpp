#include <iostream>
#include <cstring>
#include <vector>
#include <thread>
#include <mutex>
#include <unistd.h>
#include <arpa/inet.h>
#include <alsa/asoundlib.h>

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
        bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            {
                std::lock_guard<std::mutex> lock(client_mutex); // Protect vector access
                
                // Debug output
                std::cout << "Removing client_socket: " << client_socket << std::endl;
                for (int sock : client_sockets) {
                    std::cout << "Existing socket: " << sock << std::endl;
                }

                // Use std::find to locate the client_socket in the vector
                auto it = std::find(client_sockets.begin(), client_sockets.end(), client_socket);

                if (it != client_sockets.end()) {
                    client_sockets.erase(it); // Remove the client socket safely
                    std::cout << "Client disconnected and removed: " << client_socket << std::endl;
                } else {
                    std::cerr << "Error: client_socket not found during removal.\n";
                }
            }

            close(client_socket); // Clean up socket
            break;
        }

        // Broadcast received audio to other clients
        broadcastAudio(buffer, bytes_received, client_socket);
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
    server_addr.sin_addr.s_addr = INADDR_ANY;
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

        {
            std::lock_guard<std::mutex> lock(client_mutex);
            client_sockets.push_back(client_socket);
        }

        std::cout << "New client connected: " << client_socket << "\n";

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
