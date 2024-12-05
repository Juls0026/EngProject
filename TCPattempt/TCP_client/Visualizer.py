import socket
import time
import numpy as np
import errno
import signal

import matplotlib
matplotlib.use('Agg') 
from matplotlib import pyplot as plt

# Define constants
LOCAL_PORT = 65432
BUFFER_SIZE = 4096
MAX_LATENCY_POINTS = 1000  # Maximum number of latency checkpoints before auto-termination


#store variables 
latency_values = []
bandwidth_values = []
timestamps = []


stop_streaming = False #help close program gracefully 

def signal_man(sig, frame): 
    """stop programm gracefully"""
    global stop_streaming
    print("\nCtrl+C stop streaming")
    stop_streaming = True

#Register signal 
signal.signal(signal.SIGINT, signal_man)

#Local socket comms setup 
server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server_socket.bind(("127.0.0.1", LOCAL_PORT))
server_socket.listen(1)

print(f"Listening")

#Accept connection 
conn, addr = server_socket.accept()
print(f"Connected by {addr}")


start_time = time.time() #Start time of transmission 

try:
    while not stop_streaming:
        # Receive combined data (timestamp + audio data)
        combined_data = conn.recv(BUFFER_SIZE)

        # Check if enough data is received (at least 8 bytes for timestamp)
        if len(combined_data) < 8:
            # If no data received, assume the client has closed the connection
            if len(combined_data) == 0:
                break
            else:
                print("Received incomplete packet; ignoring.")
                continue

        # Extract the timestamp (first 8 bytes, assuming little-endian format)
        timestamp = struct.unpack('<q', combined_data[:8])[0]

        # Current time in milliseconds
        current_timestamp = int(time.time() * 1000)

        # Calculate latency
        latency = current_timestamp - timestamp
        latency_values.append(latency)

        # Calculate the amount of data received in bytes (excluding the timestamp)
        data_size = len(combined_data) - 8

        # Calculate the time elapsed since start
        elapsed_time = time.time() - start_time
        timestamps.append(elapsed_time)

        # Calculate bandwidth in bytes per second
        if elapsed_time > 0:
            bandwidth = data_size / elapsed_time
            bandwidth_values.append(bandwidth)

        # Debugging output every 10 samples
        if len(latency_values) % 10 == 0:
            print(f"Latency recorded: {latency} ms, Bandwidth: {bandwidth:.2f} Bps")

except socket.error as e:
    print(f"Socket error: {e}")

# Close the connection once the streaming ends
conn.close()
server_socket.close()
print("Connection closed. Plotting latency and bandwidth data...")

# Plot the latency values collected during the streaming session
plt.figure(figsize=(12, 6))

# Plot latency
plt.subplot(2, 1, 1)
plt.plot(timestamps, latency_values, color='b', label='Latency (ms)')
plt.xlabel('Time (s)')
plt.ylabel('Latency (ms)')
plt.title('Latency Over Time During Audio Streaming')
plt.grid(True)
plt.legend()

# Plot bandwidth
plt.subplot(2, 1, 2)
plt.plot(timestamps, bandwidth_values, color='g', label='Bandwidth (Bps)')
plt.xlabel('Time (s)')
plt.ylabel('Bandwidth (Bytes per Second)')
plt.title('Bandwidth Over Time During Audio Streaming')
plt.grid(True)
plt.legend()

# Show the latency and bandwidth plot
plt.tight_layout()
plt.show()