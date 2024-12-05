import socket
import time
import matplotlib.pyplot as plt
import numpy as np
import errno
import signal

# Define constants
LOCAL_PORT = 65432
BUFFER_SIZE = 4096
MAX_LATENCY_POINTS = 1000  # Maximum number of latency checkpoints before auto-termination

# Set up the socket for incoming connections
server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server_socket.bind(("127.0.0.1", LOCAL_PORT))
server_socket.listen(1)

print(f"Listening for connections on port {LOCAL_PORT}...")

# Accept a connection from the C++ client
conn, addr = server_socket.accept()
print(f"Connected by {addr}")

# To store latency values
latency_values = []

# Variable to handle streaming status
stop_streaming = False

def signal_handler(sig, frame):
    """Handles the Ctrl+C to stop the program gracefully"""
    global stop_streaming
    print("\nCtrl+C pressed. Stopping the streaming gracefully...")
    stop_streaming = True

# Register the signal handler for SIGINT (Ctrl+C)
signal.signal(signal.SIGINT, signal_handler)

try:
    while not stop_streaming:
        # Receive combined data (timestamp + audio data)
        combined_data = conn.recv(BUFFER_SIZE + 8)

        # Check if enough data is received (at least 8 bytes for timestamp)
        if len(combined_data) < 8:
            # If no data received, skip and continue waiting
            if len(combined_data) == 0:
                continue
            else:
                print("Received incomplete packet; ignoring.")
                continue

        # Extract the timestamp (first 8 bytes)
        timestamp = int.from_bytes(combined_data[:8], byteorder='little')

        # Current time in milliseconds
        current_timestamp = int(time.time() * 1000)

        # Calculate latency
        latency = current_timestamp - timestamp
        latency_values.append(latency)

        # Debugging output every 10 samples
        if len(latency_values) % 10 == 0:
            print(f"Latency recorded: {latency} ms")

        # If we reach the maximum number of latency points, stop streaming
        if len(latency_values) >= MAX_LATENCY_POINTS:
            print(f"Reached maximum latency checkpoints of {MAX_LATENCY_POINTS}.")
            stop_streaming = True

except socket.error as e:
    if e.errno != errno.EAGAIN and e.errno != errno.EWOULDBLOCK:
        print(f"Socket error: {e}")

# Close the connection once the streaming ends
conn.close()
server_socket.close()
print("Connection closed. Plotting latency data...")

# Plot the latency values collected during the streaming session
plt.figure(figsize=(10, 5))
plt.plot(latency_values, color='b', label='Latency (ms)')
plt.xlabel('Sample Index')
plt.ylabel('Latency (ms)')
plt.title('Latency Over Time During Audio Streaming')
plt.grid(True)
plt.legend()

# Show the latency plot
plt.show()
