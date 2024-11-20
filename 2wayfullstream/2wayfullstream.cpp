#include <iostream>
#include <vector>
#include <thread>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>
#include <alsa/asoundlib.h>
#include <atomic>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <algorithm>

#define SAMPLE_RATE 48000
#define CHANNELS 2
#define BUFFER_SIZE 1024
#define UDP_PORT 12345
#define BROADCAST_INTERVAL 5 // Seconds between HELLO broadcasts

// Audio packet
struct AudioPacket {
    uint32_t seq_num;
    int16_t audio_data[BUFFER_SIZE * CHANNELS];
};

// Video packet
struct VideoPacket {
    uint32_t seq_num;
    size_t data_size;
    unsigned char data[65536]; // Large enough for compressed frame
};

// Setup ALSA for audio
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

// Capture and send audio
void captureAndSendAudio(snd_pcm_t* capture_handle, std::vector<sockaddr_in>& peer_addresses, int sockfd, std::atomic<bool>& running, std::mutex& peer_mutex) {
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
        for (const auto& peer : peer_addresses) {
            sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&peer, sizeof(peer));
        }
    }
}

// Capture and send video
void captureAndSendVideo(std::vector<sockaddr_in>& peer_addresses, int sockfd, std::atomic<bool>& running, std::mutex& peer_mutex) {
    cv::VideoCapture cap(0);
    if (!cap.isOpened()) {
        std::cerr << "Failed to open video capture.\n";
        running = false;
        return;
    }

    VideoPacket packet = {};
    uint32_t seq_num = 0;
    std::vector<uchar> encoded_frame;

    while (running) {
        cv::Mat frame;
        cap >> frame;
        if (frame.empty()) continue;

        // Encode frame to reduce size
        cv::imencode(".jpg", frame, encoded_frame);
        if (encoded_frame.size() > sizeof(packet.data)) {
            std::cerr << "Encoded frame too large to send.\n";
            continue;
        }

        packet.seq_num = seq_num++;
        packet.data_size = encoded_frame.size();
        std::memcpy(packet.data, encoded_frame.data(), encoded_frame.size());

        std::lock_guard<std::mutex> lock(peer_mutex);
        for (const auto& peer : peer_addresses) {
            sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&peer, sizeof(peer));
        }
    }
}

// Receive and play audio
void receiveAndPlayAudio(snd_pcm_t* playback_handle, int sockfd, std::atomic<bool>& running) {
    AudioPacket packet = {};
    uint32_t last_seq_num = 0;
    int16_t silence[BUFFER_SIZE * CHANNELS] = {0};

    while (running) {
        ssize_t bytes_received = recv(sockfd, &packet, sizeof(packet), 0);
        if (bytes_received < 0) continue;

        if (packet.seq_num != last_seq_num + 1) {
            uint32_t lost_packets = packet.seq_num - last_seq_num - 1;
            for (uint32_t i = 0; i < lost_packets; ++i) {
                snd_pcm_writei(playback_handle, silence, BUFFER_SIZE);
            }
        }

        snd_pcm_writei(playback_handle, packet.audio_data, BUFFER_SIZE);
        last_seq_num = packet.seq_num;
    }
}

// Receive and display video
void receiveAndDisplayVideo(int sockfd, std::atomic<bool>& running) {
    VideoPacket packet = {};
    while (running) {
        ssize_t bytes_received = recv(sockfd, &packet, sizeof(packet), 0);
        if (bytes_received < 0) continue;

        cv::Mat frame = cv::imdecode(cv::Mat(1, packet.data_size, CV_8UC1, packet.data), cv::IMREAD_COLOR);
        if (!frame.empty()) {
            cv::imshow("Video Stream", frame);
            if (cv::waitKey(1) == 27) { // Stop on ESC key
                running = false;
            }
        }
    }
}

int main() {
    std::atomic<bool> running(true);
    snd_pcm_t *capture_handle, *playback_handle;

    if (!setupALSA(capture_handle, playback_handle)) return 1;

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) return 1;

    int broadcast_enable = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

    sockaddr_in local_addr{}, broadcast_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(UDP_PORT);
    bind(sockfd, (struct sockaddr*)&local_addr, sizeof(local_addr));

    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(UDP_PORT);
    broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;

    std::vector<sockaddr_in> peer_addresses;
    std::mutex peer_mutex;

    std::thread audio_send_thread(captureAndSendAudio, capture_handle, std::ref(peer_addresses), sockfd, std::ref(running), std::ref(peer_mutex));
    std::thread video_send_thread(captureAndSendVideo, std::ref(peer_addresses), sockfd, std::ref(running), std::ref(peer_mutex));
    std::thread audio_receive_thread(receiveAndPlayAudio, playback_handle, sockfd, std::ref(running));
    std::thread video_receive_thread(receiveAndDisplayVideo, sockfd, std::ref(running));

    std::cout << "Press Enter to stop...\n";
    std::cin.get();
    running = false;

    audio_send_thread.join();
    video_send_thread.join();
    audio_receive_thread.join();
    video_receive_thread.join();

    snd_pcm_close(capture_handle);
    snd_pcm_close(playback_handle);
    close(sockfd);

    return 0;
}
