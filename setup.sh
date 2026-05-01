#!/bin/bash

# 高效能即時協作系統 - 自動部署腳本
# 適用於 Debian/Ubuntu 環境

set -e

echo "=========================================="
echo "   正在開始部署即時協作系統服務端..."
echo "=========================================="

# 1. 檢查並安裝系統依賴
echo "[1/4] 檢查系統環境與安裝函式庫 (需要 sudo 權限)..."
sudo apt update
sudo apt install -y build-essential libev-dev python3 python3-pip

# 2. 安裝 Python 依賴
echo "[2/4] 安裝 Python WebSocket 代理依賴..."
pip3 install websockets --break-system-packages || pip3 install websockets

# 3. 編譯 C 伺服器
echo "[3/4] 正在編譯 C 核心伺服器..."
if [ -f "Makefile" ]; then
    make clean
    make
else
    echo "錯誤: 找不到 Makefile，請確保你在正確的專案目錄下。"
    exit 1
fi

# 4. 完成部署
echo "=========================================="
echo "   部署成功！"
echo "=========================================="
echo ""
echo "啟動服務指南："
echo "1. 執行核心伺服器: ./server"
echo "2. 執行代理伺服器: python3 ws_proxy.py"
echo ""
echo "完成後，請在瀏覽器開啟 client.html 即可開始協作。"
echo "=========================================="
