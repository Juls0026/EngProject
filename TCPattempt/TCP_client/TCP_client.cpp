#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <thread>
#include <alsa/asoundlib.h>

#define SAMPLE_RATE 48000
#define CHANNELS 2
#define BUFFER_SIZE 1024  // Frames per buffer
#define PORT 12345

// ALSA setup for capture
bool setupALSACapture(snd_pcm_t*& capture_handle) {
    if (snd_pcm_open(&capture_handle, "default", SND_PCM_STREAM_CAPTURE, 0) < 0) {
        std::cerr << "Error opening ALSA capture device.\n";
        return false;
    }

    if (snd_pcm_set_params(capture_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, CHANNELS, SAMPLE_RATE, 1, 100000) < 0) {
        std::cerr << "Error setting ALSA capture parameters.\n";
        return false;
    }

    return true;
}

// ALSA setup for playback
bool setupALSAPlayback(snd_pcm_t*& playback_handle) {
    if (snd_pcm_open(&playback_handle, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        std::cerr << "Error opening ALSA playback device.\n";
        return false;
    }

    if (snd_pcm_set_params(playback_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, CHANNELS, SAMPLE_RATE, 1, 100000) < 0) {
        std::cerr << "Error setting ALSA playback parameters.\n";
        return false;
    }

    return true;
}

// Function to capture and send audio
void captureAndSend(snd_pcm_t* capture_handle, int sockfd) {
    int16_t buffer[BUFFER_SIZE * CHANNELS];

    while (true) {
        snd_pcm_readi(capture_handle, buffer, BUFFER_SIZE);
        send(sockfd, buffer, BUFFER_SIZE * CHANNELS * sizeof(int16_t), 0);
    }
}

// Function to receive and play audio
void receiveAndPlay(snd_pcm_t* playback_handle, int sockfd) {
    int16_t buffer[BUFFER_SIZE * CHANNELS];

    while (true) {
        int bytes_received = recv(sockfd, buffer, sizeof(buffer), 0);
        if (bytes_received > 0) {
            int frames = bytes_received / (CHANNELS * sizeof(int16_t));
            snd_pcm_writei(playback_handle, buffer, frames);
        }
    }
}

int main() {
    int sockfd;
    sockaddr_in server_addr;
    snd_pcm_t *capture_handle, *playback_handle;

    // Set up ALSA for capture and playback
    if (!setupALSACapture(capture_handle) || !setupALSAPlayback(playback_handle)) {
        return 1;
    }

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cerr << "Socket creation failed\n";
        return 1;
    }

    // Set server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    // Connect to the server
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Connection failed\n";
        close(sockfd);
        return 1;
    }

    std::cout << "Connected to server\n";

    // Start threads for capture/send and receive/play
    std::thread send_thread(captureAndSend, capture_handle, sockfd);
    std::thread receive_thread(receiveAndPlay, playback_handle, sockfd);

    send_thread.join();
    receive_thread.join();

    snd_pcm_close(capture_handle);
    snd_pcm_close(playback_handle);
    close(sockfd);
    return 0;
}
