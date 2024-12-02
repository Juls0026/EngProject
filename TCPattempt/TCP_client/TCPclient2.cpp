//Include liraries 

//Standard libraries
#include <iostream>  //I/O 
#include <cstring>   //String manipulation
#include <vector>    //Dynamic array 
#include <thread>    //Multi-thread
#include <mutex>     //Thread safety variables


//POSIX libraries 
#include <unistd.h>  //System calls
#include <sys/types.h> //Data types for system calls


//Network libraries
#include <sys/socket.h> //Socket API 
#include <netdb.h>    //Network database operations 
#include <netinet/in.h> //address 
#include <arpa/inet.h>  //IP address convertion



//Define global constants 
#define PORT "12345"   //TCP port 
#define SERVER_IP "127.0.0.1"  //loopback address
#define BUFFSIZE 2048   //Buffer size


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



//Send a message to server
void send_msg(int sockfd, const char* message) {
    if (send(sockfd,  message, strlen(message), 0) == -1) {
        perror("send msg error");

    }

}


//Receive message from server
void receive_msg(int sockfd){
    char buffer[BUFFSIZE];
    int byte_num; 

    while ((byte_num = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[byte_num] = '\0'; //stop receiving data 
        std::cout << "Server: " << buffer << "\n"; 

    }

    if(byte_num == -1) {
           perror("recv error");
    } else if (byte_num == 0) {
        std::cout << "Server closed the connection.\n";
    }
    
}


int main() {

    //Connect to server
    int sockfd = connect_server(SERVER_IP, PORT);
    if (sockfd == -1) {
        return 1;  //Unable to connnect 

    }


    //Message send to server
    const char* send_hello = "HELLO"; 
    send_msg(sockfd, send_hello); 


    //Receive messages from server
    std::thread receive_data(receive_msg, sockfd); 
    receive_data.detach(); //Thread runs independently


    // Sending messages in a loop (You can customize this)
    std::string message;
    while (true) {
        std::getline(std::cin, message);  // Get user input
        if (message == "/quit") {         // Allow the client to quit gracefully
            break;
        }
        send_msg(sockfd, message.c_str());
    }

    // Close the socket and clean up
    close(sockfd);
    std::cout << "Connection closed.\n";

    return 0;
}
