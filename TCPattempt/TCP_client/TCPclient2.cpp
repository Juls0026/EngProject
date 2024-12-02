//Include liraries 

//Standard libraries
#include <iostream>  //I/O 
#include <cstring>   //String manipulation
#include <vector>    //Dynamic array 
#include <thread>    //Multi-thread
#include <mutex>     //Thread safety variables
#include <atomic>


//POSIX libraries 
#include <unistd.h>  //System calls
#include <sys/types.h> //Data types for system calls


//Network libraries
#include <sys/socket.h> //Socket API 
#include <netdb.h>    //Network database operations 
#include <netinet/in.h> //address 
#include <arpa/inet.h>  //IP address convertion

//Audio Libraries 
#include <alsa/asoundlib.h>


//Define global constants 
#define PORT "12345"   //TCP port 
#define LOCAL_PORT_C 65432 //TCP visualization port 
#define LOCAL_PORT_P 65433 //TCP playback view
#define SERVER_IP "192.168.1.83"  //loopback address
#define BUFFSIZE 1024  //Buffer size
#define SRATE 44100     //Sample rate in Hz 
#define Channels 2      //Stereo audio 


std::atomic<bool> stop_streaming(false);

//Connect to server function 
int connect_server(const char* server_ip, const char* port) {
    struct addrinfo hints, *servinfo, *p; 
    int sockfd; 
    int rv; 


    //setup hints structure 
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;   //either IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  //TCP socket 


    //get address for server
    if ((rv = getaddrinfo(server_ip, port, &hints, &servinfo)) != 0) {
        std::cerr << "getaddrinfo: " << gai_strerror(rv) << ".\n";
        return -1;

    }


    //Loop through addresses and connect to available
    for (p = servinfo; p != NULL; p = p->ai_next) {

        //Create socket
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client socket error");
            continue; 

        }

        //Connect to server
        if(connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("connection error");
            continue; 

        } 

        break; //connection ok
    }

    if (p == NULL) {
        std::cerr << "failed to connect client\n";
        return -1;

    }

    freeaddrinfo(servinfo); //free list 
    return sockfd;

}



//Function to capture audio 
void audio_cap(int sockfd, int local_sockfd_capture) {
    snd_pcm_t *capture_man;
    snd_pcm_hw_params_t *hw_params;
    int err; 
    char buffer[BUFFSIZE * 2 * Channels];


    //Open ALSA recording 
    if ((err = snd_pcm_open(&capture_man, "default", SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        std::cerr << "capture device error" << snd_strerror(err) << "\n";
        return;

    }


    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(capture_man, hw_params);
    snd_pcm_hw_params_set_access(capture_man, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(capture_man, hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate(capture_man, hw_params, SRATE, 0);
    snd_pcm_hw_params_set_channels(capture_man, hw_params, Channels);
    snd_pcm_hw_params(capture_man, hw_params); 





    //Start audio capture
    while(true) {
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
        if (send(sockfd, buffer, byte_send, 0) == -1) {
            perror("error sending");
            break;
        }

        //Send audio to Python (visualization) 
        if (send(local_sockfd_capture, buffer, byte_send, 0) == -1) {
            perror("Error sending data to Python");
            stop_streaming = true;
            break;
        }
    }    

    snd_pcm_close(capture_man);

}



//Audio playback function 
void play_audio(int sockfd, int local_sockfd_playback) {
    snd_pcm_t *playback_man;
    snd_pcm_hw_params_t *hw_params;
    int err; 
    char buffer[BUFFSIZE * 2 * Channels];


    //Open playback device
    if((err = snd_pcm_open(&playback_man, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        std::cerr << "playback device error" << snd_strerror(err) << "\n";
        return; 

    }

    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(playback_man, hw_params);
    snd_pcm_hw_params_set_access(playback_man, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(playback_man, hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate(playback_man, hw_params, SRATE, 0);
    snd_pcm_hw_params_set_channels(playback_man, hw_params, Channels);
    snd_pcm_hw_params(playback_man, hw_params); 



    //play audio 
    while(true) {
        int byte_num = recv(sockfd, buffer, sizeof(buffer), 0);
        if (byte_num <= 0) {
            if (byte_num == 0) {
                std::cout << "Server closed connection. \n";
            } else {
                perror("recv error");
            }

            break;
        }

        int frames_play = byte_num / (Channels * 2);
       if ((err = snd_pcm_writei(playback_man, buffer, frames_play)) < 0) {
            if (err == -EPIPE) {
                std::cerr << "Buffer underrun occurred: " << snd_strerror(err) << "\n";
                if ((err = snd_pcm_prepare(playback_man)) < 0) {
                    std::cerr << "Error recovering from underrum" << snd_strerror(err) << "\n";
                    break;
                
                }
            } else {
                std::cerr << "write to audio interface failed: " << snd_strerror(err) << "\n";
                break;
            }
        }

        if (send(local_sockfd_playback, buffer, byte_num, 0) == -1) {
            perror("Error sending playback to python");
            break;
        }

    }

    snd_pcm_close(playback_man);
}


int main() {

    //Connect to server
    int sockfd = connect_server(SERVER_IP, PORT);
    if (sockfd == -1) {
        return 1;  //Unable to connnect 

    }

    //Check socket 
    if (sockfd < 0) {
        std::cerr << "Socket creation error. \n";
        return 1;
    }

    //Audio capture socket
    int local_sockfd_capture;     //local socket to connect to Python
    struct sockaddr_in local_addr_capture; 

    //Create local socket
    if((local_sockfd_capture = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Local socket error");
        return 1;
    }

    local_addr_capture.sin_family = AF_INET;
    local_addr_capture.sin_port = htons(LOCAL_PORT_C);
    inet_pton(AF_INET, "127.0.0.1", &local_addr_capture.sin_addr);

    //Connect to visualizer script 
    if (connect(local_sockfd_capture, (struct sockaddr *)&local_addr_capture, sizeof(local_addr_capture)) == -1) {
        perror("Python connection error (capt)");
        return 1;
    }

     //Audio Playback socket
    int local_sockfd_playback;     //local socket to connect to Python
    struct sockaddr_in local_addr_playback; 

    //Create local socket
    if((local_sockfd_playback = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Local socket error");
        return 1;
    }

    local_addr_playback.sin_family = AF_INET;
    local_addr_playback.sin_port = htons(LOCAL_PORT_P);
    inet_pton(AF_INET, "127.0.0.1", &local_addr_playback.sin_addr);

    //Connect to visualizer script 
    if (connect(local_sockfd_playback, (struct sockaddr *)&local_addr_playback, sizeof(local_addr_playback)) == -1) {
        perror("Python connection error (capt)");
        return 1;
    }


    //Start capture and playback threads
    std::thread capture_a(audio_cap, sockfd, local_sockfd_capture);
    std::thread playback_a(play_audio, sockfd, local_sockfd_playback);


   // Join threads before closing socket
    if (capture_a.joinable()) {
        capture_a.join();
    }
    if (playback_a.joinable()) {
        playback_a.join();
    }

    close(sockfd);

    std::cout << "Connection closed.\n";

    return 0;
}
