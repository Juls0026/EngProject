// Include Libraries

// Standard Libraries         
#include <stdlib.h>        // Standard functions (exit)       
#include <vector>          // Dynamic array
#include <mutex>           // Thread safe operations
#include <cstring>         // String managing (memset, strcpy)
#include <iostream> 
#include <thread> 
#include <algorithm>

// POSIX Libraries (System calls, signal handling, process management)
#include <unistd.h>        // System calls (e.g., read, write, close)
#include <sys/types.h>     // Data types used in system calls (e.g., pid_t, size_t)
#include <sys/wait.h>      // Wait for process state changes (e.g., waitpid)
#include <signal.h>        // Signal handling (e.g., signal, sigaction)
#include <errno.h>         // Error codes (e.g., errno)

// Network Libraries
#include <sys/socket.h>    // Socket API (e.g., socket, bind, listen, accept, send, recv)
#include <arpa/inet.h>     // IP address conversion functions (e.g., inet_pton, inet_ntop)
#include <netinet/in.h>    // Internet address family constants and structures (e.g., sockaddr_in, IP protocols)
#include <netdb.h>         // Network database operations (e.g., gethostbyname, getaddrinfo)

//Audio Library 
#include <alsa/asoundlib.h>  //Audio capture and playback 


//Define Constants
#define SRATE 44100     //Sample rate in Hz ; standard for good audio
#define BUFFSIZE 2048   //Size of buffer for loading audio data
#define PORT "12345"    // TCP port for server
#define BACKLOG 5       //User connection queue 


//Define Global Variables
std::vector <int> clients; // Vector to store all connected client sockets
std::mutex client_m;       // Mutex to protect access to the clients vector


//Function to hanlde clients in different threads
void handle_c(int client_socket) {
    char buffer[BUFFSIZE];
    int byte_num;


    while ((byte_num = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
        
        //Broadcast message to all clients
        std::lock_guard<std::mutex> lock(client_m);       //Lock clients while broadcasting
        for (int others_addr : clients) {
            if (others_addr != client_socket) {
                if (send(others_addr, buffer, byte_num, 0) == -1) {
                    perror("send error");
                }
            }
        }
    
    }

    //Client disconnection 
    if (byte_num == 0) {
        std::cout << "Client disconnected.\n";
    } else if (byte_num == -1) {
        perror("recv error");

    }


    //Remove client from list 
    {
        std::lock_guard<std::mutex> lock(client_m); 
        clients.erase(std::remove(clients.begin(), clients.end(), client_socket), clients.end());

    }

    close(client_socket);


}

//Get rid of zombie processes
void sigchld_handler(int s) {

    //Save and restore errno because waitpid might overwrite it
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0){
        errno = saved_errno; 
    }
}

//Utility to get socket address
void *get_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
    
}




int main() {

    int sockfd, new_fd;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage client_addr;   //connector`s address
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
    int rv;


    // Clear `hints` structure before using it
    memset (&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; 
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; //use my IP 



    //Get address information
    if ((rv = getaddrinfo("192.168.1.83", PORT, &hints, &servinfo)) != 0) {
        std::cerr << "Address info error." << gai_strerror(rv) << "\n";
        return 1; 
    } 
   
    //Bind socket to address (Iterative process)
    for (p = servinfo; p != NULL; p = p->ai_next) {

        //Setup socket 
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("socket error");
            continue; 

        }


        //Socket options (reuse address)
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsocketopt");
            exit(1);
        }


        //Bind socket to address
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd); //Close socket when binding error 
            perror("socket bind error");
            continue; //attempt next address

        }


        break; //when binding works exit loop 

    }
    
    //check that inding worked
    if (p == NULL) {
        std::cerr << "Failed to bind.\n ";
        return 2;
    }

    freeaddrinfo(servinfo); //free addresses list 


    //Listen for clients
    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen error");
        exit(1);
    }

    std::cout << "waiting for connection ...\n";

    
     // Set up the signal handler to reap zombie processes
    sa.sa_handler = sigchld_handler;  // Reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // Main loop: Accept and handle incoming connections
    while (true) {
        sin_size = sizeof client_addr;
        int new_fd = accept(sockfd, (struct sockaddr *)&client_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue; // Skip to the next iteration if `accept()` fails
        }

        // Convert the client address to a human-readable format
        inet_ntop(client_addr.ss_family, get_addr((struct sockaddr *)&client_addr), s, sizeof s);
        std::cout << "Server: got connection from " << s << "\n";

        // Add new client to the list
        {
            std::lock_guard<std::mutex> lock(client_m);
            clients.push_back(new_fd);
        }

        // Create a new thread to handle the client
        std::thread client_thread(handle_c, new_fd);
        client_thread.detach();  // Detach the thread to let it run independently
    }

    // Close the listening socket (though we won't reach here)
    close(sockfd);

    return 0;
}


