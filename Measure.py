import socket
import struct
import time
from threading import Thread, Lock

UDP_PORT = 12345
BUFFER_SIZE = 65536

latency_lock = Lock()
bandwidth_lock = Lock()

latency_measurements = []
bandwidth_measurements = []

running = True

def receive_packets():
    global running
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", UDP_PORT))
    print(f"Listening for packets on port {UDP_PORT}...")

    while running:
        try:
            data, addr = sock.recvfrom(BUFFER_SIZE)
            timestamp = time.time()  # Record the time we received the packet

            # Parse packet to get the original timestamp
            if len(data) >= 12:  # Ensure we have enough data to parse
                # Assuming that the first 4 bytes are seq_num and the next 8 bytes are the timestamp
                seq_num, packet_timestamp = struct.unpack("!Iq", data[:12])
                latency = (timestamp - packet_timestamp / 1_000_000.0) * 1000  # Convert latency to milliseconds
                
                with latency_lock:
                    latency_measurements.append(latency)
                    
                # Print the latency measurement (for debugging purposes)
                print(f"Packet {seq_num} latency: {latency:.2f} ms")

            # Update bandwidth statistics
            with bandwidth_lock:
                bandwidth_measurements.append(len(data))

        except socket.error as e:
            print(f"Socket error: {e}")
            running = False

    sock.close()

def report_metrics():
    global running
    while running:
        time.sleep(1)  # Report metrics every second
        
        with latency_lock:
            if latency_measurements:
                avg_latency = sum(latency_measurements) / len(latency_measurements)
                print(f"Average latency over the last second: {avg_latency:.2f} ms")
                latency_measurements.clear()
        
        with bandwidth_lock:
            if bandwidth_measurements:
                total_bytes = sum(bandwidth_measurements)
                bandwidth_mbps = (total_bytes * 8) / (1024 * 1024)
                print(f"Bandwidth requirement over the last second: {bandwidth_mbps:.2f} Mbps")
                bandwidth_measurements.clear()

def main():
    global running
    receiver_thread = Thread(target=receive_packets)
    reporter_thread = Thread(target=report_metrics)

    receiver_thread.start()
    reporter_thread.start()

    try:
        print("Press Enter to stop...")
        input()
    except KeyboardInterrupt:
        pass
    finally:
        running = False

    receiver_thread.join()
    reporter_thread.join()

if __name__ == "__main__":
    main()
