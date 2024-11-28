// Libraries 
 //Standard 
 #include <iostream>  //Error log and user interaction
 #include <vector>    //Dynamic array 
 #include <thread>    //Allow multithreading
 #include <mutex>     //multithread constant access 
 #include <algorithm> //search algorithm 
 #include <atomic>    //boolean control for multithreading

 //Sytem libraries
 #include <unistd.h>  //API functions; system calls 
 #include <cstring>   //string manipulation 
 #include <arpa/inet.h> //UDP and IP address handle (Network)

 //Audio 
 #include <alsa/asoundlib.h> //Audio capture and playback 
 
 //Timing and synchronization 
 #include <chrono>     //Timestamps



#include <queue>


//Global constants

//Audio format
#define S_Rate 48000   //Sample rate HZ
#define Channels 2     //Stereo audio.

//Audio buffer
#define Buff_Size 4096 

//UDP comms
#define UDP_port 12345 //Communication port

//Connection check 
#define Broadcast_Int 5 //Time (s) between HELLO messages 



//Audio packet capture and sequence
struct Audio_Packet {
    uint32_t sequence; 
    uint64_t timestamp;
    int16_t audio_data[Buff_Size * Channels]; 

};

struct PeerInfo {
    sockaddr_in address;
    std::chrono::steady_clock::time_point last_hello_received;
};



//Setup Alsa
bool ALSAset(snd_pcm_t* &captureman, snd_pcm_t* &playbackman) {
    if (snd_pcm_open(&captureman, "default", SND_PCM_STREAM_CAPTURE, 0) < 0) {
        std::cerr << "Capturing error.\n";
        return false;
    }

    if (snd_pcm_open(&playbackman, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        std::cerr << "Playback error.\n";
        return false;
    }

    if (snd_pcm_set_params(captureman, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, Channels, S_Rate, 1, 10000) < 0) {
        std::cerr << "Capture set params error.\n";
        return false;
    }

    if (snd_pcm_set_params(playbackman, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, Channels, S_Rate, 1, 10000) < 0) {
        std::cerr << "Playback set params error.\n";
        return false;
    }

    return true;
}



//Audio capture and send function
void RecAndSend(snd_pcm_t* captureman, std::vector<PeerInfo>& peerIP, int sockfd, std::atomic<bool>& running, std::mutex& peer_mute) {
    pthread_t this_thread = pthread_self();
    struct sched_param params;
    params.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_setschedparam(this_thread, SCHED_FIFO, &params);

    
    Audio_Packet packet = {};
    uint32_t sequence = 0;

    while (running) {
        int FrameNum = snd_pcm_readi(captureman, packet.audio_data, Buff_Size);
        if (FrameNum < 0) {
            std::cerr << "Capture overrun: " << snd_strerror(FrameNum) << "\n";
            if (snd_pcm_prepare(captureman) < 0) {
                std::cerr << "Error recovering audio capture. Stopping...\n";
                running = false;
                break;
            }
            continue;
        }

        // Set sequence number
        packet.sequence = sequence++;

        // Access peer IP addresses
        std::lock_guard<std::mutex> lock(peer_mute);
        auto peersList = peerIP;

        // Send audio to each peer
        for (const auto& peer : peersList) {
            ssize_t Audio_size = sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&peer.address, sizeof(peer.address));
            if (Audio_size < 0) {
                std::cerr << "Audio stream error.\n";
            }
        }

        // Add small delay to avoid overloading the network
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}


//Receive and playback audio function
void AudioPlayback(snd_pcm_t* playbackman, int sockfd, std::atomic<bool>& running) {
    pthread_t this_thread = pthread_self();
    struct sched_param params;
    params.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_setschedparam(this_thread, SCHED_FIFO, &params);

    
    std::queue<Audio_Packet> jitter_buffer;
    Audio_Packet packet = {};
    uint32_t last_sequ = 0;
    const int buffer_delay = 5; // Adjust based on network conditions

    while (running) {
        ssize_t Audio_rec = recv(sockfd, &packet, sizeof(packet), 0);
        if (Audio_rec < 0) {
            std::cerr << "Error receiving audio: " << strerror(errno) << "\n";
            continue;
        }

        jitter_buffer.push(packet);

        // Wait until enough packets are in the buffer to start playback
        if (jitter_buffer.size() < buffer_delay) {
            continue;
        }

        // Get packet from buffer
        Audio_Packet play_packet = jitter_buffer.front();
        jitter_buffer.pop();

        // Handle missing packets
        if (play_packet.sequence != last_sequ + 1) {
            std::cerr << "Packet loss detected. Expected: " << last_sequ + 1 << " but received: " << play_packet.sequence << "\n";
            uint16_t silence[Buff_Size * Channels] = {0}; // Buffer of silence
            snd_pcm_writei(playbackman, silence, Buff_Size);
        } else {
            last_sequ = play_packet.sequence;
        }

        // Playback
        int Frames_r = snd_pcm_writei(playbackman, play_packet.audio_data, Buff_Size);
        if (Frames_r < 0) {
            std::cerr << "Playback underrun: " << snd_strerror(Frames_r) << "\n";
            snd_pcm_prepare(playbackman);
            continue;
        }
    }
}



//UDP hello broadcast 
void sendHELLO(int sockfd, std::atomic<bool>& running) {
    const char* message = "HELLO";

    while (running) {
        for (int i = 1; i < 255; ++i) { // Assuming a /24 subnet
            sockaddr_in peer_addr{};
            peer_addr.sin_family = AF_INET;
            peer_addr.sin_port = htons(UDP_port);
            std::string ip = "192.168.1." + std::to_string(i);  // Adjust subnet as needed
            peer_addr.sin_addr.s_addr = inet_addr(ip.c_str());

            if (sendto(sockfd, message, strlen(message), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr)) < 0) {
                std::cerr << "Failed to send HELLO to " << ip << ": " << strerror(errno) << "\n";
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(Broadcast_Int));
    }
}



//UDP Find peers function 
void LookForPeers(int sockfd, std::vector<PeerInfo>& peer_List, std::mutex& peer_mute, std::atomic<bool>& running) {
    char buffer[1024];
    sockaddr_in peer_addr{};
    socklen_t addr_l = sizeof(peer_addr);

    while (running) {
        // Get message
        ssize_t Audio_rec = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&peer_addr, &addr_l);
        if (Audio_rec > 0) {
            buffer[Audio_rec] = '\0';
            if (strcmp(buffer, "HELLO") == 0) {
                std::lock_guard<std::mutex> lock(peer_mute);
                auto it = std::find_if(peer_List.begin(), peer_List.end(), [&peer_addr](const PeerInfo& peer) {
                    return peer.address.sin_addr.s_addr == peer_addr.sin_addr.s_addr && peer.address.sin_port == peer_addr.sin_port;
                });

                // Add new peer or update existing peer
                if (it == peer_List.end()) {
                    PeerInfo new_peer{peer_addr, std::chrono::steady_clock::now()};
                    peer_List.push_back(new_peer);
                    std::cout << "New peer found: " << inet_ntoa(peer_addr.sin_addr) << ":" << ntohs(peer_addr.sin_port) << "\n";
                } else {
                    it->last_hello_received = std::chrono::steady_clock::now();
                }
            }
        }
    }
}



void RemoveInactivePeers(std::vector<PeerInfo>& peer_List, std::mutex& peer_mute) {
    std::lock_guard<std::mutex> lock(peer_mute);
    auto now = std::chrono::steady_clock::now();
    peer_List.erase(
        std::remove_if(peer_List.begin(), peer_List.end(), [now](const PeerInfo& peer) {
            return std::chrono::duration_cast<std::chrono::seconds>(now - peer.last_hello_received).count() > 15;
        }),
        peer_List.end()
    );
}





int main() {
    // Initialize common variables
    std::vector<PeerInfo> peer_List;   // List of discovered peers
    std::mutex peer_mute;              // Protecting peer list
    std::atomic<bool> running(true);   // Flag to control threads

   
    // UDP socket init
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "Failed socket init: " << strerror(errno) << "\n";
        return 1;
    }

    // Increase the socket buffer size
    int buffer_size = 2 * 1024 * 1024; // 2 MB buffer
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size)) < 0) {
        std::cerr << "Failed to set receive buffer size: " << strerror(errno) << "\n";
    }

    // Bind the socket to the correct network interface
    const char* interface_name = "wlo1";  // Replace with the correct interface name
    if (setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, interface_name, strlen(interface_name)) < 0) {
        std::cerr << "Failed to bind socket to device " << interface_name << ": " << strerror(errno) << "\n";
    }

    // Enable broadcasting
    int enable_brd = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &enable_brd, sizeof(enable_brd)) < 0) {
        std::cerr << "Failed to initialize broadcast: " << strerror(errno) << "\n";
        close(sockfd);
        return 1;
    }

    // Set socket address and port
    sockaddr_in local_address = {};
    local_address.sin_family = AF_INET;
    local_address.sin_port = htons(UDP_port);
    local_address.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr*)&local_address, sizeof(local_address)) < 0) {
        std::cerr << "Socket bind failed: " << strerror(errno) << "\n";
        close(sockfd);
        return 1;
    }

    // Init ALSA
    snd_pcm_t* captureman = nullptr;
    snd_pcm_t* playbackman = nullptr;

    if (!ALSAset(captureman, playbackman)) {
        std::cerr << "Failed to initialize ALSA.\n";
        close(sockfd);
        return 1;
    }

    // Start threads
    std::thread broadcast(sendHELLO, sockfd, std::ref(running));
    std::thread listen(LookForPeers, sockfd, std::ref(peer_List), std::ref(peer_mute), std::ref(running));
    std::thread send_audio(RecAndSend, captureman, std::ref(peer_List), sockfd, std::ref(running), std::ref(peer_mute));
    std::thread play_audio(AudioPlayback, playbackman, sockfd, std::ref(running));

    // End program
    std::cout << "Press Enter to stop the program...\n";
    std::cin.get();

    // Stop threads
    running = false;

    // Join threads
    broadcast.join();
    listen.join();
    send_audio.join();
    play_audio.join();

    // Clean up buffer
    snd_pcm_close(captureman);
    snd_pcm_close(playbackman);
    close(sockfd);

    std::cout << "End of program.\n";
    return 0;
}
