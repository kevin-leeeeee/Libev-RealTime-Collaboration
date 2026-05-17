import socket
import struct
import sys

INTERFACE = 'lo'
CUSTOM_ETH_TYPE = 0x88B5
MSG_JOIN = 0

def pack_eth_frame(dest_mac, src_mac, msg_type, payload):
    eth_header = struct.pack("!6s6sH", dest_mac, src_mac, CUSTOM_ETH_TYPE)
    payload_bytes = payload.encode('utf-8') if isinstance(payload, str) else payload
    custom_header = struct.pack("!BH", msg_type, len(payload_bytes))
    return eth_header + custom_header + payload_bytes

try:
    # 注意：AF_PACKET 是 Linux 專有的 Raw Socket 類型，Windows 並不支援
    raw_socket = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(CUSTOM_ETH_TYPE))
    raw_socket.bind((INTERFACE, 0))
except AttributeError:
    print("錯誤：Windows 不支援 AF_PACKET。請在 WSL (Linux) 環境下執行此腳本！")
    sys.exit(1)
except PermissionError:
    print("錯誤：Raw socket 需要 root 權限，請使用 sudo 執行！")
    sys.exit(1)

# 注入三個幽靈使用者，使用特殊的 MAC 位址
ghosts = [
    (b'\x02\x00\x00\x00\xaa\xaa', "Ghost_A"),
    (b'\x02\x00\x00\x00\xbb\xbb', "Ghost_B"),
    (b'\x02\x00\x00\x00\xcc\xcc', "Ghost_C")
]

dest_mac = b'\x00\x00\x00\x00\x00\x00'

print("開始注入 Layer 2 幽靈使用者...")
for src_mac, nickname in ghosts:
    frame = pack_eth_frame(dest_mac, src_mac, MSG_JOIN, nickname)
    raw_socket.send(frame)
    print(f"成功注入幽靈使用者: {nickname} (MAC: {src_mac.hex(':')})")

raw_socket.close()
print("注入完成！請在 15 秒內執行 check_users.py 查看，隨後心跳機制將會自動清除他們。")
