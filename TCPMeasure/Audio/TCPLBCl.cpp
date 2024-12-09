// Include Libraries
#include <iostream>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <alsa/asoundlib.h>

#define SERVER_IP "192.168.1.83"
#define SERVER_PORT 12345
#define BUFFSIZE 4096
#define SRATE 44100
#define CHANNELS 2
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
void send_audio_with_metrics(int sockfd) {
    snd_pcm_t* capture_handle;
    snd_pcm_hw_params_t* hw_params;
    char buffer[BUFFSIZE];
    int err;

    // Initialize ALSA for audio capture
    if ((err = snd_pcm_open(&capture_handle, "default", SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        std::cerr << "Capture open error: " << snd_strerror(err) << std::endl;
        return;
    }

    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(capture_handle, hw_params);
    snd_pcm_hw_params_set_access(capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(capture_handle, hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate(capture_handle, hw_params, SRATE, 0);
    snd_pcm_hw_params_set_channels(capture_handle, hw_params, CHANNELS);
    snd_pcm_hw_params(capture_handle, hw_params);

    int64_t total_bytes_sent = 0;
    auto start_time = std::chrono::steady_clock::now();

    while (!stop_streaming) {
        int frames = snd_pcm_readi(capture_handle, buffer, BUFFSIZE / (CHANNELS * 2));
        if (frames < 0) {
            frames = snd_pcm_recover(capture_handle, frames, 0);
            if (frames < 0) {
                std::cerr << "Audio capture error: " << snd_strerror(frames) << std::endl;
                break;
            }
        }

        int byte_count = frames * CHANNELS * 2;

        // Add timestamp for latency measurement
        auto timestamp = std::chrono::steady_clock::now();
        int64_t ms_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    timestamp.time_since_epoch())
                                    .count();

        // Prepare packet: timestamp + audio data
        std::vector<char> packet(sizeof(ms_timestamp) + byte_count);
        memcpy(packet.data(), &ms_timestamp, sizeof(ms_timestamp));
        memcpy(packet.data() + sizeof(ms_timestamp), buffer, byte_count);

        // Send packet
        if (send(sockfd, packet.data(), packet.size(), 0) == -1) {
            perror("Send failed");
            break;
        }

        total_bytes_sent += byte_count;

        // Receive acknowledgment for latency measurement
        char ack[8];
        if (recv(sockfd, ack, sizeof(ack), 0) > 0) {
            auto ack_time = std::chrono::steady_clock::now();
            int latency = std::chrono::duration_cast<std::chrono::milliseconds>(ack_time - timestamp).count();
            std::cout << "Latency: " << latency << " ms\n";
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    double duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();
    double bandwidth = (total_bytes_sent / 1024.0) / duration; // Bandwidth in KB/s

    std::cout << "Average Bandwidth: " << bandwidth << " KB/s\n";

    snd_pcm_close(capture_handle);
}

// Receive and playback audio
void receive_and_play_audio(int sockfd) {
    snd_pcm_t* playback_handle;
    snd_pcm_hw_params_t* hw_params;
    char buffer[BUFFSIZE];
    int err;

    // Initialize ALSA for playback
    if ((err = snd_pcm_open(&playback_handle, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        std::cerr << "Playback open error: " << snd_strerror(err) << std::endl;
        return;
    }

    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(playback_handle, hw_params);
    snd_pcm_hw_params_set_access(playback_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(playback_handle, hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate(playback_handle, hw_params, SRATE, 0);
    snd_pcm_hw_params_set_channels(playback_handle, hw_params, CHANNELS);
    snd_pcm_hw_params(playback_handle, hw_params);

    while (!stop_streaming) {
        int bytes_received = recv(sockfd, buffer, BUFFSIZE, 0);
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                std::cout << "Server closed connection.\n";
            } else {
                perror("Receive error");
            }
            break;
        }

        int frames = bytes_received / (CHANNELS * 2);
        if ((err = snd_pcm_writei(playback_handle, buffer, frames)) < 0) {
            snd_pcm_recover(playback_handle, err, 0);
        }
    }

    snd_pcm_close(playback_handle);
}

int main() {
    signal(SIGPIPE, SIG_IGN); // Ignore SIGPIPE to prevent crashes

    int sockfd = connect_to_server(SERVER_IP, SERVER_PORT, RETRIES, DELAY);
    if (sockfd == -1) {
        std::cerr << "Failed to connect to server.\n";
        return 1;
    }

    std::thread sender_thread(send_audio_with_metrics, sockfd);
    std::thread receiver_thread(receive_and_play_audio, sockfd);

    sender_thread.join();
    receiver_thread.join();

    close(sockfd);
    return 0;
}
