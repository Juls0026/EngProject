import socket
import time
import matplotlib.pyplot as plt

# Configuration
RECEIVER_IP = "192.168.1.85"  # Change to your receiver IP
RECEIVER_PORT = 54321          # Receiver port
LOCAL_PORT = 12345             # Local port for listening

# Create socket for receiving data
recv_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
recv_socket.bind(("", LOCAL_PORT))

# Variables to store latency data
latencies = []

# Start receiving data
try:
    for i in range(100):  # Collect latency for 100 packets
        recv_socket.settimeout(5.0)  # Set timeout for receiving
        try:
            data, addr = recv_socket.recvfrom(4096)  # Buffer size to accommodate timestamp + audio data
            # Split the timestamp from the actual data
            data_str = data.decode('utf-8', errors='ignore')
            timestamp_str, _ = data_str.split('|', 1)
            
            # Calculate the latency
            send_timestamp = int(timestamp_str)
            receive_timestamp = time.time_ns()  # Get current time in nanoseconds
            latency = (receive_timestamp - send_timestamp) / 1e6  # Convert to milliseconds
            latencies.append(latency)

            print(f"Packet latency: {latency:.2f} ms")
        except socket.timeout:
            print("Packet not received within timeout period.")

        time.sleep(0.1)  # Slight delay between packets

except KeyboardInterrupt:
    print("Interrupted by user.")
finally:
    # Close socket
    recv_socket.close()

    # Plot the latencies
    if latencies:
        plt.figure(figsize=(10, 5))
        plt.plot(latencies, label='Packet Latency (ms)', color='blue')
        plt.xlabel('Packet Number')
        plt.ylabel('Latency (ms)')
        plt.title('UDP Packet Latency')
        plt.legend()
        plt.grid(True)
        plt.show()
