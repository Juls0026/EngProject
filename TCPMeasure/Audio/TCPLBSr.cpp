// Include Libraries
#include <iostream>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <algorithm> 


#define SERVER_PORT 12345
#define BACKLOG 5
#define BUFFSIZE 4096

std::vector<int> clients; // Vector to store connected clients
std::mutex client_m;      // Mutex for thread-safe access to the clients vector
std::atomic<bool> stop_server(false);

// Handle client connection
#include <algorithm> // For std::remove
#include <sys/wait.h> // For waitpid

void handle_client(int client_socket) {
    char buffer[BUFFSIZE];
    int byte_num;

    while ((byte_num = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
        // Broadcast message to all clients
        std::lock_guard<std::mutex> lock(client_m); // Lock clients while broadcasting
        for (int other_client : clients) {
            if (other_client != client_socket) {
                if (send(other_client, buffer, byte_num, 0) == -1) {
                    perror("send error");
                }
            }
        }
    }

    // Handle client disconnection or receive error
    if (byte_num == 0) {
        std::cout << "Client disconnected.\n";
    } else if (byte_num == -1) {
        perror("recv error");
    }

    // Remove client from the list
    {
        std::lock_guard<std::mutex> lock(client_m);
        clients.erase(std::remove(clients.begin(), clients.end(), client_socket), clients.end());
    }

    close(client_socket); // Close the client socket
}

void sigchld_handler(int s) {
    // Save and restore errno because waitpid might overwrite it
    int saved_errno = errno;

    while (waitpid(-1, nullptr, WNOHANG) > 0) {
        // Reap zombie processes
    }

    errno = saved_errno;
}


int main() {
    signal(SIGPIPE, SIG_IGN); // Ignore SIGPIPE to prevent crashes

    int server_socket, new_client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t sin_size = sizeof(client_addr);

    // Create server socket
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
        return 1;
    }

    // Allow address reuse
    int yes = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        perror("Set socket options failed");
        return 1;
    }

    // Set up server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind socket to the address
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Bind failed");
        close(server_socket);
        return 1;
    }

    // Start listening for incoming connections
    if (listen(server_socket, BACKLOG) == -1) {
        perror("Listen failed");
        close(server_socket);
        return 1;
    }

    std::cout << "Server listening on port " << SERVER_PORT << "...\n";

    // Set up signal handler for cleaning up zombie processes
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, nullptr) == -1) {
        perror("Sigaction setup failed");
        return 1;
    }

    // Accept incoming connections
    while (!stop_server) {
        new_client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &sin_size);
        if (new_client_socket == -1) {
            perror("Accept failed");
            continue;
        }

        // Display the client address
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        std::cout << "Connection from " << client_ip << "\n";

        // Add client to the list
        {
            std::lock_guard<std::mutex> lock(client_m);
            clients.push_back(new_client_socket);
        }

        // Create a thread to handle the client
        std::thread client_thread(handle_client, new_client_socket);
        client_thread.detach();
    }

    // Clean up
    close(server_socket);
    return 0;
}
