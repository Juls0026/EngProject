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

//Video (OpenCV)
#include <opencv2/opencv.hpp> //Main OpenCV Library
#include <opencv2/imgproc.hpp> //Image processing Library
#include <opencv2/highgui.hpp> //GUI 
#include <opencv2/imgcodecs.hpp> //encode/decode library 

 //Timing and synchronization 
#include <chrono>     //Timestamps



//Global constants
//Audio format
#define S_Rate 48000   //Sample rate HZ
#define Channels 2     //Stereo audio

//Audio buffer
#define Buff_Size 1024 

//Video format 
#define Width 320
#define Heighth 240

//Video Quality 
#define V_Quality 50 

//Video packet size
#define Max_Size 131072

//UDP comms
#define UDP_port 12345 //Communication port

//Connection check 
#define Broadcast_Int 5 //Time (s) between HELLO messages 



//Audio packet structure
struct Audio_Packet {
    uint32_t a_sequence; 
    uint64_t timestamp;
    int16_t audio_data[Buff_Size * Channels]; 

};


//Video packet structure
struct Video_Packet {
    uint32_t v_sequence;
    uint64_t timestamp;
    size_t video_size; 
    unsigned char Vdata[Max_Size]; 
};

//Setup Alsa
bool ALSAset(snd_pcm_t* &captureman, snd_pcm_t* &playbackman) {
    
    //Start audio capture; send message if failed
    if (snd_pcm_open(&captureman, "default", SND_PCM_STREAM_CAPTURE, 0) < 0){
        std::cerr << "Capturing error.\n";
        return false; 
    }

    //Start audio playback; send message if failed 
    if(snd_pcm_open(&playbackman, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        std::cerr << "Playback error. \n";
        return false;
    }


    //Set audio stream parameters
    snd_pcm_set_params(captureman, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, Channels, S_Rate, 1, 10000);
    snd_pcm_set_params(playbackman, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, Channels, S_Rate, 1, 10000);
    return true; //Successful setup

}


//Audio capture and send function
void AudioRecAndSend(snd_pcm_t* captureman, std::vector<sockaddr_in>& peerIP, int sockfd, std::atomic<bool>& running, std::mutex& peer_mute) {
    
    //Initialize
    Audio_Packet packet = {};
    uint32_t sequence = 0; 

    while(running) {
        int FrameNum = snd_pcm_readi(captureman, packet.audio_data, Buff_Size);
        if (FrameNum < 0) {
            std::cerr << "Audio rec error" << snd_strerror(FrameNum) << "\n";
            if (snd_pcm_prepare(captureman) < 0) {
                std::cerr << "Audio device error. \n";
                running = false;
                break;
            }
            continue;
        }

        //Set sequence number
        packet.a_sequence = sequence++; 

        //Access peer IP address 
        std::lock_guard<std::mutex> lock(peer_mute);
        auto peersList = peerIP; 

        //Send Audio to each peer
        for (const auto& peer : peersList) {
            ssize_t Audio_size = sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&peer, sizeof(peer));
            if (Audio_size < 0) {
                std::cerr << "Audio stream error. \n";
            }
        }

    }
}

void VideoRecandSend(std::vector<sockaddr_in>& peer_List, int sockfd, std::atomic<bool>& running, std::mutex& peer_mute) {
    cv::VideoCapture cap(0, cv::CAP_V4L2); //Open Webcam
    if (!cap.isOpened()) {
        std::cerr << "Video device error. \n";
        running = false; 
        return;

    }

    //Initialize video params
    cap.set(cv::CAP_PROP_FRAME_WIDTH, Width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, Heighth);
    cap.set(cv::CAP_PROP_FPS, 30);

    //Initialize packet and sequence
    Video_Packet packet = {};
    uint32_t v_sequence = 0;

    while (running) { 
        cv::Mat frame;
        if (!cap.read(frame)) {
            std::cerr << "Failed to rec video. \n";
            continue; 

        }

        //Encode video data
        std::vector<uchar> enc_frame;
        std::vector<int> comp_params = {cv::IMWRITE_JPEG_QUALITY, V_Quality} ;
        if(!cv::imencode(".jpg", frame,enc_frame, comp_params)) {
            std::cerr << "Video Encode error. \n";
            continue;

        }

        //Check frame size
        if (enc_frame.size() > sizeof(packet.Vdata)) {
            std::cerr << "Frame is too large." << enc_frame.size() << " bytes. \n";
            continue; 

        }


        //Init packet
        packet.v_sequence = v_sequence++;
        packet.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        packet.video_size = enc_frame.size();
        std::memcpy(packet.Vdata, enc_frame.data(), enc_frame.size());


        //Send packet to peers
        std::lock_guard<std::mutex> lock(peer_mute);

        //Log peer list size
        if (peer_List.empty()) {
            std::cerr << "No connected peers. \n";
            return;

        }

        for (const auto& peer : peer_List) {
            ssize_t VideoBytes = sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&peer, sizeof(peer));
            if (VideoBytes < 0) {
                std::cerr << "Video transmission error." << strerror(errno) << "\n";
            } else {
                std::cerr << "Video sent to" << inet_ntoa(peer.sin_addr) << ":" << ntohs(peer.sin_port) << "(" << VideoBytes << " bytes) \n";

            }
        }
    }

}

//Receive and playback audio function
void AudioPlayback(snd_pcm_t* playbackman, int sockfd, std::atomic<bool>& runnning){

    //Initialize
    Audio_Packet packet = {};
    uint32_t last_sequ = 0;
    uint16_t silence[Buff_Size * Channels] = {0}; //Buffer of 0s for lost packets 

    while (runnning) { 
        ssize_t Audio_rec = recv(sockfd, &packet, sizeof(packet), 0);
        if (Audio_rec < 0) {
            std::cerr << "Error receiving audio. \n";
            continue;
        }


        int Frames_r = snd_pcm_writei(playbackman, packet.audio_data, Buff_Size);
        if (Frames_r < 0) {
            snd_pcm_prepare(playbackman);
        }

        //Update sequence
        last_sequ = packet.a_sequence;
    }

}

//Recieve and play video 
void VideoPlayback(int sockfd, std::atomic<bool>& running) {

    //Open Video Window 
    cv::namedWindow("Stream", cv::WINDOW_AUTOSIZE);

    //Initialize Video Stream
    Video_Packet packet = {};
    while(running) {
        ssize_t Video_Bytes = recv(sockfd, &packet, sizeof(packet), 0);
        if (Video_Bytes < 0) {
            std::cerr << "Video receive error. \n";
            continue;

        }


        //Decode video data 
        cv::Mat frame = cv::imdecode(cv::Mat(1, packet.video_size, CV_8UC1, packet.Vdata), cv::IMREAD_COLOR);
        if (frame.empty()) {
            std::cerr << "Decoding video error. \n";
            continue; 

        }

        //Video Display 
        cv::imshow("Stream", frame);
        if (cv::waitKey(1) == 27) {  //Stop with Esc Key
            running = false;
        }
    }

    cv::destroyWindow("Stream");

}



//UDP hello broadcast 
void sendHELLO (int sockfd, sockaddr_in& boradcast_ad, std::atomic<bool>& running) {
    //Initialize 
    const char* message = "HELLO"; 

    //repetedly send message to constantly check peers
    while (running) {
        if (sendto(sockfd, message, strlen(message), 0, (struct sockaddr*)&boradcast_ad, sizeof(boradcast_ad)) < 0) {

            std::cerr << "Failed to broadcast message " << strerror(errno) << "\n";      
        }

        //Wait to send message again 
        std::this_thread::sleep_for(std::chrono::seconds(Broadcast_Int));

    }
}


//UDP Find peers function 
void LookForPeers(int sockfd, std::vector<sockaddr_in>& peer_List, std::mutex& peer_mute, std::atomic<bool>& running) {

     //Initialize
     char buffer[1024];
     sockaddr_in peer_addr{};
     socklen_t addr_l = sizeof(peer_addr);
    

    while (running) {
        //Get message
        ssize_t Audio_rec = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&peer_addr, &addr_l);
        if (Audio_rec > 0) {
            buffer[Audio_rec] = '\0';
            if (strcmp(buffer, "HELLO") == 0) {
                std::lock_guard<std::mutex> lock(peer_mute);
                auto it = std::find_if(peer_List.begin(), peer_List.end(), [&peer_addr](const sockaddr_in& addr) {
                    return addr.sin_addr.s_addr == peer_addr.sin_addr.s_addr && addr.sin_port == peer_addr.sin_port;
                });
                
                //Add Peer to list
                if (it == peer_List.end()) {
                    peer_List.push_back(peer_addr);
                    std::cout << "Peer: " <<inet_ntoa(peer_addr.sin_addr) << "\n";
                }
                }

            }

        }
    }



int main() {

    //Initialize coommon variables 
    std::vector<sockaddr_in> peer_List; //List of discovered peers
    std::mutex peer_mute;               //Protecting peer List 
    std::atomic<bool> running(true);    //Flag to control threads

    //UDP scoket init 
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd <0) {
        std::cerr << "Failed socket init: " << strerror(errno) << "\n";
        return 1;
    }

    //Enable broadcasting 
    int enable_brd = 1; 
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &enable_brd, sizeof(enable_brd))< 0) {
        std::cerr << "Failed to initialize broadcast: " << strerror(errno) << "ºn";
        close (sockfd);
        return 1;
    }

    //Set socket address and port
    sockaddr_in local_address = {};
    local_address.sin_family = AF_INET;
    local_address.sin_port = htons(UDP_port);
    local_address.sin_addr.s_addr = INADDR_BROADCAST;

    if (bind(sockfd, (struct sockaddr*)&local_address, sizeof(local_address)) < 0) {
        std::cerr << "Socket bind failed: " <<strerror(errno) << "\n";
        close(sockfd);
        return 1;

    }


    //Set Broadcast address
    sockaddr_in brd_address = {};
    brd_address.sin_family = AF_INET;
    brd_address.sin_port = htons(UDP_port);
    brd_address.sin_addr.s_addr = INADDR_BROADCAST;


    //Init ALSA 
    snd_pcm_t* captureman = nullptr;
    snd_pcm_t* playbackman = nullptr;


    if(!ALSAset(captureman, playbackman)) {
        std::cerr << "Failed to initialize ALSA. \n";
        close(sockfd);
        return 1;

    }


    //Start threads
    std::thread broadcast(sendHELLO, sockfd, std::ref(brd_address), std::ref(running));
    std::thread listen(LookForPeers, sockfd, std::ref(peer_List), std::ref(peer_mute), std::ref(running));
    std::thread send_audio(AudioRecAndSend, captureman, std::ref(peer_List), sockfd, std::ref(running), std::ref(peer_mute));
    std::thread play_audio(AudioPlayback, playbackman, sockfd, std::ref(running));
    std::thread send_video(VideoRecandSend, std::ref(peer_List), sockfd, std::ref(running), std::ref(peer_mute));
    std::thread play_video(VideoPlayback, sockfd, std::ref(running));


    //End program
    std::cout << "Press Enter to stop the program... \n";
    std::cin.get();

    //stop threads
    running = false; 

    //join threads 
    broadcast.join();
    listen.join();
    send_audio.join();
    play_audio.join();
    send_video.join();
    play_video.join();




    //clean up buffer
    snd_pcm_close(captureman);
    snd_pcm_close(playbackman);
    close(sockfd);



    std::cout << "End of program. \n";
    return 0;

}