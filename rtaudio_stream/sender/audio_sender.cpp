#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <alsa/asoundlib.h>

#define SERVER_IP "127.0.0.1"  // Change to receiver's IP if different
#define PORT 8888
#define BUFSIZE 1024

int main() {
    int sockfd;
    struct sockaddr_in serverAddr;
    char buffer[BUFSIZE];

    // Setup UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        return 1;
    }

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

    // Setup ALSA for capture
    snd_pcm_t *capture_handle;
    snd_pcm_hw_params_t *hw_params;
    int err;

    if ((err = snd_pcm_open(&capture_handle, "default", SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        std::cerr << "Error opening capture device: " << snd_strerror(err) << std::endl;
        return 1;
    }
    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(capture_handle, hw_params);
    snd_pcm_hw_params_set_access(capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(capture_handle, hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate(capture_handle, hw_params, 44100, 0);
    snd_pcm_hw_params_set_channels(capture_handle, hw_params, 2);
    snd_pcm_hw_params(capture_handle, hw_params);

    // Capture and send audio data
    while (true) {
        snd_pcm_readi(capture_handle, buffer, BUFSIZE / 4); // assuming 16-bit stereo
        sendto(sockfd, buffer, BUFSIZE, 0, (const struct sockaddr *)&serverAddr, sizeof(serverAddr));
    }

    snd_pcm_close(capture_handle);
    close(sockfd);
    return 0;
} 
