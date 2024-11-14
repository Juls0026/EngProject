#include <iostream>
#include <vector>
#include <thread>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>
#include <alsa/asoundlib.h>

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

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " peer_ip_1 peer_ip_2 ... peer_ip_n\n";
        return 1;
    }

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

    // Bind socket to listen on UDP_PORT
    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(UDP_PORT);
    if (bind(sockfd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        std::cerr << "Failed to bind socket.\n";
        return 1;
    }

    // Set up peer addresses from command-line arguments
    std::vector<sockaddr_in> peer_addresses;
    for (int i = 1; i < argc; ++i) {
        sockaddr_in peer_addr{};
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_port = htons(UDP_PORT);
        inet_pton(AF_INET, argv[i], &peer_addr.sin_addr);
        peer_addresses.push_back(peer_addr);
    }

    // Start threads for capturing/sending and receiving/playing
    std::thread send_thread(captureAndSend, capture_handle, std::ref(peer_addresses), sockfd);
    std::thread receive_thread(receiveAndPlay, playback_handle, sockfd);

    send_thread.join();
    receive_thread.join();

    // Clean up
    snd_pcm_close(capture_handle);
    snd_pcm_close(playback_handle);
    close(sockfd);

    return 0;
}
