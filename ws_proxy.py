import asyncio
import os
import socket
import struct
import random
from aiohttp import web

# Network Configuration
INTERFACE = os.environ.get('IFACE', 'lo')
CUSTOM_ETH_TYPE = 0x88B5
MSG_JOIN = 0
MSG_DATA = 1
MSG_LEAVE = 2

WS_PORT = int(os.environ.get('PORT', 8081))
BROADCAST_MAC = b'\xff\xff\xff\xff\xff\xff'

raw_socket = None
server_mac = None

# Mapping virtual MAC to WebSocket
connected_clients = {}

def pack_eth_frame(dest_mac, src_mac, msg_type, payload):
    eth_header = struct.pack("!6s6sH", dest_mac, src_mac, CUSTOM_ETH_TYPE)
    payload_bytes = payload.encode('utf-8') if isinstance(payload, str) else payload
    custom_header = struct.pack("!BH", msg_type, len(payload_bytes))
    return eth_header + custom_header + payload_bytes

async def central_raw_recv():
    global server_mac
    log_msg("Central raw_to_ws task started")
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
            
            # If from a connected client, ignore (don't loop back)
            if src_mac in connected_clients:
                continue
            
            if server_mac is None:
                server_mac = src_mac
                log_msg(f"Learned Server MAC: {server_mac.hex(':')}")

            msg_type, payload_len = struct.unpack("!BH", data[14:17])
            if len(data) < 17 + payload_len:
                continue
                
            payload = data[17:17+payload_len]
            
            if msg_type == MSG_DATA:
                decoded = payload.decode('utf-8', errors='replace').strip()
                # Dispatch to clients
                if dest_mac == BROADCAST_MAC:
                    for ws in list(connected_clients.values()):
                        try:
                            await ws.send_str(decoded)
                        except:
                            pass
                elif dest_mac in connected_clients:
                    try:
                        await connected_clients[dest_mac].send_str(decoded)
                    except:
                        pass

    except Exception as e:
        log_msg(f"central_raw_recv error: {e}")
    log_msg("Central raw_to_ws task exiting")

async def websocket_handler(request):
    global server_mac
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    
    # Generate a virtual MAC for this client
    virtual_mac = bytes([0x02, 0x00, 0x00, 0x00, random.randint(0, 255), random.randint(0, 255)])
    connected_clients[virtual_mac] = ws
    log_msg(f"New Web client connected. Assigned Virtual MAC: {virtual_mac.hex(':')}")
    
    try:
        # Send JOIN message via broadcast to find the server
        join_frame = pack_eth_frame(BROADCAST_MAC, virtual_mac, MSG_JOIN, b"")
        raw_socket.send(join_frame)
        log_msg(f"Sent JOIN broadcast for {virtual_mac.hex(':')}.")
    except Exception as e:
        log_msg(f"Failed to send JOIN frame: {e}")
        del connected_clients[virtual_mac]
        await ws.close()
        return ws

    async def ws_to_raw():
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
                    frame = pack_eth_frame(server_mac, virtual_mac, MSG_DATA, text)
                    await loop.sock_sendall(raw_socket, frame)
                elif msg.type == web.WSMsgType.ERROR:
                    log_msg(f"WebSocket closed with exception {ws.exception()}")
        except Exception as e:
            log_msg(f"ws_to_raw error: {e}")
            
        # Send LEAVE message
        try:
            if server_mac:
                leave_frame = pack_eth_frame(server_mac, virtual_mac, MSG_LEAVE, b"")
                await loop.sock_sendall(raw_socket, leave_frame)
        except:
            pass

    await ws_to_raw()
    
    if virtual_mac in connected_clients:
        del connected_clients[virtual_mac]
    log_msg(f"Web client {virtual_mac.hex(':')} disconnected")
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
    # Start the central raw packet receiver
    asyncio.create_task(central_raw_recv())
    return app

if __name__ == '__main__':
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
    print(f"Waiting for Web Browser to connect at ws://localhost:{WS_PORT}/ws")
    
    web.run_app(init_app(), host='0.0.0.0', port=WS_PORT)
