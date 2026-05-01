#!/bin/bash

# 1. 啟動 C 語言核心伺服器 (背景執行)
echo "Starting C Core Server..."
./server &

# 2. 等待一秒確保 C Server 已經起來
sleep 1
if ! pgrep -x "server" > /dev/null; then
    echo "CRITICAL ERROR: C Server (./server) is NOT running! It must have crashed."
    # 我們不退出，讓 Python Proxy 繼續跑，這樣才能從網頁和 Render 看到日誌
fi

# 3. 啟動 Python WebSocket 代理 (前景執行，保持容器運作)
echo "Starting Python WebSocket Proxy..."
python3 -u ws_proxy.py
