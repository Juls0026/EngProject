import asyncio
import websockets

peers = set()

async def signaling_server(websocket, path):
    peers.add(websocket)
    try:
        async for message in websocket:
            # Forward messages to all peers except sender
            for peer in peers:
                if peer != websocket:
                    await peer.send(message)
    except websockets.exceptions.ConnectionClosed:
        print("Peer disconnected.")
    finally:
        peers.remove(websocket)

start_server = websockets.serve(signaling_server, "localhost", 8765)
print("Signaling server running on ws://localhost:8765")

asyncio.get_event_loop().run_until_complete(start_server)
asyncio.get_event_loop().run_forever()
