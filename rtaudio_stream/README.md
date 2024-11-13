# Real-Time Audio Streaming with ALSA and UDP

This project enables real-time audio capture, transmission, and playback using ALSA and UDP sockets in C++. The application is divided into two main components:

- **Sender**: Captures audio from the microphone and sends it to the receiver via UDP.
- **Receiver**: Receives the audio stream and plays it back in real-time.

## Setup

1. **Compile the Programs**: To build the sender and receiver programs, use the provided `Makefile` (or create one if needed).
   ```bash
   make
