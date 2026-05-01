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
    
    log_msg(f"New Web client connected. Handshake complete.")
    
    try:
        # 連接到後端 C 伺服器
        log_msg(f"Attempting to connect to C server at {TCP_HOST}:{TCP_PORT}...")
        reader, writer = await asyncio.open_connection(TCP_HOST, TCP_PORT)
        log_msg("Successfully connected to C server.")
    except Exception as e:
        log_msg(f"CRITICAL: Failed to connect to C Server with error: {e}")
        await ws.close()
        return ws

    # 任務：從 C 伺服器讀取，轉發給 WebSocket
    async def tcp_to_ws():
        log_msg("tcp_to_ws task started")
        try:
            while True:
                data = await reader.readuntil(separator=b'\n')
                if not data:
                    log_msg("tcp_to_ws: Received EOF from C server")
                    break
                # log_msg(f"tcp_to_ws: received raw bytes: {data}")
                decoded = data.decode('utf-8', errors='replace').strip()
                await ws.send_str(decoded)
        except Exception as e:
            log_msg(f"tcp_to_ws error: {e}")
        log_msg("tcp_to_ws task exiting")

    # 任務：從 WebSocket 讀取，轉發給 C 伺服器
    async def ws_to_tcp():
        log_msg("ws_to_tcp task started")
        try:
            async for msg in ws:
                if msg.type == web.WSMsgType.TEXT:
                    text = msg.data
                    if not text.endswith('\n'):
                        text += '\n'
                    writer.write(text.encode('utf-8'))
                    await writer.drain()
                elif msg.type == web.WSMsgType.ERROR:
                    log_msg(f"WebSocket connection closed with exception {ws.exception()}")
        except Exception as e:
            log_msg(f"ws_to_tcp error: {e}")
        log_msg("ws_to_tcp task exiting")

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
    log_msg("Web client disconnected")
    return ws

import time

logs = []
def log_msg(msg):
    t = time.strftime('%H:%M:%S')
    logs.append(f"[{t}] {msg}")
    print(msg)
    if len(logs) > 100:
        logs.pop(0)

async def logs_handler(request):
    return web.Response(text="\n".join(logs))

async def health_check_handler(request):
    log_msg(f"[{request.method}] Normal HTTP request to {request.path} from {request.remote}")
    return web.Response(text="Render Health Check OK")

async def init_app():
    app = web.Application()
    app.add_routes([
        web.get('/', health_check_handler),
        web.get('/ws', websocket_handler),
        web.get('/logs', logs_handler)
    ])
    return app

if __name__ == '__main__':
    print(f"Starting WebSocket Proxy on 0.0.0.0:{WS_PORT}")
    print(f"Proxying traffic to TCP Server at {TCP_HOST}:{TCP_PORT}")
    web.run_app(init_app(), host='0.0.0.0', port=WS_PORT)
