import asyncio
import websockets
import time

WS_URL = "ws://127.0.0.1:8081/ws"

async def ws_receiver():
    try:
        async with websockets.connect(WS_URL) as ws:
            print("Receiver connected.")
            while True:
                try:
                    msg = await asyncio.wait_for(ws.recv(), timeout=5.0)
                    print("Receiver got:", repr(msg))
                    if "StressTest" in msg:
                        print("SUCCESS! Received StressTest msg.")
                        break
                except asyncio.TimeoutError:
                    print("Receiver timeout waiting for messages")
                    break
    except Exception as e:
        print("Receiver Error:", e)

async def ws_sender():
    try:
        async with websockets.connect(WS_URL) as ws:
            print("Sender connected.")
            await asyncio.sleep(1) # wait a bit
            print("Sender sending msg...")
            await ws.send("StressTest|12345\n")
            await asyncio.sleep(2)
    except Exception as e:
        print("Sender Error:", e)

async def main():
    task1 = asyncio.create_task(ws_receiver())
    await asyncio.sleep(0.5)
    task2 = asyncio.create_task(ws_sender())
    await asyncio.gather(task1, task2)

if __name__ == "__main__":
    asyncio.run(main())
