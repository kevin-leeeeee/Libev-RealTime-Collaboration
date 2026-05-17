import asyncio
import websockets
import time

async def ping():
    try:
        async with websockets.connect("ws://127.0.0.1:8081/ws") as ws:
            await ws.send(f"PING|{time.time()}\n")
            for i in range(10):
                try:
                    msg = await asyncio.wait_for(ws.recv(), timeout=1.0)
                    print(f"Recv{i}:", repr(msg))
                except asyncio.TimeoutError:
                    print("Timeout waiting for more msgs")
                    break
    except Exception as e:
        print("Error:", e)

asyncio.run(ping())
