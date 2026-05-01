import asyncio
import os
from aiohttp import web

# C 伺服器的設定
TCP_HOST = '127.0.0.1'
TCP_PORT = 8080

# WebSocket 伺服器設定
WS_PORT = int(os.environ.get('PORT', 8081))

async def websocket_handler(request):
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    
    print(f"New Web client connected")
    
    try:
        # 連接到後端 C 伺服器
        reader, writer = await asyncio.open_connection(TCP_HOST, TCP_PORT)
    except ConnectionRefusedError:
        print("Failed to connect to C Server. Is it running?")
        await ws.close()
        return ws

    # 任務：從 C 伺服器讀取，轉發給 WebSocket
    async def tcp_to_ws():
        try:
            while True:
                data = await reader.readuntil(separator=b'\n')
                if not data:
                    break
                await ws.send_str(data.decode('utf-8').strip())
        except Exception as e:
            print(f"tcp_to_ws error: {e}")

    # 任務：從 WebSocket 讀取，轉發給 C 伺服器
    async def ws_to_tcp():
        try:
            async for msg in ws:
                if msg.type == web.WSMsgType.TEXT:
                    text = msg.data
                    if not text.endswith('\n'):
                        text += '\n'
                    writer.write(text.encode('utf-8'))
                    await writer.drain()
                elif msg.type == web.WSMsgType.ERROR:
                    print(f"WebSocket connection closed with exception {ws.exception()}")
        except Exception as e:
            print(f"ws_to_tcp error: {e}")

    task1 = asyncio.create_task(tcp_to_ws())
    task2 = asyncio.create_task(ws_to_tcp())

    # 等待任一任務結束
    done, pending = await asyncio.wait(
        [task1, task2],
        return_when=asyncio.FIRST_COMPLETED
    )

    for task in pending:
        task.cancel()

    writer.close()
    await writer.wait_closed()
    print("Web client disconnected")
    return ws

async def root_handler(request):
    # 如果帶有 WebSocket Upgrade 標頭，就交給 websocket_handler 處理
    if request.headers.get('Upgrade', '').lower() == 'websocket':
        return await websocket_handler(request)
    # 否則就回傳 200 OK，作為 Render Health Check 的回應
    return web.Response(text="Render Health Check OK")

async def init_app():
    app = web.Application()
    # web.get 已經自動包含了 HEAD 請求的處理，用來應付 Render 的健康度檢查
    app.add_routes([
        web.get('/', root_handler)
    ])
    return app

if __name__ == '__main__':
    print(f"Starting WebSocket Proxy on 0.0.0.0:{WS_PORT}")
    print(f"Proxying traffic to TCP Server at {TCP_HOST}:{TCP_PORT}")
    web.run_app(init_app(), host='0.0.0.0', port=WS_PORT)
