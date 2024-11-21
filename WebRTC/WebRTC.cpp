#include <iostream>
#include <vector>
#include <thread>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>
#include <alsa/asoundlib.h>
#include <atomic>
#include <mutex>
#include <algorithm>

#define SAMPLE_RATE 48000
#define CHANNELS 2
#define BUFFER_SIZE 1024
#define UDP_PORT 12345
#define BROADCAST_INTERVAL 5 // Seconds between HELLO broadcasts

struct AudioPacket {
    uint32_t seq_num;           
    int16_t audio_data[BUFFER_SIZE * CHANNELS]; 
};

bool setupALSA(snd_pcm_t* &capture_handle, snd_pcm_t* &playback_handle) {
    if (snd_pcm_open(&capture_handle, "default", SND_PCM_STREAM_CAPTURE, 0) < 0) {
        std::cerr << "Error opening capture device.\n";
        return false;
    }
    if (snd_pcm_open(&playback_handle, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        std::cerr << "Error opening playback device.\n";
        return false;
    }
    snd_pcm_set_params(capture_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, CHANNELS, SAMPLE_RATE, 1, 10000);
    snd_pcm_set_params(playback_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, CHANNELS, SAMPLE_RATE, 1, 10000);
    return true;
}

void captureAndSend(snd_pcm_t* capture_handle, std::vector<sockaddr_in>& peer_addresses, int sockfd, std::atomic<bool>& running, std::mutex& peer_mutex) {
    AudioPacket packet = {};
    uint32_t seq_num = 0;

    while (running) {
        int frames_read = snd_pcm_readi(capture_handle, packet.audio_data, BUFFER_SIZE);
        if (frames_read < 0) {
            std::cerr << "ALSA capture error: " << snd_strerror(frames_read) << "\n";
            if (snd_pcm_prepare(capture_handle) < 0) {
                std::cerr << "Failed to recover ALSA capture device.\n";
                running = false;
                break;
            }
            continue;
        }

        packet.seq_num = seq_num++;

        std::lock_guard<std::mutex> lock(peer_mutex);
        auto peers_copy = peer_addresses;

        for (const auto& peer : peers_copy) {
            ssize_t bytes_sent = sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&peer, sizeof(peer));
            if (bytes_sent < 0) {
                std::cerr << "Error sending audio packet.\n";
            }
        }
    }
}

void receiveAndPlay(snd_pcm_t* playback_handle, int sockfd, std::atomic<bool>& running) {
    AudioPacket packet = {};
    uint32_t last_seq_num = 0;
    int16_t silence[BUFFER_SIZE * CHANNELS] = {0};

    while (running) {
        ssize_t bytes_received = recv(sockfd, &packet, sizeof(packet), 0);
        if (bytes_received < 0) {
            std::cerr << "Error receiving audio packet.\n";
            continue;
        }

        if (packet.seq_num != last_seq_num + 1) {
            uint32_t lost_packets = packet.seq_num - last_seq_num - 1;
            for (uint32_t i = 0; i < lost_packets; ++i) {
                int frames_written = snd_pcm_writei(playback_handle, silence, BUFFER_SIZE);
                if (frames_written < 0) {
                    snd_pcm_prepare(playback_handle);
                }
            }
        }

        int frames_written = snd_pcm_writei(playback_handle, packet.audio_data, BUFFER_SIZE);
        if (frames_written < 0) {
            snd_pcm_prepare(playback_handle);
        }

        last_seq_num = packet.seq_num;
    }
}

void broadcastHello(int sockfd, sockaddr_in& broadcast_addr, std::atomic<bool>& running) {
    const char* message = "HELLO";

    while (running) {
        if (sendto(sockfd, message, strlen(message), 0, (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr)) < 0) {
            std::cerr << "Failed to broadcast HELLO: " << strerror(errno) << "\n";
        }
        std::this_thread::sleep_for(std::chrono::seconds(BROADCAST_INTERVAL));
    }
}

void listenForPeers(int sockfd, std::vector<sockaddr_in>& peer_addresses, std::mutex& peer_mutex, std::atomic<bool>& running) {
    char buffer[1024];
    sockaddr_in peer_addr{};
    socklen_t addr_len = sizeof(peer_addr);

    while (running) {
        ssize_t bytes_received = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&peer_addr, &addr_len);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            if (strcmp(buffer, "HELLO") == 0) {
                std::lock_guard<std::mutex> lock(peer_mutex);
                auto it = std::find_if(peer_addresses.begin(), peer_addresses.end(), [&peer_addr](const sockaddr_in& addr) {
                    return addr.sin_addr.s_addr == peer_addr.sin_addr.s_addr && addr.sin_port == peer_addr.sin_port;
                });

                if (it == peer_addresses.end()) {
                    peer_addresses.push_back(peer_addr);
                    std::cout << "Discovered peer: " << inet_ntoa(peer_addr.sin_addr) << "\n";
                }
            }
        }
    }
}

int main() {
    std::atomic<bool> running(true);
    snd_pcm_t *capture_handle, *playback_handle;

    if (!setupALSA(capture_handle, playback_handle)) {
        return 1;
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "Failed to create socket.\n";
        return 1;
    }

    int broadcast_enable = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(UDP_PORT);
    if (bind(sockfd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        std::cerr << "Failed to bind socket.\n";
        return 1;
    }

    sockaddr_in broadcast_addr{};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(UDP_PORT);
    broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;

    std::vector<sockaddr_in> peer_addresses;
    std::mutex peer_mutex;

    std::thread broadcast_thread(broadcastHello, sockfd, std::ref(broadcast_addr), std::ref(running));
    std::thread peer_thread(listenForPeers, sockfd, std::ref(peer_addresses), std::ref(peer_mutex), std::ref(running));
    std::thread send_thread(captureAndSend, capture_handle, std::ref(peer_addresses), sockfd, std::ref(running), std::ref(peer_mutex));
    std::thread receive_thread(receiveAndPlay, playback_handle, sockfd, std::ref(running));

    std::cout << "Press Enter to stop...\n";
    std::cin.get();
    running = false;

    broadcast_thread.join();
    peer_thread.join();
    send_thread.join();
    receive_thread.join();

    snd_pcm_close(capture_handle);
    snd_pcm_close(playback_handle);
    close(sockfd);

    return 0;
}