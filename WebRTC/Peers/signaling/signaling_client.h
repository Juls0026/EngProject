#ifndef SIGNALING_CLIENT_H
#define SIGNALING_CLIENT_H

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <string>
#include <functional>

namespace signaling {
    // Define a callback type for messages received from the signaling server
    using OnMessageCallback = std::function<void(const std::string&)>;

    class SignalingClient {
    public:
        // Constructor to initialize the signaling client with a server host and port
        SignalingClient(const std::string& host, const std::string& port);
        
        // Connect to the signaling server
        void connect();
        
        // Send a message to the signaling server
        void send(const std::string& message);
        
        // Set a callback function that will be called when a message is received
        void onMessage(OnMessageCallback callback);

    private:
        // Private method for reading messages from the signaling server
        void read();

        // Member variables
        boost::asio::io_context ioc_; // IO context used by Boost ASIO
        boost::asio::ip::tcp::resolver resolver_; // Resolver for hostname resolution
        boost::beast::websocket::stream<boost::asio::ip::tcp::socket> ws_; // WebSocket stream
        boost::beast::flat_buffer buffer_; // Buffer to store received data
        OnMessageCallback on_message_; // Callback for incoming messages
    };
}

#endif
