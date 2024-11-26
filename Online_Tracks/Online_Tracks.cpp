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


//Global constants

//Audio format
#define S_Rate 48000   //Sample rate HZ
#define Channels 2     //Stereo audio

//Audio buffer
#define Buff_Size 1024 

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


//Sending audio function
void RecAndSend(snd_pcm_t* captureman, std::vector<sockaddr_in>& peerIP, int sockfd, std::atomic<bool>& running, std::mutex& peer_mute) {
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
        packet.sequence = sequence++; 

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





