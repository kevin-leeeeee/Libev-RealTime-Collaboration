import asyncio
import time
import random

HOST = '127.0.0.1'
PORT = 8080
NUM_CLIENTS = 100
MESSAGES_PER_CLIENT = 10

latencies = []

async def tcp_client(client_id, is_sender):
    try:
        reader, writer = await asyncio.open_connection(HOST, PORT)
    except ConnectionRefusedError:
        print(f"Client {client_id} failed to connect.")
        return

    print(f"Client {client_id} connected.")

    async def receive_messages():
        while True:
            try:
                data = await reader.readuntil(separator=b'\n')
                if not data:
                    break
                
                # 解析時間戳記來計算延遲
                decoded = data.decode('utf-8').strip()
                # 格式: [Client fd]: MSG_ID|TIMESTAMP
                if "|" in decoded:
                    parts = decoded.split("|")
                    if len(parts) == 2:
                        try:
                            sent_time = float(parts[1])
                            latency = (time.time() - sent_time) * 1000 # 轉換為毫秒
                            latencies.append(latency)
                        except ValueError:
                            pass
            except asyncio.IncompleteReadError:
                break
            except Exception as e:
                print(f"Receive error: {e}")
                break

    recv_task = asyncio.create_task(receive_messages())

    if is_sender:
        for i in range(MESSAGES_PER_CLIENT):
            await asyncio.sleep(random.uniform(0.1, 0.5))
            msg = f"HELLO_{client_id}_{i}|{time.time()}\n"
            writer.write(msg.encode('utf-8'))
            await writer.drain()

    # 讓接收端多等一下，確保收到所有廣播
    await asyncio.sleep(MESSAGES_PER_CLIENT * 0.6 + 2)
    
    recv_task.cancel()
    writer.close()
    await writer.wait_closed()

async def main():
    print(f"Starting stress test with {NUM_CLIENTS} clients...")
    
    # 建立多個客戶端，一半當發送者，一半單純接收
    tasks = []
    for i in range(NUM_CLIENTS):
        is_sender = (i % 2 == 0) # 偶數 ID 發送訊息
        tasks.append(asyncio.create_task(tcp_client(i, is_sender)))
    
    await asyncio.gather(*tasks)

    if latencies:
        avg_latency = sum(latencies) / len(latencies)
        max_latency = max(latencies)
        print("\n--- 壓力測試結果 ---")
        print(f"總共接收廣播訊息數: {len(latencies)}")
        print(f"平均延遲 (Latency): {avg_latency:.2f} ms")
        print(f"最大延遲 (Max Latency): {max_latency:.2f} ms")
    else:
        print("\n未收到任何完整的廣播訊息。")

if __name__ == "__main__":
    asyncio.run(main())
