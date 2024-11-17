#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 12345

int main() {
    int sock = 0;
    sockaddr_in server_addr;
    char buffer[1024] = {0};

    // 1. Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cerr << "Socket creation failed\n";
        return 1;
    }

    // 2. Define server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    // Convert IP address to binary form
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address or address not supported\n";
        return 1;
    }

    // 3. Connect to the server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Connection failed\n";
        return 1;
    }

    std::cout << "Connected to server\n";

    // 4. Communication: Send and receive data
    while (true) {
        std::cout << "Enter message: ";
        std::string message;
        std::getline(std::cin, message);

        if (message == "exit") break;

        send(sock, message.c_str(), message.size(), 0);

        memset(buffer, 0, sizeof(buffer));
        int valread = read(sock, buffer, sizeof(buffer));
        if (valread <= 0) {
            std::cout << "Server disconnected\n";
            break;
        }

        std::cout << "Server replied: " << buffer << "\n";
    }

    // 5. Close the socket
    close(sock);

    return 0;
}
