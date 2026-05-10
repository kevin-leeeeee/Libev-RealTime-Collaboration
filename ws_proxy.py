import asyncio
import os
import socket
import struct
from aiohttp import web

# Network Configuration
INTERFACE = os.environ.get('IFACE', 'lo') # 預設使用 loopback，可透過環境變數 IFACE 更改為 eth0
CUSTOM_ETH_TYPE = 0x88B5
MSG_JOIN = 0
MSG_DATA = 1
MSG_LEAVE = 2

WS_PORT = int(os.environ.get('PORT', 8081))
BROADCAST_MAC = b'\xff\xff\xff\xff\xff\xff'

raw_socket = None
server_mac = None # Will learn when server broadcasts or replies
my_mac = None

def get_mac_address(ifname):
    import fcntl
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    info = fcntl.ioctl(s.fileno(), 0x8927, struct.pack('256s', bytes(ifname, 'utf-8')[:15]))
    return info[18:24]

def pack_eth_frame(dest_mac, msg_type, payload):
    # Ethernet Header: 6 bytes Dest, 6 bytes Src, 2 bytes EtherType
    eth_header = struct.pack("!6s6sH", dest_mac, my_mac, CUSTOM_ETH_TYPE)
    
    # Custom Header: 1 byte Type, 2 bytes Length
    payload_bytes = payload.encode('utf-8') if isinstance(payload, str) else payload
    custom_header = struct.pack("!BH", msg_type, len(payload_bytes))
    
    return eth_header + custom_header + payload_bytes

async def websocket_handler(request):
    global server_mac
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    
    log_msg(f"New Web client connected.")
    
    try:
        # Send JOIN message via broadcast to find the server
        join_frame = pack_eth_frame(BROADCAST_MAC, MSG_JOIN, b"")
        raw_socket.send(join_frame)
        log_msg("Sent JOIN broadcast to LAN.")
    except Exception as e:
        log_msg(f"Failed to send JOIN frame: {e}")
        await ws.close()
        return ws

    async def raw_to_ws():
        global server_mac
        log_msg("raw_to_ws task started")
        loop = asyncio.get_event_loop()
        try:
            while True:
                data = await loop.sock_recv(raw_socket, 65536)
                if len(data) < 17:
                    continue
                
                dest_mac = data[0:6]
                src_mac = data[6:12]
                eth_type = struct.unpack("!H", data[12:14])[0]
                
                if eth_type != CUSTOM_ETH_TYPE:
                    continue
                
                # Ignore our own packets
                if src_mac == my_mac:
                    continue
                
                # If we don't know the server MAC yet, assume the first one speaking our protocol is the server
                if server_mac is None:
                    server_mac = src_mac
                    log_msg(f"Learned Server MAC: {server_mac.hex(':')}")

                # Accept broadcast or packets destined to us
                if dest_mac != my_mac and dest_mac != BROADCAST_MAC:
                    continue

                msg_type, payload_len = struct.unpack("!BH", data[14:17])
                if len(data) < 17 + payload_len:
                    continue
                    
                payload = data[17:17+payload_len]
                
                if msg_type == MSG_DATA:
                    decoded = payload.decode('utf-8', errors='replace').strip()
                    await ws.send_str(decoded)
        except Exception as e:
            log_msg(f"raw_to_ws error: {e}")
        log_msg("raw_to_ws task exiting")

    async def ws_to_raw():
        log_msg("ws_to_raw task started")
        loop = asyncio.get_event_loop()
        try:
            async for msg in ws:
                if msg.type == web.WSMsgType.TEXT:
                    if server_mac is None:
                        log_msg("Cannot send: Server MAC not known yet.")
                        continue
                    
                    text = msg.data
                    if not text.endswith('\n'):
                        text += '\n'
                    frame = pack_eth_frame(server_mac, MSG_DATA, text)
                    await loop.sock_sendall(raw_socket, frame)
                elif msg.type == web.WSMsgType.ERROR:
                    log_msg(f"WebSocket closed with exception {ws.exception()}")
        except Exception as e:
            log_msg(f"ws_to_raw error: {e}")
            
        # Send LEAVE message when websocket disconnects
        try:
            if server_mac:
                leave_frame = pack_eth_frame(server_mac, MSG_LEAVE, b"")
                await loop.sock_sendall(raw_socket, leave_frame)
        except:
            pass
        log_msg("ws_to_raw task exiting")

    task1 = asyncio.create_task(raw_to_ws())
    task2 = asyncio.create_task(ws_to_raw())

    done, pending = await asyncio.wait(
        [task1, task2],
        return_when=asyncio.FIRST_COMPLETED
    )

    for task in pending:
        task.cancel()

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

async def init_app():
    app = web.Application()
    app.add_routes([
        web.get('/ws', websocket_handler),
        web.get('/logs', logs_handler)
    ])
    return app

if __name__ == '__main__':
    try:
        my_mac = get_mac_address(INTERFACE)
    except Exception as e:
        print(f"Error getting MAC for interface {INTERFACE}: {e}")
        exit(1)
        
    try:
        raw_socket = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(CUSTOM_ETH_TYPE))
        raw_socket.bind((INTERFACE, 0))
        raw_socket.setblocking(False)
    except PermissionError:
        print("CRITICAL: Raw sockets require root privileges. Please run with sudo.")
        exit(1)
    except Exception as e:
        print(f"Failed to create raw socket: {e}")
        exit(1)

    print(f"Starting Local WebSocket Proxy on 0.0.0.0:{WS_PORT}")
    print(f"Using Raw Sockets (Ethernet Layer 2) on interface: {INTERFACE}")
    print(f"My MAC Address: {my_mac.hex(':')}")
    print(f"Waiting for Web Browser to connect at ws://localhost:{WS_PORT}/ws")
    
    web.run_app(init_app(), host='0.0.0.0', port=WS_PORT)
