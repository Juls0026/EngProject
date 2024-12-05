#include "signaling_client.h"
#include <iostream>
#include <thread>

namespace signaling {
    // Constructor to initialize WebSocket client with the host and port
    SignalingClient::SignalingClient(const std::string& host, const std::string& port)
        : resolver_(ioc_), ws_(ioc_) {
        // Resolve the host and connect to the server
        auto const results = resolver_.resolve(host, port);
        boost::asio::connect(ws_.next_layer(), results.begin(), results.end());
        
        // Perform the WebSocket handshake
        ws_.handshake(host, "/");
        
        std::cout << "Connected to signaling server" << std::endl;
    }

    // Method to initiate connection (runs the IO context in a separate thread)
    void SignalingClient::connect() {
        std::thread([this] { ioc_.run(); }).detach();
        read();  // Begin reading messages from the server
    }

    // Method to send a message to the signaling server
    void SignalingClient::send(const std::string& message) {
        ws_.write(boost::asio::buffer(message));
    }

    // Method to set a callback for handling received messages
    void SignalingClient::onMessage(OnMessageCallback callback) {
        on_message_ = std::move(callback);
    }

    // Private method to read messages from the signaling server asynchronously
    void SignalingClient::read() {
        ws_.async_read(buffer_, [this](boost::system::error_code ec, std::size_t bytes_transferred) {
            boost::ignore_unused(bytes_transferred);
            if (!ec) {
                // Convert buffer to string and call the message callback if it's set
                std::string message(boost::beast::buffers_to_string(buffer_.data()));
                if (on_message_) on_message_(message);
                
                buffer_.consume(buffer_.size());  // Clear the buffer after use
                
                // Continue reading the next message
                read();
            } else {
                std::cerr << "Error reading from signaling server: " << ec.message() << std::endl;
            }
        });
    }
}
