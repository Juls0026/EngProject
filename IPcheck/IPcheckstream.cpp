#include <iostream>
#include <vector>
#include <thread>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>
#include <set>
#include <mutex>
#include <alsa/asoundlib.h>

#define SAMPLE_RATE 48000
#define CHANNELS 2
#define BUFFER_SIZE 1024  // Size of the audio buffer in frames
#define UDP_PORT 12345
#define BROADCAST_PORT 12346
#define BROADCAST_ADDR "255.255.255.255"  // Broadcast address for local network

std::mutex peer_mutex;  // Mutex to guard access to peer addresses
std::set<std::string> peer_addresses;  // Set of unique peer IP addresses

// Set up ALSA for capturing and playing audio
bool setupALSA(snd_pcm_t* &capture_handle, snd_pcm_t* &playback_handle) {
    // Open capture (input) device
    if (snd_pcm_open(&capture_handle, "default", SND_PCM_STREAM_CAPTURE, 0) < 0) {
        std::cerr << "Error opening capture device.\n";
        return false;
    }
    // Open playback (output) device
    if (snd_pcm_open(&playback_handle, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        std::cerr << "Error opening playback device.\n";
        return false;
    }
    // Configure capture parameters
    snd_pcm_set_params(capture_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, CHANNELS, SAMPLE_RATE, 1, 10000);
    // Configure playback parameters
    snd_pcm_set_params(playback_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, CHANNELS, SAMPLE_RATE, 1, 10000);
    return true;
}

// Send a "hello" broadcast message to discover peers
void broadcastPresence(int broadcast_sock) {
    sockaddr_in broadcast_addr{};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(BROADCAST_PORT);
    inet_pton(AF_INET, BROADCAST_ADDR, &broadcast_addr.sin_addr);

    const char* hello_msg = "HELLO";
    while (true) {
        sendto(broadcast_sock, hello_msg, strlen(hello_msg), 0, (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));
        sleep(1);  // Send hello message every second
    }
}

// Listen for "hello" messages from other peers and add them to peer list
void listenForPeers(int broadcast_sock) {
    char buffer[64];
    sockaddr_in peer_addr{};
    socklen_t addr_len = sizeof(peer_addr);

    while (true) {
        int bytes_received = recvfrom(broadcast_sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&peer_addr, &addr_len);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            std::string peer_ip = inet_ntoa(peer_addr.sin_addr);
            
            // Lock mutex and add peer if not already in the list
            std::lock_guard<std::mutex> lock(peer_mutex);
            if (peer_addresses.find(peer_ip) == peer_addresses.end() && peer_ip != "127.0.0.1") {
                std::cout << "Discovered new peer: " << peer_ip << std::endl;
                peer_addresses.insert(peer_ip);
            }
        }
    }
}

// Function to capture and send audio to all peers
void captureAndSend(snd_pcm_t* capture_handle, int sockfd) {
    int16_t buffer[BUFFER_SIZE * CHANNELS];
    while (true) {
        // Capture audio data from ALSA
        snd_pcm_readi(capture_handle, buffer, BUFFER_SIZE);

        // Lock peer list to ensure thread-safe access
        std::lock_guard<std::mutex> lock(peer_mutex);
        for (const auto &peer_ip : peer_addresses) {
            sockaddr_in peer_addr{};
            peer_addr.sin_family = AF_INET;
            peer_addr.sin_port = htons(UDP_PORT);
            inet_pton(AF_INET, peer_ip.c_str(), &peer_addr.sin_addr);
            sendto(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
        }
    }
}

// Function to receive audio data from peers and play it back
void receiveAndPlay(snd_pcm_t* playback_handle, int sockfd) {
    int16_t buffer[BUFFER_SIZE * CHANNELS];
    while (true) {
        recv(sockfd, buffer, sizeof(buffer), 0);
        snd_pcm_writei(playback_handle, buffer, BUFFER_SIZE);
    }
}

int main() {
    // ALSA handles for capture and playback
    snd_pcm_t *capture_handle, *playback_handle;
    if (!setupALSA(capture_handle, playback_handle)) {
        std::cerr << "Failed to set up ALSA.\n";
        return 1;
    }

    // Create UDP socket for audio communication
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "Failed to create socket.\n";
        return 1;
    }
    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(UDP_PORT);
    if (bind(sockfd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        std::cerr << "Failed to bind socket.\n";
        return 1;
    }

    // Create UDP socket for broadcast communication
    int broadcast_sock = socket(AF_INET, SOCK_DGRAM, 0);
    int broadcast_enable = 1;
    setsockopt(broadcast_sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

    sockaddr_in broadcast_addr{};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_addr.s_addr = INADDR_ANY;
    broadcast_addr.sin_port = htons(BROADCAST_PORT);
    bind(broadcast_sock, (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));

    // Start threads for broadcasting, listening, capturing, and receiving
    std::thread broadcast_thread(broadcastPresence, broadcast_sock);
    std::thread listen_thread(listenForPeers, broadcast_sock);
    std::thread send_thread(captureAndSend, capture_handle, sockfd);
    std::thread receive_thread(receiveAndPlay, playback_handle, sockfd);

    broadcast_thread.detach();
    listen_thread.detach();
    send_thread.join();
    receive_thread.join();

    // Clean up
    snd_pcm_close(capture_handle);
    snd_pcm_close(playback_handle);
    close(sockfd);
    close(broadcast_sock);

    return 0;
}
