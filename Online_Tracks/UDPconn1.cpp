// Libraries 
 //Standard 
 #include <iostream>  //I/O operations
 #include <cstring>   //string manipulation
 #include <vector>    //Dynamic array 
 #include <thread>    //multithreading
 #include <mutex>     //thread safe variables
 #include <condition_variable>
 #include <algorithm> //search algorithm 
 #include <atomic>    //boolean controñl 
 #include <stdlib.h>  //Standard functions (exit) 


 //POSIX libraries 
 #include <unistd.h>  //API functions; system calls 
 #include <sys/types.h>
 #include <sys/wait.h> 
 #include <signal.h> //Audio format
 #include <errno.h>

 //Network Libraries
 #include <sys/socket.h> 
 #include <arpa/inet.h>
 #include <netinet/in.h>
 #include <netdb.h>

 //Audio Libraries 
 #include <alsa/asoundlib.h>


 //Initialize global constants 
 #define PORT1 12345   //Port for client a 
 #define PORT2 54321   //Port for client b 
 #define BUFFSIZE 1024 
 #define CLIENT1_IP "192.168.1.83"
 #define CLIENT2_IP "192.168.1.85"
 #define SRATE 44100 
 #define Channels 2 


std::atomic<bool> stop_streaming(false);
std::condition_variable stop_condition; 
std::mutex fn_mute;

void init_sig(int sig) {
    if (signal == SIGINT) {
        stop_streaming = true; 
        stop_condition.notify_all();
    }
}

//Audio capture function

void audio_cap(int sockfd, struct sockaddr_in remote_addr) {
    snd_pcm_t *capture_man;
    snd_pcm_hw_params_t *hw_params;
    int err; 
    char buffer[BUFFSIZE * 2 * Channels];


    //Open ALSA recording 
    if ((err = snd_pcm_open(&capture_man, "default", SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        std::cerr << "capture device error" << snd_strerror(err) << "\n";
        return;

    }

    //ALSA audio parameter
    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(capture_man, hw_params);
    snd_pcm_hw_params_set_access(capture_man, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(capture_man, hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate(capture_man, hw_params, SRATE, 0);
    snd_pcm_hw_params_set_channels(capture_man, hw_params, Channels);
    snd_pcm_hw_params(capture_man, hw_params); 



    //Start audio capture
    while(!stop_streaming) {
        int fr_capture = snd_pcm_readi(capture_man, buffer, BUFFSIZE);
        if (fr_capture < 0) {
            fr_capture = snd_pcm_recover(capture_man, fr_capture, 0);
            if (fr_capture < 0) {
                std::cerr << "Audio capture error" << snd_strerror(fr_capture) << "\n";
                break;
            }
        }

        //Send audio on socket 
        int byte_send = fr_capture * 2 * Channels; //Calculate bytes sent
        if (sendto(sockfd, buffer, byte_send, 0, (struct sockaddr *)&remote_addr, sizeof(remote_addr)) == -1) {
            perror("error sending");
            stop_streaming = true;
            break;
        }

    }    

    snd_pcm_close(capture_man);

}

//Audio playback function 
void play_audio(int sockfd) {
    snd_pcm_t *playback_man;
    snd_pcm_hw_params_t *hw_params;
    int err; 
    char buffer[BUFFSIZE * 2 * Channels];
    struct sockaddr_in source_addr;
    socklen_t addr_len = sizeof(source_addr);

    //Open playback device
    if((err = snd_pcm_open(&playback_man, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        std::cerr << "playback device error" << snd_strerror(err) << "\n";
        return; 

    }

    //ALSA audio parameters
    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(playback_man, hw_params);
    snd_pcm_hw_params_set_access(playback_man, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(playback_man, hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate(playback_man, hw_params, SRATE, 0);
    snd_pcm_hw_params_set_channels(playback_man, hw_params, Channels);
    snd_pcm_hw_params(playback_man, hw_params); 



    //play audio 
    while(!stop_streaming) {
        int byte_num = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&source_addr, &addr_len);

        if (byte_num <= 0) {
            if (byte_num == 0) {
                std::cout << "Server closed connection. \n";
            } else {
                perror("recv error");
            }
            stop_streaming = true;
            break;
        }

        int frames_play = byte_num / (Channels * 2);
        if ((err = snd_pcm_writei(playback_man, buffer, frames_play)) < 0) {
            if (err == -EPIPE) {
                std::cerr << "Buffer underrun occurred: " << snd_strerror(err) << "\n";
                if ((err = snd_pcm_prepare(playback_man)) < 0) {
                    std::cerr << "Error recovering from underrum" << snd_strerror(err) << "\n";
                    stop_streaming = true;
                    break;
                
                }
            } else {
                std::cerr << "write to audio interface failed: " << snd_strerror(err) << "\n";
                stop_streaming = true;
                break;
            }
        }
    }

    snd_pcm_close(playback_man); 
}



int main() {
    
    signal(SIGINT, init_sig);


    //Initialize socket 
    int sockfd; 
    struct sockaddr_in local_addr, remote_addr;

    //Create UDP socket 
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("Socket setup error");
        exit(1);

    }


    //Address structure (client 1)
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET; 
    local_addr.sin_port = htons(PORT1);
    local_addr.sin_addr.s_addr = INADDR_ANY; 


    //Bind socket to address
    if (bind(sockfd, (struct sockaddr *)&local_addr, sizeof(local_addr)) == -1) {
        perror("Binding error");
        close(sockfd);
        exit(1);

    }

     // Set up remote address structure (destination address)
    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(PORT2); 
    if (inet_pton(AF_INET, CLIENT2_IP, &remote_addr.sin_addr) <= 0) {  
        perror("Invalid IP address");
        close(sockfd);
        exit(1);
    }

    
    //Start threads and join them
    std::thread capture_th(audio_cap, sockfd, remote_addr);
    std::thread playback_th(play_audio, sockfd);

   {
    std::unique_lock<std::mutex> lock(fn_mute);
    stop_condition.wait(lock, [] { return stop_streaming.load(); });
   }


    //Join threads
    if (capture_th.joinable()) {
        capture_th.join();
    }
    if (playback_th.joinable()) {
        playback_th.join();
    }

    
    close(sockfd);
    return 0; 

}