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

// Function to capture and send audio to all peers
void captureAndSend(snd_pcm_t* capture_handle, const std::vector<sockaddr_in> &peer_addresses, int sockfd) {
    int16_t buffer[BUFFER_SIZE * CHANNELS];
    while (true) {
        // Capture audio data from ALSA
        snd_pcm_readi(capture_handle, buffer, BUFFER_SIZE);
        // Send the captured audio data to each peer
        for (const auto &peer : peer_addresses) {
            sendto(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)&peer, sizeof(peer));
        }
    }
}

// Function to receive audio data from peers and play it back
void receiveAndPlay(snd_pcm_t* playback_handle, int sockfd) {
    int16_t buffer[BUFFER_SIZE * CHANNELS];
    while (true) {
        // Receive audio data from any peer
        recv(sockfd, buffer, sizeof(buffer), 0);
        // Play received audio data
        snd_pcm_writei(playback_handle, buffer, BUFFER_SIZE);
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
void listenForPeers(int sockfd, std::set<std::string>& discovered_peers) {
    char buffer[1024];
    sockaddr_in peer_addr{};
    socklen_t addr_len = sizeof(peer_addr);

    while (true) {
        ssize_t bytes_received = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&peer_addr, &addr_len);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            std::string peer_ip = inet_ntoa(peer_addr.sin_addr);
            discovered_peers.insert(peer_ip);
            std::cout << "Discovered peer: " << peer_ip << "\n";
        }
    }
}

int main(int argc, char* argv[]) {
    // ALSA handles for capture and playback
    snd_pcm_t *capture_handle, *playback_handle;
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

    // Enable broadcasting on the socket
    int broadcast_enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        std::cerr << "Failed to enable broadcast.\n";
        return 1;
    }

    // Bind socket to listen for responses
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
    std::thread discovery_thread(listenForPeers, sockfd, std::ref(discovered_peers));

    // Broadcast HELLO message
    broadcastHello(sockfd, UDP_PORT);

    // Wait a few seconds for responses
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Convert discovered peers into sockaddr_in structures
    std::vector<sockaddr_in> peer_addresses;
    for (const auto& peer_ip : discovered_peers) {
        sockaddr_in peer_addr{};
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_port = htons(UDP_PORT);
        inet_pton(AF_INET, peer_ip.c_str(), &peer_addr.sin_addr);
        peer_addresses.push_back(peer_addr);
    }

    // Start threads for capturing/sending and receiving/playing
    std::thread send_thread(captureAndSend, capture_handle, std::ref(peer_addresses), sockfd);
    std::thread receive_thread(receiveAndPlay, playback_handle, sockfd);

    send_thread.join();
    receive_thread.join();

    // Clean up
    discovery_thread.detach(); // Allow discovery thread to exit
    snd_pcm_close(capture_handle);
    snd_pcm_close(playback_handle);
    close(sockfd);

    return 0;
}
