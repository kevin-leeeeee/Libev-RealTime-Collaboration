#!/bin/bash

# 1. 啟動 C 語言核心伺服器 (背景執行)
echo "Starting C Core Server..."
./server &

# 2. 等待一秒確保 C Server 已經起來
sleep 1

# 3. 啟動 Python WebSocket 代理 (前景執行，保持容器運作)
echo "Starting Python WebSocket Proxy..."
python3 ws_proxy.py
