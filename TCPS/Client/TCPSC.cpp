//Include liraries 

//Standard libraries
#include <iostream>  //I/O 
#include <cstring>   //String manipulation
#include <vector>    //Dynamic array 
#include <thread>    //Multi-thread
#include <mutex>     //Thread safety variables
#include <atomic>

#include <signal.h> 
#include <chrono>  //Add timestamps for measurements

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
#define PORT 12345   //TCP port 
#define LOCAL_PORT_C 65432 //TCP visualization port 
#define LOCAL_PORT_P 65433 //TCP playback view
#define SERVER_IP "192.168.1.83"  //loopback address
#define BUFFSIZE 1024  //Buffer size
#define SRATE 44100     //Sample rate in Hz 
#define Channels 2      //Stereo audio 


std::atomic<bool> stop_streaming(false);

//Connect to server function 
int connect_server(const char* server_ip, int port, int retries, int delay_second) {
    int sockfd; 
    struct sockaddr_in server_addr;

    while (retries-- > 0) {
        //Create socket
        if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            perror("client socket error");
            std::this_thread::sleep_for(std::chrono::seconds(delay_second));
            continue;

        }

        //Set up server address struct
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(PORT);
        if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
            perror("Invalid address or not supported");
            close(sockfd);
            std::this_thread::sleep_for(std::chrono::seconds(delay_second));
            continue;

        }

        //Conect to server 
        if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
            perror("connect error");
            close(sockfd);
            std::this_thread::sleep_for(std::chrono::seconds(delay_second));
            continue;

        }

        //Connection succesful
        return sockfd;

    }
    //If all retries fail
    std::cerr << "Failed after retries.\n";
    return -1;

}



//Function to capture audio 
void audio_cap(int sockfd, int local_sockfd_capture) {
    snd_pcm_t *capture_man;
    snd_pcm_hw_params_t *hw_params;
    int err; 
    char buffer[BUFFSIZE * 2 * Channels];
    int64_t timestamp; 


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

         // Get the current timestamp in milliseconds
        timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()
                    ).count();

        //Send audio on socket 
        int byte_send = fr_capture * 2 * Channels; //Calculate bytes sent
        if (send(sockfd, buffer, byte_send, 0) == -1) {
            perror("error sending");
            stop_streaming = true;
            break;
        }

        //Combine timestamp and buffer into one packet
        std::vector<char> packet(sizeof(timestamp) + byte_send);

        // Copy timestamp into packet
        memcpy(packet.data(), &timestamp, sizeof(timestamp));

        // Copy audio buffer into packet
        memcpy(packet.data() + sizeof(timestamp), buffer, byte_send);

        // Send the combined packet to Python visualizer
        if (send(local_sockfd_capture, packet.data(), packet.size(), 0) == -1) {
            perror("Error sending data to Python (timestamp + audio)");
            stop_streaming = true;
            break;
        }
    }    

    snd_pcm_close(capture_man);
    close(sockfd);           // Close main server socket
    close(local_sockfd_capture);  // Close local socket properly

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
        int byte_num = recv(sockfd, buffer, sizeof(buffer), 0);
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

        //Python visualizer send
        if (send(local_sockfd_playback, buffer, byte_num, 0) == -1) {
            if (errno == EPIPE) {
                std::cerr << "Python visualizer disconnected. Playback pipe";
            } else {
                perror("Error sending playback to python");
            }
            stop_streaming = true;
            break;
        }

    }

    snd_pcm_close(playback_man); 
    close(local_sockfd_playback);
}


int main() {
    //Ignore SIGPIPE
    signal(SIGPIPE, SIG_IGN);

    //Retry configuration
    const int max_retries = 5;
    const int delay_second = 2;


    // Connect to server
    int sockfd = connect_server(SERVER_IP, PORT, max_retries, delay_second);
    if (sockfd == -1) {
        std::cerr << "Server connect error.\n";
        return  1;

    }

    //Audio capture
    int local_sockfd_cap = -1; 
    struct sockaddr_in local_addr_capture; 

    //Socket for Python script 
    if ((local_sockfd_cap = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Python socket error");

    } else {
        memset(&local_addr_capture, 0, sizeof(local_addr_capture));
        local_addr_capture.sin_family = AF_INET;
        local_addr_capture.sin_port = htons(LOCAL_PORT_C);
        inet_pton(AF_INET, "127.0.0.1", &local_addr_capture.sin_addr);

        int retries = max_retries;
        while (retries-- > 0) {
            if (connect(local_sockfd_cap, (struct sockaddr *)&local_addr_capture, sizeof(local_addr_capture)) == 0) {
                std::cout << "Python View connection successful.\n";
                break; 
            } else {
                perror("Python connection error");
                std::this_thread::sleep_for(std::chrono::seconds(delay_second));

            }
        }


        if (retries <= 0) {
            std::cerr << "Failed to connect to Python.\n";
            local_sockfd_cap = -1; 

        }
    }

     // Audio playback socket connection (to Python script for visualization)
    int local_sockfd_playback = -1;
    struct sockaddr_in local_addr_playback;

    // Create and connect playback socket for Python visualizer
    if ((local_sockfd_playback = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Local socket error (playback)");
    } else {
        memset(&local_addr_playback, 0, sizeof(local_addr_playback));
        local_addr_playback.sin_family = AF_INET;
        local_addr_playback.sin_port = htons(LOCAL_PORT_P);
        inet_pton(AF_INET, "127.0.0.1", &local_addr_playback.sin_addr);

        int retries = max_retries;
        while (retries-- > 0) {
            if (connect(local_sockfd_playback, (struct sockaddr *)&local_addr_playback, sizeof(local_addr_playback)) == 0) {
                std::cout << "Python playback view successful (playback).\n";
                break;
            } else {
                perror("Python connection error (playback)");
                std::this_thread::sleep_for(std::chrono::seconds(delay_second));
            }
        }

        if (retries <= 0) {
            std::cerr << "Failed to connect to Python playback view after retries.\n";
            local_sockfd_playback = -1;  // Mark as invalid if connection not successful
        }
    }

    // Start capture and playback threads
    std::thread capture_a(audio_cap, sockfd, local_sockfd_cap);
    std::thread playback_a(play_audio, sockfd, local_sockfd_playback);

    // Wait for user to terminate the transmission via a keyboard command (e.g., pressing Ctrl+C)
    std::cout << "Press Ctrl+C to stop streaming..." << std::endl;
    
    // Join threads before closing socket
    if (capture_a.joinable()) {
        capture_a.join();
    }
    if (playback_a.joinable()) {
        playback_a.join();
    }

    // Close all open sockets
    close(sockfd);
    if (local_sockfd_cap != -1) {
        close(local_sockfd_cap);
    }
    if (local_sockfd_playback != -1) {
        close(local_sockfd_playback);
    }

    std::cout << "Connection closed.\n";

    return 0;

    

    
}    