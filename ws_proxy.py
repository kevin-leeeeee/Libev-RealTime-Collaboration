import asyncio
import websockets

# C 伺服器的設定
TCP_HOST = '127.0.0.1'
TCP_PORT = 8080

# WebSocket 伺服器設定
WS_HOST = '0.0.0.0'
WS_PORT = 8081

async def handle_client(websocket):
    print(f"New Web client connected from {websocket.remote_address}")
    
    try:
        # 連接到後端 C 伺服器
        reader, writer = await asyncio.open_connection(TCP_HOST, TCP_PORT)
    except ConnectionRefusedError:
        print("Failed to connect to C Server. Is it running?")
        await websocket.close(reason="Backend offline")
        return

    # 任務：從 C 伺服器讀取，轉發給 WebSocket
    async def tcp_to_ws():
        try:
            while True:
                data = await reader.readuntil(separator=b'\n')
                if not data:
                    break
                # 將資料解碼後送往網頁
                msg = data.decode('utf-8').strip()
                await websocket.send(msg)
        except asyncio.IncompleteReadError:
            pass
        except Exception as e:
            print(f"tcp_to_ws error: {e}")

    # 任務：從 WebSocket 讀取，轉發給 C 伺服器
    async def ws_to_tcp():
        try:
            async for message in websocket:
                # 確保訊息以換行結尾（因為 C 伺服器是以 \n 拆包）
                if not message.endswith('\n'):
                    message += '\n'
                writer.write(message.encode('utf-8'))
                await writer.drain()
        except websockets.exceptions.ConnectionClosed:
            pass
        except Exception as e:
            print(f"ws_to_tcp error: {e}")

    task1 = asyncio.create_task(tcp_to_ws())
    task2 = asyncio.create_task(ws_to_tcp())

    # 等待任一任務結束 (通常代表斷線)
    done, pending = await asyncio.wait(
        [task1, task2],
        return_when=asyncio.FIRST_COMPLETED
    )

    for task in pending:
        task.cancel()

    writer.close()
    await writer.wait_closed()
    print(f"Web client {websocket.remote_address} disconnected")

async def main():
    print(f"Starting WebSocket Proxy on ws://{WS_HOST}:{WS_PORT}")
    print(f"Proxying traffic to TCP Server at {TCP_HOST}:{TCP_PORT}")
    async with websockets.serve(handle_client, WS_HOST, WS_PORT):
        await asyncio.Future()  # 永遠執行

if __name__ == "__main__":
    asyncio.run(main())
