import socket
import time
import numpy as np
import errno
import signal

import matplotlib
matplotlib.use('TkAgg') 
from matplotlib import pyplot as plt

# Define constants
LOCAL_PORT = 65432
BUFFER_SIZE = 4096
MAX_LATENCY_POINTS = 1000  # Maximum number of latency checkpoints before auto-termination


#store variables 
latency_values = []
bandwidth_values = []
timestamps = []



#Listen on port for data 
def listen_on_port(port): 
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s: 
        s.bind((LOCAL_PORT, port))
        s.listen()

        print(f"Listenning on {port}")
        conn, addr = s.accept()
        print('Connected on', addr)
        while True: 
            try: 
                #Recive timestamp and audio data
                header = conn.recv(8) #8 bytes for timestamp
                if len(header) < 8: 
                    print("Connection closed. Plotting data")
                    break

                timestamp = struct.unpack('q', header)[0] #Unpack timestamp

                data = conn.recv(1024) #Receive audio 
                if not data: 
                    print("Connection closed")
                    break

                #Calculate latency and bandwidth
                current_time = int(time.time() * 1000) 
                latency = current_time - timestamp 
                bandwidth = len(data) / 1024.0 #KBps (based on the buffer size received)


                # Store latency and bandwidth values for plotting later 
                timestamps.append(current_time)
                latency_values.append(latency)
                bandwidth_values.append(bandwidth)

            except socket.error as e: 
                print(f"Socket error: {e}")
                break


# Listen and collect data from capture socket 
listen_on_port(LOCAL_PORT)


#Plot latency and bandwidth 
plt.figure(figsize=(12, 6))

plt.subplot(2, 1, 1) 
plt.plot(timestamps, latency_values, label='Latency (ms)', color='b')
plt.xlabel('Time (ms)')
plt.ylabel('Latency (ms)')
plt.title('Latency Over Time')
plt.legend()


plt.tight_layout()
plt.savefig('latency_bandwidth_plot.png')
print("Latency and bandwidth plots saved as 'latency_bandwidth_plot.png")


            

