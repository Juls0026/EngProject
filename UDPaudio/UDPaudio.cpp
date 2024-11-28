#include <iostream>
#include <thread>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <asio.hpp>

#define SAMPLE_RATE 44100
#define CHANNELS 2
#define SAMPLE_SIZE 1024

void audio_sender(asio::ip::udp::socket& socket, asio::ip::udp::endpoint& receiver_endpoint, pa_simple* pa) {
    uint8_t buffer[SAMPLE_SIZE * CHANNELS * 2]; // 16-bit samples (2 bytes per sample)
    int error;
    try {
        while (true) {
            // Capture audio data from the microphone
            if (pa_simple_read(pa, buffer, sizeof(buffer), &error) < 0) {
                std::cerr << "PulseAudio read error: " << pa_strerror(error) << "\n";
                break;
            }

            // Send the captured audio data via UDP
            socket.send_to(asio::buffer(buffer, sizeof(buffer)), receiver_endpoint);
        }
    } catch (std::exception& e) {
        std::cerr << "Sender exception: " << e.what() << "\n";
    }
}

void audio_receiver(asio::ip::udp::socket& socket, pa_simple* pa) {
    uint8_t buffer[SAMPLE_SIZE * CHANNELS * 2]; // 16-bit samples (2 bytes per sample)
    int error;
    try {
        asio::ip::udp::endpoint sender_endpoint;
        while (true) {
            // Receive audio data via UDP
            size_t length = socket.receive_from(asio::buffer(buffer), sender_endpoint);

            // Playback the received audio data
            if (pa_simple_write(pa, buffer, length, &error) < 0) {
                std::cerr << "PulseAudio write error: " << pa_strerror(error) << "\n";
                break;
            }
        }
    } catch (std::exception& e) {
        std::cerr << "Receiver exception: " << e.what() << "\n";
    }
}

int main(int argc, char* argv[]) {
    // Debugging: Print argument count and the arguments passed
    std::cout << "argc: " << argc << "\n";
    for (int i = 0; i < argc; ++i) {
        std::cout << "argv[" << i << "]: " << argv[i] << "\n";
    }

    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <local port> <receiver IP> <receiver port>\n";
        return 1;
    }

    uint16_t local_port = std::stoi(argv[1]);
    const char* receiver_ip = argv[2];
    uint16_t receiver_port = std::stoi(argv[3]);

    asio::io_context io_context;

    // Set up sockets for sending and receiving
    asio::ip::udp::socket send_socket(io_context);
    send_socket.open(asio::ip::udp::v4());
    asio::ip::udp::endpoint receiver_endpoint(asio::ip::make_address(receiver_ip), receiver_port);

    asio::ip::udp::socket receive_socket(io_context, asio::ip::udp::endpoint(asio::ip::udp::v4(), local_port));

    // Set up PulseAudio for recording
    pa_simple *pa_record = nullptr;
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = SAMPLE_RATE;
    ss.channels = CHANNELS;

    int error;
    if (!(pa_record = pa_simple_new(NULL, "UDP Streamer", PA_STREAM_RECORD, NULL, "record", &ss, NULL, NULL, &error))) {
        std::cerr << "PulseAudio record connection error: " << pa_strerror(error) << "\n";
        return 1;
    }

    // Set up PulseAudio for playback
    pa_simple *pa_play = nullptr;
    if (!(pa_play = pa_simple_new(NULL, "UDP Streamer", PA_STREAM_PLAYBACK, NULL, "playback", &ss, NULL, NULL, &error))) {
        std::cerr << "PulseAudio playback connection error: " << pa_strerror(error) << "\n";
        if (pa_record) pa_simple_free(pa_record);
        return 1;
    }

    // Start sender and receiver threads
    std::thread sender_thread(audio_sender, std::ref(send_socket), std::ref(receiver_endpoint), pa_record);
    std::thread receiver_thread(audio_receiver, std::ref(receive_socket), pa_play);

    // Wait for threads to finish
    sender_thread.join();
    receiver_thread.join();

    if (pa_record) pa_simple_free(pa_record);
    if (pa_play) pa_simple_free(pa_play);

    return 0;
}

