import socket
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import errno
import time  # For getting the current timestamp

# Define constants
HOST = '127.0.0.1'           # Localhost
CAPTURE_PORT = 65432         # Port to listen for captured audio (must match the C++ client)
PLAYBACK_PORT = 65433        # Port to listen for received (playback) audio
BUFFER_SIZE = 4096           # Buffer size to receive data
SRATE = 44100                # Sample rate
CHANNELS = 2                 # Stereo audio

# Set up the sockets for receiving audio data
# Socket for captured audio
capture_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
capture_sock.bind((HOST, CAPTURE_PORT))
capture_sock.listen(1)
print("Waiting for connection from C++ client for captured audio...")
capture_conn, capture_addr = capture_sock.accept()
print(f"Connected by {capture_addr} for captured audio.")
capture_conn.setblocking(False)  # Set to non-blocking

# Socket for received (playback) audio
playback_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
playback_sock.bind((HOST, PLAYBACK_PORT))
playback_sock.listen(1)
print("Waiting for connection from C++ client for playback audio...")
playback_conn, playback_addr = playback_sock.accept()
print(f"Connected by {playback_addr} for playback audio.")
playback_conn.setblocking(False)  # Set to non-blocking

# Set up the plotting figure for real-time waveform visualization
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 6))

# Set up x data for both subplots
x_data = np.linspace(0, BUFFER_SIZE // CHANNELS, BUFFER_SIZE // CHANNELS)
y_data_capture = np.zeros(BUFFER_SIZE // CHANNELS)
y_data_playback = np.zeros(BUFFER_SIZE // CHANNELS)

# Set up lines for both subplots
line_capture, = ax1.plot(x_data, y_data_capture, color='blue')
line_playback, = ax2.plot(x_data, y_data_playback, color='green')

# Configure subplot 1: Captured Audio
ax1.set_ylim(-32768, 32767)  # 16-bit audio range
ax1.set_xlim(0, BUFFER_SIZE // CHANNELS)
ax1.set_title("Real-Time Captured Audio Waveform")
ax1.set_xlabel("Sample Index")
ax1.set_ylabel("Amplitude")

# Configure subplot 2: Playback Audio
ax2.set_ylim(-32768, 32767)  # 16-bit audio range
ax2.set_xlim(0, BUFFER_SIZE // CHANNELS)
ax2.set_title("Real-Time Playback Audio Waveform")
ax2.set_xlabel("Sample Index")
ax2.set_ylabel("Amplitude")

# List to store latency values
latency_values = []

# Function to update the visualization
def update(frame):
    global capture_conn, playback_conn
    try:
        # Receive combined data (timestamp + audio data)
        combined_data = capture_conn.recv(BUFFER_SIZE + 8)  # 8 bytes for timestamp + audio buffer

        if combined_data:
            # Extract timestamp
            timestamp = int.from_bytes(combined_data[:8], byteorder='little')
            current_timestamp = int(time.time() * 1000)  # Current time in milliseconds
            latency = current_timestamp - timestamp
            latency_values.append(latency)  # Record latency
            print(f"Captured audio latency: {latency} ms")

            # Extract audio data
            audio_data = combined_data[8:]
            samples_capture = np.frombuffer(audio_data, dtype=np.int16)
            line_capture.set_ydata(samples_capture)

    except socket.error as e:
        if e.errno != errno.EAGAIN and e.errno != errno.EWOULDBLOCK:
            print(f"Socket error (captured audio): {e}")
            capture_conn.close()

    try:
        # Receive audio data from the C++ client (playback audio)
        playback_data = playback_conn.recv(BUFFER_SIZE)
        if playback_data:
            samples_playback = np.frombuffer(playback_data, dtype=np.int16)
            line_playback.set_ydata(samples_playback)
            print("Received playback audio data")  # Debugging output

    except socket.error as e:
        if e.errno != errno.EAGAIN and e.errno != errno.EWOULDBLOCK:
            print(f"Socket error (playback audio): {e}")
            playback_conn.close()

    return line_capture, line_playback


# Set up the animation
ani = FuncAnimation(fig, update, blit=True, interval=50)
plt.tight_layout()
plt.show()

# Close the connection after visualization ends
capture_conn.close()
playback_conn.close()
capture_sock.close()
playback_sock.close()

# Plot the latency graph after the connection ends
if latency_values:
    plt.figure(figsize=(10, 5))
    plt.plot(latency_values, color='red', label='Latency (ms)')
    plt.xlabel('Frame')
    plt.ylabel('Latency (ms)')
    plt.title('Streaming Latency Over Time')
    plt.legend()
    plt.grid()
    plt.show()

    # Optionally save the latency data to a file for further analysis
    with open("latency_data.txt", "w") as f:
        for latency in latency_values:
            f.write(f"{latency}\n")

    print("Latency data saved to latency_data.txt")
