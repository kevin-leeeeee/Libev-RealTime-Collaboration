import asyncio
import websockets

async def test():
    try:
        async with websockets.connect("ws://127.0.0.1:8081/ws") as ws:
            print("Connected to proxy!")
            await ws.send("test\n")
            msg = await ws.recv()
            print("Recv:", msg)
    except Exception as e:
        print("Error:", e)

asyncio.run(test())
