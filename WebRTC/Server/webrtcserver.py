import asyncio
import websockets

# Store all connected peers
peers = set()

async def signaling_server(websocket, path):
    peers.add(websocket)
    try:
        async for message in websocket:
            # Broadcast the message to all other peers
            for peer in peers:
                if peer != websocket:
                    await peer.send(message)
    finally:
        peers.remove(websocket)

async def main():
    # Start the server
    async with websockets.serve(signaling_server, "localhost", 8765):
        print("WebRTC signaling server running on ws://localhost:8765")
        await asyncio.Future()  # Run forever

# Run the main coroutine
if __name__ == "__main__":
    asyncio.run(main())
