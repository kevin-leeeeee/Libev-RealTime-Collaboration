import asyncio
import websockets
import time
import statistics

WS_URL = "ws://127.0.0.1:8081/ws"

async def ws_client(client_id, is_sender, results):
    try:
        async with websockets.connect(WS_URL, ping_interval=None) as ws:
            await asyncio.sleep(1)
            
            async def receive_messages():
                while True:
                    try:
                        msg = await asyncio.wait_for(ws.recv(), timeout=2.0)
                        if "CapacityTest" in msg and "|" in msg:
                            ts_str = msg.split("|")[-1].strip().split('\x1b')[0]
                            try:
                                sent_time = float(ts_str)
                                latency = (time.time() - sent_time) * 1000
                                results.append(latency)
                            except:
                                pass
                    except asyncio.TimeoutError:
                        continue
                    except:
                        break

            recv_task = asyncio.create_task(receive_messages())

            if is_sender:
                payload = f"CapacityTest_{client_id}|{time.time()}\n"
                await ws.send(payload)

            await asyncio.sleep(3)
            recv_task.cancel()
            return True
    except Exception as e:
        return False

async def run_batch(num_clients):
    results = []
    tasks = []
    
    num_senders = max(1, int(num_clients * 0.1))
    
    for i in range(num_clients):
        is_sender = (i < num_senders)
        tasks.append(asyncio.create_task(ws_client(i, is_sender, results)))
        if i % 20 == 0:
            await asyncio.sleep(0.01)
            
    completed = await asyncio.gather(*tasks, return_exceptions=True)
    successful_clients = sum(1 for c in completed if c is True)
    
    avg_lat = statistics.mean(results) if results else 0
    max_lat = max(results) if results else 0
    
    return successful_clients, len(results), avg_lat, max_lat

async def main():
    print("Capacity Test")
    print("-" * 50)
    
    test_scales = [50, 100, 200, 400]
    
    for scale in test_scales:
        print(f"Trying to establish {scale} connections...")
        success, msgs, avg, max_l = await run_batch(scale)
        print(f"  Success:    {success}/{scale}")
        print(f"  Broadcasts: {msgs}")
        print(f"  Avg Latency:{avg:.2f} ms")
        print(f"  Max Latency:{max_l:.2f} ms")
        print("-" * 50)
        await asyncio.sleep(2)

if __name__ == "__main__":
    asyncio.run(main())
