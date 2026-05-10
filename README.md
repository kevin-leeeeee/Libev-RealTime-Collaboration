# 高效能即時協作系統 (Real-Time Collaboration System)

本專案是一個基於 **Libev** 事件驅動模型開發的高效能多人即時協作系統。採用 C 語言處理底層網路通訊，並透過 Python 代理實作 WebSocket 支援，讓網頁端能輕鬆進行跨裝置協作。

## 系統架構
- **核心伺服器 (C Server)**: 基於 `libev` 的非阻塞 TCP 伺服器，負責數據分發、連線管理與狀態持久化。
- **WebSocket 代理 (Python Proxy)**: 橋接網頁端 (WebSocket) 與核心伺服器 (TCP) 的數據傳輸。
- **前端客戶端 (Web Client)**: 使用 HTML5/Javascript 實作的協作介面，支援 Base64 數據同步與即時聊天。

## 系統需求
- **作業系統**: Linux (Ubuntu/Debian 建議) 或 Windows WSL。
- **編譯器**: `gcc`。
- **函式庫**: `libev-dev`。
- **語言環境**: `Python 3.8+`。

## 快速開始

### 1. 建立虛擬環境與安裝依賴 (推薦)
為了避免系統套件衝突 (例如 `externally-managed-environment` 錯誤)，強烈建議使用虛擬環境：
```bash
# 安裝系統層級依賴
sudo apt update && sudo apt install build-essential libev-dev python3 python3-pip python3-venv -y

# 建立並啟動虛擬環境
python3 -m venv venv
source venv/bin/activate

# 在虛擬環境內安裝 Python 依賴
pip install aiohttp websockets
```

### 2. 編譯伺服器
```bash
make
```

## 服務啟動指南
本系統基於 **Layer 2 Raw Sockets** 開發，因此必須在 Linux/WSL 環境下並使用 `sudo` 權限執行。測試時請開啟兩個終端機視窗：

1. **啟動核心伺服器**:
   需指定網卡介面名稱（本機測試請用 `lo`）：
   ```bash
   sudo ./server lo
   ```

2. **啟動 WebSocket 代理**:
   請使用虛擬環境內的 Python 執行，並給予 sudo 權限：
   ```bash
   sudo ./venv/bin/python3 ws_proxy.py
   ```
   *(預設對外 WebSocket 服務連接埠: `8081`)*

3. **網頁連線測試**:
   - 直接用瀏覽器開啟專案資料夾內的 `client.html`。
   - 畫面上方若顯示「已連線」，即代表成功。

## 內建功能
- **多人即時編輯**: 一端修改，多端秒級同步（Base64 增量同步）。
- **遠端游標跟隨**: 仿 Google Docs 效果，即時顯示其他在線使用者的游標位置。
- **即時聊天系統**: 支援 `/nick` 修改暱稱、表情符號解析。
- **小遊戲與工具**:
  - `/roll`: 隨機擲骰子。
  - `/startgame`: 開始終極密碼遊戲。
  - `/guess <數字>`: 進行遊戲猜測。
  - `/list`: 查看目前在線名單。

## 遠端連線與分享 (Remote Sharing)

若要讓外網或區域網內的同學加入共編，請參考以下步驟：

### 1. 使用 Tailscale (推薦)
這是最安全且穩定的方式，適合小組協作：
- **主機端**: 確保已安裝 Tailscale 並取得你的 Tailscale IP (例如 `100.x.y.z`)。
- **客戶端 (他人)**: 
  1. 加入同一個 Tailscale 網路。
  2. 開啟 `client.html`。
  3. 修改 `client.html` 內的連線位址，將 `localhost` 改為你的 **Tailscale IP**。

### 2. 使用 Ngrok (快速測試)
適合臨時產出一個公開連結供人測試：
1. 啟動 `ws_proxy.py` (預設 8081)。
2. 執行：`ngrok http 8081`。
3. 將產生的 `https://xxxx.ngrok-free.app` 傳給對方。
4. 對方將該網址填入 `client.html` 的 WebSocket 連線處即可。

## 配置說明
若需更改埠號，請修改以下檔案：
- `server.c` 中的 `#define PORT`
- `ws_proxy.py` 中的 `TCP_PORT` 與 `WS_PORT`
- `client.html` 中的 WebSocket 連線位址
