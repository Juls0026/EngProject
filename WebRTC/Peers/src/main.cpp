#include <api/create_peerconnection_factory.h>
#include <api/peer_connection_interface.h>
#include <rtc_base/thread.h>
#include <rtc_base/ssl_adapter.h>
#include "signaling_client.h"
#include <iostream>
#include <memory>

class CustomPeerConnectionObserver : public webrtc::PeerConnectionObserver {
public:
    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) override {
        std::cout << "Signaling state changed: " << new_state << std::endl;
    }

    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override {
        std::string sdp;
        candidate->ToString(&sdp);
        std::cout << "New ICE Candidate: " << sdp << std::endl;
        signaling_client_->send(sdp);
    }

    void setSignalingClient(std::shared_ptr<signaling::SignalingClient> signaling_client) {
        signaling_client_ = signaling_client;
    }

private:
    std::shared_ptr<signaling::SignalingClient> signaling_client_;
};

void initialize_webrtc(std::shared_ptr<signaling::SignalingClient> signaling_client) {
    rtc::InitializeSSL();
    auto network_thread = rtc::Thread::CreateWithSocketServer().release();
    auto worker_thread = rtc::Thread::Create().release();
    auto signaling_thread = rtc::Thread::Create().release();

    network_thread->Start();
    worker_thread->Start();
    signaling_thread->Start();

    auto peer_connection_factory = webrtc::CreatePeerConnectionFactory(
        network_thread, worker_thread, signaling_thread, nullptr,
        webrtc::CreateBuiltinAudioEncoderFactory(),
        webrtc::CreateBuiltinAudioDecoderFactory(),
        nullptr, nullptr
    );

    if (!peer_connection_factory) {
        std::cerr << "Failed to initialize PeerConnectionFactory" << std::endl;
        return;
    }

    webrtc::PeerConnectionInterface::RTCConfiguration config;
    webrtc::PeerConnectionInterface::IceServer stun_server;
    stun_server.uri = "stun:stun.l.google.com:19302";
    config.servers.push_back(stun_server);

    rtc::scoped_refptr<CustomPeerConnectionObserver> observer =
        new rtc::RefCountedObject<CustomPeerConnectionObserver>();
    observer->setSignalingClient(signaling_client);

    auto peer_connection = peer_connection_factory->CreatePeerConnection(config, nullptr, nullptr, observer);

    if (!peer_connection) {
        std::cerr << "Failed to create PeerConnection" << std::endl;
        return;
    }

    rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track(
        peer_connection_factory->CreateAudioTrack("audio_label",
        peer_connection_factory->CreateAudioSource(cricket::AudioOptions())));
    peer_connection->AddTrack(audio_track, { "stream_id" });
}

int main() {
    auto signaling_client = std::make_shared<signaling::SignalingClient>("localhost", "8080");
    signaling_client->connect();

    initialize_webrtc(signaling_client);

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
