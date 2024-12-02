import socket
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation

# Define constants
HOST = '127.0.0.1'  # Localhost
PORT = 65432        # Port to listen on (must match the C++ client)
BUFFER_SIZE = 4096  # Buffer size to receive data
SRATE = 44100       # Sample rate
CHANNELS = 2        # Stereo audio

# Set up the socket for receiving audio data
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.bind((HOST, PORT))
sock.listen(1)
print("Waiting for connection from C++ client...")
conn, addr = sock.accept()
print(f"Connected by {addr}")

# Set up the plotting figure
fig, ax = plt.subplots()
x_data = np.linspace(0, BUFFER_SIZE // CHANNELS, BUFFER_SIZE // CHANNELS)
y_data = np.zeros(BUFFER_SIZE // CHANNELS)
line, = ax.plot(x_data, y_data)

ax.set_ylim(-32768, 32767)  # 16-bit audio range
ax.set_xlim(0, BUFFER_SIZE // CHANNELS)
ax.set_title("Real-Time Audio Waveform")
ax.set_xlabel("Sample Index")
ax.set_ylabel("Amplitude")

def update(frame):
    global conn
    try:
        # Receive audio data from the C++ client
        data = conn.recv(BUFFER_SIZE)
        if not data:
            return line,
        
        # Convert byte data to int16
        samples = np.frombuffer(data, dtype=np.int16)

        # Update the plot data
        line.set_ydata(samples)
    except socket.error as e:
        print(f"Socket error: {e}")
        conn.close()
    return line,

# Set up the animation
ani = FuncAnimation(fig, update, blit=True, interval=50)
plt.show()

# Close the connection after visualization ends
conn.close()
sock.close()
