import socket
import time
import numpy as np
import errno
import signal
import struct  # <-- Add this import for unpacking the timestamp

import matplotlib
matplotlib.use('TkAgg') 
from matplotlib import pyplot as plt

# Define constants
LOCAL_PORT = 65432
BUFFER_SIZE = 4096
MAX_LATENCY_POINTS = 1000  # Maximum number of latency checkpoints before auto-termination

# Store variables
latency_values = []
bandwidth_values = []
timestamps = []

# Listen on port for data
def listen_on_port(port): 
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s: 
        s.bind(("127.0.0.1", port))
        s.listen()

        print(f"Listening on port {port}")
        conn, addr = s.accept()
        print('Connected by', addr)
        while True: 
            try: 
                # Receive timestamp and audio data
                header = conn.recv(8)  # 8 bytes for timestamp
                if len(header) < 8: 
                    print("Connection closed. Plotting data.")
                    break

                # Unpack timestamp
                timestamp = struct.unpack('q', header)[0]

                # Receive audio data
                data = conn.recv(1024)  # Receive audio
                if not data: 
                    print("Connection closed.")
                    break

                # Calculate latency and bandwidth
                current_time = int(time.time() * 1000) 
                latency = current_time - timestamp 
                bandwidth = len(data) / 1024.0  # KBps (based on the buffer size received)

                # Store latency and bandwidth values for plotting later
                timestamps.append(current_time)
                latency_values.append(latency)
                bandwidth_values.append(bandwidth)

                # Limit the number of points to avoid excessive memory usage
                if len(latency_values) >= MAX_LATENCY_POINTS:
                    print("Maximum latency points reached. Stopping collection.")
                    break

            except socket.error as e: 
                print(f"Socket error: {e}")
                break

# Listen and collect data from capture socket
listen_on_port(LOCAL_PORT)

# Plot latency and bandwidth
plt.figure(figsize=(12, 6))

# Plot latency
plt.subplot(2, 1, 1) 
plt.plot(timestamps, latency_values, label='Latency (ms)', color='b')
plt.xlabel('Time (ms)')
plt.ylabel('Latency (ms)')
plt.title('Latency Over Time')
plt.legend()

# Plot bandwidth
plt.subplot(2, 1, 2)
plt.plot(timestamps, bandwidth_values, label='Bandwidth (KBps)', color='r')
plt.xlabel('Time (ms)')
plt.ylabel('Bandwidth (KBps)')
plt.title('Bandwidth Over Time')
plt.legend()

plt.tight_layout()
plt.savefig('latency_bandwidth_plot.png')
print("Latency and bandwidth plots saved as 'latency_bandwidth_plot.png'")


