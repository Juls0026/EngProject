import socket
import time
import struct
import threading
from datetime import datetime



# Constants
UDP_PORT = 12345          #Broadcast port 
BUFFER_SIZE = 131072    
LOG_INTERVAL = 1          # Log results every second 



# Initialize Variables 
lat_data = []
band_data = []
start_t = time.time()
Bytes = 0
lock = threading.Lock()

def log_data():
    """Log latency and bandwidth data"""
    global lat_data, band_data


    #Save latency data
    with open("latency_d.csv", "w") as f:
        f.write("Timestamp, Latency (ms)\n")
        for timestamp, latency in lat_data:
            f.write(f"{timestamp},{latency:.2f}\n")


    #Save bandwidth data
    with open("bandwidth.csv", "w") as f:
        f.write("Timestamp, Bandwidth (ms)\n")
        for timestamp, bandwidth in band_data:
            f.write(f"{timestamp},{bandwidth:.2f}\n")

    print("Results logged to latency_d.csv  and Bandwidth_d.csv")



def calculate_data():
    """Logs latency and badnwidth as functions of time"""
    global lat_data, band_data, Bytes


    while True: 
        time.sleep(LOG_INTERVAL)


        #Bandwidth calculation 
        with lock: 
            bandwith = (Bytes / 1024) / LOG_INTERVAL  #KBps
            Bytes = 0 #Reset counter


        #Log Results
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        if lat_data: 
            latestL_data = lat_data[-1][1]  #latency
            print(f"{timestamp} - Latency: {latestL_data: .2f} ms, Bandwidth: {bandwith: .2f} KBps")
            band_data.append((timestamp, bandwith))


def receive_pack():
    """Receive packs and extract timestamps"""
    global Bytes, lat_data

    #Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", UDP_PORT))

    print(f"Listen for packets on UDP port {UDP_PORT}")

    while True: 
        # Receive packet
        data, addr = sock.recvfrom(BUFFER_SIZE)
        recv_time = time.time()
        Bytes += len(data)


        #Unpack data
        try: 
            #Adjust structure format to match packet's 
            header_size = struct.calcsize("I Q")
            seq_num, timestamp = struct.unpack_from("I Q", data[:header_size])

            #Calculate Latency 
            latency = (recv_time -timestamp / 1e6) * 1000 #Convert to ms
            timestamp_formatted = datetime.fromtimestamp(timestamp / 1e6).strftime("%Y-%m-%d %H:%M:%S.%f")
            lat_data.append((timestamp_formatted, latency))
        except struct.error: 
            print(f"Failed to unpack data from {addr}")


#Main Program 
if __name__ == "__main__" : 
    try: 
        #start a thread for logging results
        logging_thread = threading.Thread(target=calculate_data, daemon=True)
        logging_thread.start()


        #Receive packets
        receive_pack()
    except KeyboardInterrupt: 
        print("\nShutting down...")
        log_data()




        