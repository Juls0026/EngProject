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
#include <chrono> 


#define SAMPLE_RATE 48000
#define CHANNELS 2
#define BUFFER_SIZE 1024
#define UDP_PORT 12345
#define BROADCAST_INTERVAL 5 // Seconds between HELLO broadcasts

// Packet Structures

// Audio packet structure
struct AudioPacket {
    uint32_t seq_num;
    uint64_t timestamp; // Add a timestamp
    int16_t audio_data[BUFFER_SIZE * CHANNELS];
};

// Video packet structure
struct VideoPacket {
    uint32_t seq_num;
    uint64_t timestamp; // Add a timestamp
    size_t data_size;
    unsigned char data[65536];
};
// Helper function to get the current timestamp in microseconds
uint64_t getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}


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
    // Open the video capture with device 0 (typically the default webcam)
    cv::VideoCapture cap(0, cv::CAP_V4L2);
    if (!cap.isOpened()) {
        std::cerr << "Failed to open video capture.\n";
        running = false;
        return;
    }

    // Set capture properties to reduce frame resolution and ensure a consistent frame rate
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 320);  // Set to a lower resolution to reduce size
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 240); // Set to a lower resolution to reduce size
    cap.set(cv::CAP_PROP_FPS, 30);           // Set frame rate to 30 frames per second

    VideoPacket packet = {};
    uint32_t seq_num = 0;

    while (running) {
        cv::Mat frame;
        if (!cap.read(frame)) {
            std::cerr << "Failed to capture video frame.\n";
            continue; // Skip this iteration if capturing fails
        }

        if (frame.empty()) continue;

        // Encode frame with reduced quality to make it smaller
        std::vector<uchar> encoded_frame;
        std::vector<int> compression_params = {cv::IMWRITE_JPEG_QUALITY, 50}; // Set JPEG quality to 50 (reduce size)
        
        if (!cv::imencode(".jpg", frame, encoded_frame, compression_params)) {
            std::cerr << "Failed to encode video frame.\n";
            continue; // Skip this frame if encoding fails
        }

        // Check if encoded frame exceeds the size limit and skip sending if too large
        if (encoded_frame.size() > sizeof(packet.data)) {
            std::cerr << "Encoded frame too large to send. Size: " << encoded_frame.size() << " bytes\n";
            continue; // Skip to the next iteration if the frame is too large
        }

        // Fill the packet details
        packet.seq_num = seq_num++;
        packet.data_size = encoded_frame.size();
        std::memcpy(packet.data, encoded_frame.data(), encoded_frame.size());

        // Lock the mutex to safely access the peer addresses
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
    cv::namedWindow("Video Stream", cv::WINDOW_AUTOSIZE); // Create a named window to display video

    while (running) {
        // Receive a video packet
        ssize_t bytes_received = recv(sockfd, &packet, sizeof(packet), 0);
        if (bytes_received < 0) {
            std::cerr << "Failed to receive video packet.\n";
            continue;
        }

        // Decode the received packet into a frame
        cv::Mat frame = cv::imdecode(cv::Mat(1, packet.data_size, CV_8UC1, packet.data), cv::IMREAD_COLOR);
        if (frame.empty()) {
            std::cerr << "Failed to decode video frame.\n";
            continue; // Skip to the next packet if the frame is empty
        }

        // Display the frame
        cv::imshow("Video Stream", frame);

        // Wait for 1 millisecond and allow a key press (to stop using ESC key)
        if (cv::waitKey(1) == 27) { // Press 'ESC' key to stop
            running = false;
        }
    }

    cv::destroyWindow("Video Stream"); // Destroy the window when finished
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
                peer_addresses.push_back(peer_addr);
            }
        }
    }
}


int main() {
    // Main function implementation
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

    // Start threads for audio, video, and peer discovery
    std::thread audio_send_thread(captureAndSendAudio, capture_handle, std::ref(peer_addresses), sockfd, std::ref(running), std::ref(peer_mutex));
    std::thread video_send_thread(captureAndSendVideo, std::ref(peer_addresses), sockfd, std::ref(running), std::ref(peer_mutex));
    std::thread audio_receive_thread(receiveAndPlayAudio, playback_handle, sockfd, std::ref(running));
    std::thread video_receive_thread(receiveAndDisplayVideo, sockfd, std::ref(running));

    std::cout << "Press Enter to stop...\n";
    std::cin.get();
    running = false;

    // Join threads and clean up resources
    audio_send_thread.join();
    video_send_thread.join();
    audio_receive_thread.join();
    video_receive_thread.join();

    snd_pcm_close(capture_handle);
    snd_pcm_close(playback_handle);
    close(sockfd);

    return 0;
}
