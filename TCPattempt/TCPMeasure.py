import socket
import time
import argparse
from threading import Thread, Lock

BUFFER_SIZE = 2048

latency_lock = Lock()
bandwidth_lock = Lock()

latency_measurements = []
bandwidth_measurements = []

running = True

def measure_latency_and_bandwidth(server_ip, port):
    global running

    # Create a TCP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((server_ip, port))
    print(f"Connected to server at {server_ip}:{port}")

    # Send/receive data for latency and bandwidth measurement
    while running:
        try:
            # Send a timestamp packet to measure round-trip time (RTT)
            send_timestamp = time.time()
            sock.sendall(send_timestamp.to_bytes(8, byteorder='big', signed=False))

            # Receive the echoed packet back
            data = sock.recv(8)
            if len(data) == 8:
                receive_timestamp = time.time()
                returned_timestamp = float.fromhex(data.hex())

                # Calculate round-trip time
                latency = (receive_timestamp - returned_timestamp) * 1000  # Latency in ms

                with latency_lock:
                    latency_measurements.append(latency)

            # Update bandwidth measurements
            total_bytes = 0
            start_time = time.time()

            while time.time() - start_time < 1:  # Measure for 1 second
                data = sock.recv(BUFFER_SIZE)
                if not data:
                    break
                total_bytes += len(data)

            bandwidth_mbps = (total_bytes * 8) / (1024 * 1024)  # Convert to Mbps

            with bandwidth_lock:
                bandwidth_measurements.append(bandwidth_mbps)

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
                avg_bandwidth = sum(bandwidth_measurements) / len(bandwidth_measurements)
                print(f"Average bandwidth over the last second: {avg_bandwidth:.2f} Mbps")
                bandwidth_measurements.clear()

def main():
    global running

    # Parse command-line arguments
    parser = argparse.ArgumentParser(description="Measure TCP streaming latency and bandwidth.")
    parser.add_argument("--server_ip", type=str, required=True, help="Server IP address.")
    parser.add_argument("--port", type=int, required=True, help="TCP port to connect to.")

    args = parser.parse_args()

    # Start threads for packet reception and metric reporting
    measurement_thread = Thread(target=measure_latency_and_bandwidth, args=(args.server_ip, args.port))
    reporter_thread = Thread(target=report_metrics)

    measurement_thread.start()
    reporter_thread.start()

    try:
        print("Press Enter to stop...")
        input()
    except KeyboardInterrupt:
        pass
    finally:
        running = False

    measurement_thread.join()
    reporter_thread.join()

if __name__ == "__main__":
    main()
