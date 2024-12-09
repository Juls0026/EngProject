#include <iostream>
#include <cstring>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>

#define SERVER_IP "192.168.1.83"
#define SERVER_PORT 12345
#define BUFFSIZE 1024
#define RETRIES 5
#define DELAY 2

std::atomic<bool> stop_streaming(false);

// Connect to the server with retries
int connect_to_server(const char* ip, int port, int retries, int delay_seconds) {
    int sockfd;
    struct sockaddr_in server_addr;

    while (retries-- > 0) {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd == -1) {
            perror("Socket creation failed");
            std::this_thread::sleep_for(std::chrono::seconds(delay_seconds));
            continue;
        }

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
            perror("Invalid address or address not supported");
            close(sockfd);
            std::this_thread::sleep_for(std::chrono::seconds(delay_seconds));
            continue;
        }

        if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == 0) {
            return sockfd; // Success
        }

        perror("Connection failed");
        close(sockfd);
        std::this_thread::sleep_for(std::chrono::seconds(delay_seconds));
    }
    return -1; // All retries failed
}

// Send audio data and measure latency and bandwidth
void send_data_with_metrics(int sockfd) {
    char buffer[BUFFSIZE];
    memset(buffer, 'A', sizeof(buffer)); // Dummy data for simplicity

    auto start_time = std::chrono::steady_clock::now();
    int data_sent = 0;

    while (!stop_streaming) {
        auto timestamp = std::chrono::steady_clock::now();
        int64_t ms_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   timestamp.time_since_epoch())
                                   .count();

        // Add timestamp to the beginning of the buffer
        memcpy(buffer, &ms_timestamp, sizeof(ms_timestamp));

        // Send the data
        if (send(sockfd, buffer, sizeof(buffer), 0) == -1) {
            perror("Send failed");
            break;
        }

        data_sent += BUFFSIZE;

        // Simulate acknowledgment for latency calculation
        char ack[8];
        if (recv(sockfd, ack, sizeof(ack), 0) > 0) {
            auto ack_time = std::chrono::steady_clock::now();
            int latency = std::chrono::duration_cast<std::chrono::milliseconds>(ack_time - timestamp).count();
            std::cout << "Latency: " << latency << " ms\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Adjust the sending rate
    }

    auto end_time = std::chrono::steady_clock::now();
    double duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();
    double bandwidth = (data_sent / 1024.0) / duration; // Bandwidth in KB/s

    std::cout << "Average Bandwidth: " << bandwidth << " KB/s\n";
}

int main() {
    signal(SIGPIPE, SIG_IGN); // Ignore SIGPIPE to prevent crashes

    int sockfd = connect_to_server(SERVER_IP, SERVER_PORT, RETRIES, DELAY);
    if (sockfd == -1) {
        std::cerr << "Failed to connect to server.\n";
        return 1;
    }

    std::thread data_thread(send_data_with_metrics, sockfd);
    data_thread.join();

    close(sockfd);
    return 0;
}
