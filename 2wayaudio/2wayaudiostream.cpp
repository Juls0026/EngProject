#include <iostream>
#include <vector>
#include <thread>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>
#include <alsa/asoundlib.h>
#include <set> // To store unique peer addresses


#define SAMPLE_RATE 48000
#define CHANNELS 2
#define BUFFER_SIZE 1024  // Size of the audio buffer in frames
#define UDP_PORT 12345

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

void captureAndSend(snd_pcm_t* capture_handle, const std::vector<sockaddr_in>& peer_addresses, int sockfd) {
    int16_t buffer[BUFFER_SIZE * CHANNELS];
    while (true) {
        ssize_t frames = snd_pcm_readi(capture_handle, buffer, BUFFER_SIZE);
        if (frames < 0) {
            std::cerr << "Audio capture error: " << snd_strerror(frames) << "\n";
            snd_pcm_prepare(capture_handle);
            continue;
        }
        for (const auto& peer : peer_addresses) {
            ssize_t sent_bytes = sendto(sockfd, buffer, frames * sizeof(int16_t) * CHANNELS, 0, (struct sockaddr*)&peer, sizeof(peer));
            if (sent_bytes < 0) {
                std::cerr << "sendto error: " << strerror(errno) << "\n";
            }
        }
    }
}


// Function to receive audio data from peers and play it back
void receiveAndPlay(snd_pcm_t* playback_handle, int sockfd) {
    int16_t buffer[BUFFER_SIZE * CHANNELS];
    sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    while (true) {
        ssize_t received_bytes = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)&sender_addr, &sender_len);
        if (received_bytes > 0) {
            ssize_t frames = snd_pcm_writei(playback_handle, buffer, BUFFER_SIZE);
            if (frames < 0) {
                std::cerr << "Audio playback error: " << snd_strerror(frames) << "\n";
                snd_pcm_prepare(playback_handle);
            }
        } else if (received_bytes < 0) {
            std::cerr << "recvfrom error: " << strerror(errno) << "\n";
        }
    }
}




// Broadcast "HELLO" message
void broadcastHello(int sockfd, int broadcast_port) {
    sockaddr_in broadcast_addr{};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(broadcast_port);
    broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;

    const char* message = "HELLO";
    sendto(sockfd, message, strlen(message), 0, (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));
}

// Listen for responses from peers
void listenForPeers(int sockfd, std::set<std::string>& discovered_peers, int timeout_seconds = 10) {
    sockaddr_in sender_addr{};
    socklen_t addr_len = sizeof(sender_addr);
    char buffer[1024];
    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        ssize_t received_bytes = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)&sender_addr, &addr_len);
        if (received_bytes > 0) {
            buffer[received_bytes] = '\0'; // Null-terminate
            std::string sender_ip = inet_ntoa(sender_addr.sin_addr);
            if (discovered_peers.find(sender_ip) == discovered_peers.end()) {
                discovered_peers.insert(sender_ip);
                std::cout << "Discovered peer: " << sender_ip << "\n";
            }
        }

        // Exit after timeout
        if (std::chrono::steady_clock::now() - start_time > std::chrono::seconds(timeout_seconds)) {
            std::cout << "Peer discovery finished.\n";
            break;
        }
    }
}


int main() {
    // ALSA handles for capture and playback
    snd_pcm_t* capture_handle;
    snd_pcm_t* playback_handle;
    if (!setupALSA(capture_handle, playback_handle)) {
        std::cerr << "Failed to set up ALSA.\n";
        return 1;
    }

    // Create UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "Failed to create socket.\n";
        return 1;
    }

    // Bind socket to listen on UDP_PORT
    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(UDP_PORT);
    if (bind(sockfd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        std::cerr << "Failed to bind socket.\n";
        return 1;
    }

    // Discover peers
    std::set<std::string> discovered_peers;
    listenForPeers(sockfd, discovered_peers);

    // Convert discovered peers to sockaddr_in
    std::vector<sockaddr_in> peer_addresses;
    for (const auto& peer_ip : discovered_peers) {
        sockaddr_in peer_addr{};
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_port = htons(UDP_PORT);
        inet_pton(AF_INET, peer_ip.c_str(), &peer_addr.sin_addr);
        peer_addresses.push_back(peer_addr);
    }

    if (peer_addresses.empty()) {
        std::cerr << "No peers discovered. Exiting.\n";
        return 1;
    }

    std::cout << "Peers discovered. Starting audio streaming...\n";

    // Start threads for capturing/sending and receiving/playing
    std::thread send_thread(captureAndSend, capture_handle, std::ref(peer_addresses), sockfd);
    std::thread receive_thread(receiveAndPlay, playback_handle, sockfd);

    // Wait for threads to complete (if ever)
    send_thread.join();
    receive_thread.join();

    // Clean up
    snd_pcm_close(capture_handle);
    snd_pcm_close(playback_handle);
    close(sockfd);

    return 0;
}
