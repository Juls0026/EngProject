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
#include <rtc/rtc.hpp>
#include <iostream>
#include <thread>
#include <opencv2/opencv.hpp>
#include <asio.hpp>


#define SAMPLE_RATE 48000
#define CHANNELS 2
#define BUFFER_SIZE 1024
#define UDP_PORT 12345
#define BROADCAST_INTERVAL 5 // Seconds between HELLO broadcasts

struct AudioPacket {
    uint32_t seq_num;           
    int16_t audio_data[BUFFER_SIZE * CHANNELS]; 
};

void startSignaling(rtc::PeerConnection& pc, const std::string& signalingServerUrl) {
    asio::io_context ioContext;
    asio::ip::tcp::resolver resolver(ioContext);
    auto endpoints = resolver.resolve(signalingServerUrl, "8765");
    asio::ip::tcp::socket socket(ioContext);
    asio::connect(socket, endpoints);

    std::thread signalingThread([&]() {
        while (true) {
            char buffer[1024];
            size_t length = socket.read_some(asio::buffer(buffer));
            std::string message(buffer, length);

            if (message.find("sdp") != std::string::npos) {
                rtc::Description desc(message, "offer");
                pc.setRemoteDescription(desc);
            } else if (message.find("candidate") != std::string::npos) {
                rtc::Candidate candidate(message);
                pc.addRemoteCandidate(candidate);
            }
        }
    });

    pc.onLocalDescription([&](rtc::Description desc) {
        std::string sdpMessage = desc.toSdp();  // Use toSdp() method here
        asio::write(socket, asio::buffer(sdpMessage));
    });

    pc.onLocalCandidate([&](rtc::Candidate candidate) {
        std::string iceMessage = candidate.toString();  // Use toString() method here
        asio::write(socket, asio::buffer(iceMessage));
    });

    signalingThread.detach();
}




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

void captureAndSendAudio(snd_pcm_t* capture_handle, std::shared_ptr<rtc::Track> audioTrack, std::atomic<bool>& running) {
    int16_t buffer[BUFFER_SIZE * CHANNELS];

    while (running) {
        int frames_read = snd_pcm_readi(capture_handle, buffer, BUFFER_SIZE);
        if (frames_read < 0) {
            std::cerr << "ALSA capture error: " << snd_strerror(frames_read) << "\n";
            continue;
        }

        audioTrack->send(reinterpret_cast<const std::byte*>(buffer), frames_read * CHANNELS * sizeof(int16_t));
    }
}


void captureAndSendVideo(std::shared_ptr<rtc::Track> videoTrack, std::atomic<bool>& running) {
    cv::VideoCapture cap(0); // Open the default camera
    if (!cap.isOpened()) {
        std::cerr << "Failed to open camera.\n";
        return;
    }

    cv::Mat frame;
    while (running) {
        cap.read(frame);
        if (frame.empty()) continue;

        // Resize frame for WebRTC
        cv::resize(frame, frame, cv::Size(640, 480));

        rtc::binary videoData(reinterpret_cast<const std::byte*>(frame.data),
                              reinterpret_cast<const std::byte*>(frame.data) + frame.total() * frame.elemSize());
        videoTrack->send(videoData);

        std::this_thread::sleep_for(std::chrono::milliseconds(30)); // Approx 30 FPS
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
    snd_pcm_t* capture_handle;
    snd_pcm_t* playback_handle = nullptr;  // Add playback handle for setupALSA

    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");

    // Create PeerConnection
    rtc::PeerConnection pc(config);
    auto audioTrack = pc.addTrack(rtc::Description::Media::Audio);  // Use Media::Audio
    auto videoTrack = pc.addTrack(rtc::Description::Media::Video);  // Use Media::Video

    // Start signaling
    startSignaling(pc, "localhost");

    // Setup ALSA for audio capture
    if (!setupALSA(capture_handle, playback_handle)) {
        return 1;
    }

    // Start threads for audio and video
    std::thread audioThread(captureAndSendAudio, capture_handle, audioTrack, std::ref(running));
    std::thread videoThread(captureAndSendVideo, videoTrack, std::ref(running));

    std::cout << "Press Enter to stop...\n";
    std::cin.get();
    running = false;

    audioThread.join();
    videoThread.join();
    snd_pcm_close(capture_handle);
    snd_pcm_close(playback_handle);

    return 0;
}