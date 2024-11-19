#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <alsa/asoundlib.h>

#define SAMPLE_RATE 48000
#define CHANNELS 2
#define BUFFER_SIZE 1024  // Frames per buffer
#define PORT 12345
#define SERVER_IP "192.168.1.83"  // Replace with server IP for actual deployment

int main() {
    int sockfd;
    sockaddr_in server_addr;
    int16_t buffer[BUFFER_SIZE * CHANNELS];

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cerr << "Socket creation failed\n";
        return 1;
    }

    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported\n";
        return 1;
    }

    // Connect to server
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Connection failed\n";
        return 1;
    }

    std::cout << "Connected to the server.\n";

    // ALSA setup for audio capture and playback
    snd_pcm_t *capture_handle, *playback_handle;

    if (snd_pcm_open(&capture_handle, "default", SND_PCM_STREAM_CAPTURE, 0) < 0) {
        std::cerr << "Error opening capture device.\n";
        return 1;
    }

    if (snd_pcm_open(&playback_handle, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        std::cerr << "Error opening playback device.\n";
        snd_pcm_close(capture_handle);
        return 1;
    }

    snd_pcm_set_params(capture_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                       CHANNELS, SAMPLE_RATE, 1, 10000);
    snd_pcm_set_params(playback_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                       CHANNELS, SAMPLE_RATE, 1, 10000);

    // Audio capture and send loop
    while (true) {
        // Capture audio data
        if (snd_pcm_readi(capture_handle, buffer, BUFFER_SIZE) < 0) {
            std::cerr << "Audio capture error\n";
            break;
        }

        // Send audio data to server
        send(sockfd, buffer, sizeof(buffer), 0);

        // Receive audio data from server
        ssize_t bytes_received = recv(sockfd, buffer, sizeof(buffer), 0);
        if (bytes_received > 0) {
            // Play received audio
            if (snd_pcm_writei(playback_handle, buffer, BUFFER_SIZE) < 0) {
                std::cerr << "Audio playback error\n";
                break;
            }
        } else if (bytes_received <= 0) {
            std::cerr << "Disconnected from server\n";
            break;
        }
    }

    // Clean up
    snd_pcm_close(capture_handle);
    snd_pcm_close(playback_handle);
    close(sockfd);

    return 0;
}
