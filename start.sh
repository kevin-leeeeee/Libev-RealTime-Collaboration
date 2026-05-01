#!/bin/bash

# 1. 啟動 C 語言核心伺服器 (背景執行並自動重啟)
echo "Starting C Core Server with auto-restart..."
(
  while true; do
      ./server
      echo "CRITICAL ERROR: C Server crashed! Restarting in 1 second..."
      sleep 1
  done
) &

# 2. 啟動 Python WebSocket 代理 (前景執行，保持容器運作)
echo "Starting Python WebSocket Proxy..."
python3 -u ws_proxy.py
