#include <iostream>
#include <thread>
#include <boost/asio.hpp>
#include <alsa/asoundlib.h>

using namespace boost::asio;
using ip::tcp;

// Function to initialize ALSA
bool setupAlsa(snd_pcm_t*& handle, snd_pcm_stream_t streamType) {
    int err;
    // Open ALSA device for capture or playback
    if ((err = snd_pcm_open(&handle, "default", streamType, 0)) < 0) {
        std::cerr << "Cannot open audio device: " << snd_strerror(err) << std::endl;
        return false;
    }
    // Set parameters (format, rate, channels, etc.)
    // Configure here as per your latency requirements
    return true;
}

// Example server function to send audio data
void audioServer(int port) {
    io_service ioService;
    tcp::acceptor acceptor(ioService, tcp::endpoint(tcp::v4(), port));
    tcp::socket socket(ioService);

    std::cout << "Server listening on port " << port << std::endl;
    acceptor.accept(socket);

    snd_pcm_t *captureHandle;
    if (!setupAlsa(captureHandle, SND_PCM_STREAM_CAPTURE)) {
        return;
    }

    const int bufferSize = 4096;  // Adjust buffer size as needed
    char buffer[bufferSize];

    while (true) {
        snd_pcm_readi(captureHandle, buffer, bufferSize / 4);  // Adjust as per sample size
        boost::system::error_code ignoredError;
        boost::asio::write(socket, buffer::buffer(buffer), ignoredError);
    }

    snd_pcm_close(captureHandle);
}

// Example client function to receive audio data
void audioClient(const std::string &host, int port) {
    io_service ioService;
    tcp::socket socket(ioService);
    socket.connect(tcp::endpoint(ip::address::from_string(host), port));

    snd_pcm_t *playbackHandle;
    if (!setupAlsa(playbackHandle, SND_PCM_STREAM_PLAYBACK)) {
        return;
    }

    const int bufferSize = 4096;  // Match server buffer size
    char buffer[bufferSize];

    while (true) {
        boost::asio::read(socket, buffer::buffer(buffer));
        snd_pcm_writei(playbackHandle, buffer, bufferSize / 4);  // Adjust as per sample size
    }

    snd_pcm_close(playbackHandle);
}
