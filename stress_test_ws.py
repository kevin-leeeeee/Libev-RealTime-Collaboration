import asyncio
import websockets
import time
import random

WS_URL = "ws://127.0.0.1:8081/ws"
NUM_CLIENTS = 50
MESSAGES_PER_CLIENT = 3

latencies = []
received_msgs = 0

async def ws_client(client_id, is_sender):
    global received_msgs
    try:
        async with websockets.connect(WS_URL) as ws:
            await asyncio.sleep(1)
            
            async def receive_messages():
                global received_msgs
                while True:
                    try:
                        msg = await asyncio.wait_for(ws.recv(), timeout=1.0)
                        if "StressTest" in msg:
                            received_msgs += 1
                            if "|" in msg:
                                ts_str = msg.split("|")[-1].strip().split('\x1b')[0]
                                try:
                                    sent_time = float(ts_str)
                                    latency = (time.time() - sent_time) * 1000
                                    latencies.append(latency)
                                except Exception as e:
                                    pass
                    except asyncio.TimeoutError:
                        continue
                    except Exception as e:
                        break

            recv_task = asyncio.create_task(receive_messages())

            if is_sender:
                for i in range(MESSAGES_PER_CLIENT):
                    await asyncio.sleep(random.uniform(0.1, 0.5))
                    payload = f"StressTest_{client_id}_{i}|{time.time()}\n"
                    await ws.send(payload)

            await asyncio.sleep(5)
            recv_task.cancel()
    except Exception as e:
        print(f"Client {client_id} Error: {e}")

async def main():
    print(f"Starting Stress Test with {NUM_CLIENTS} concurrent WebSocket clients...")
    
    start_total = time.time()
    tasks = []
    for i in range(NUM_CLIENTS):
        is_sender = (i < 10)
        tasks.append(asyncio.create_task(ws_client(i, is_sender)))
    
    await asyncio.gather(*tasks)
    end_total = time.time()

    print("\n" + "="*30)
    print("Stress Test Results")
    print("="*30)
    print(f"Total Clients:    {NUM_CLIENTS}")
    print(f"Duration:         {end_total - start_total:.2f} seconds")
    print(f"Total Raw Msgs:   {received_msgs}")
    
    if latencies:
        avg_latency = sum(latencies) / len(latencies)
        max_latency = max(latencies)
        print(f"Parsed Msgs:      {len(latencies)} / 1470 (Expected)")
        print(f"Avg Latency:      {avg_latency:.2f} ms")
        print(f"Max Latency:      {max_latency:.2f} ms")
    else:
        print("No broadcast messages received with timestamps.")
    print("="*30)

if __name__ == "__main__":
    asyncio.run(main())
