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
- **檔案持久化與管理**: 支援建立多個共編文件，伺服器會自動儲存至硬碟並維護索引 (`files_index.txt`)，確保伺服器重啟資料不遺失，並提供圖形化的檔案刪除功能。
- **手機掃碼秒連 (Mobile Sharing)**: Proxy 啟動時能穿透 WSL，精準自動偵測 Windows 本機的 Wi-Fi IP，並在終端機直接印出 QR Code。
- **網頁自動託管**: 現在不需手動開啟本地端的 `client.html`，只要在瀏覽器輸入 `http://localhost:8081` 或手機掃碼即可自動載入系統介面。
- **多人即時編輯**: 一端修改，多端秒級同步（Base64 增量同步）。
- **遠端游標跟隨**: 仿 Google Docs 效果，即時顯示其他在線使用者的游標位置。
- **即時聊天系統**: 支援 `/nick` 修改暱稱、表情符號解析。
- **小遊戲與工具**:
  - `/roll`: 隨機擲骰子。
  - `/startgame`: 開始終極密碼遊戲。
  - `/guess <數字>`: 進行遊戲猜測。
  - `/list`: 查看目前在線名單。

## 遠端連線與分享 (Remote Sharing)

由於系統內建了強大的**網路偵測與 QR Code 分享功能**，現在分享給同學變得非常簡單：

### 1. 同一 Wi-Fi 下快速分享 (最推薦)
1. 確保你的電腦與手機（或其他設備）連上**同一個 Wi-Fi 網路**。
2. 啟動 `ws_proxy.py`，終端機會自動印出目前的 **Network URL** 與 **QR Code**。
3. 直接用手機掃描該 QR Code，即可瞬間進入共編畫面！
4. 進入網頁後，也可以點擊網頁右上角的**「分享」**按鈕，產生可複製的網址與網頁版 QR Code 傳給遠端同學。

*(WSL2 使用者注意：由於 WSL 的網路隔離特性，您必須先在 Windows 以管理員權限執行 Port Forwarding 指令，外部設備才能成功連入。若偵測失敗，可在啟動前加上環境變數 `SHARE_IP=你的IP` 強制指定)*

### 2. 使用 Tailscale (跨網域連線)
這是最安全且穩定的方式，適合不同網路環境的小組協作：
- **主機端**: 確保已安裝 Tailscale 並取得你的 Tailscale IP (例如 `100.x.y.z`)。
- **啟動方式**: 透過 `sudo SHARE_IP=100.x.y.z ./venv/bin/python3 ws_proxy.py` 啟動，這樣 QR Code 就會使用 Tailscale IP 生成。
- **客戶端 (他人)**: 加入同一個 Tailscale 網路後，直接掃碼或輸入該網址即可連線。

## 配置說明
若需更改埠號或底層通訊協定，請修改以下檔案：
- `server.c` 中的 `#define CUSTOM_ETH_TYPE` (預設為 `0x88B5`)
- `ws_proxy.py` 中的 `WS_PORT` (預設為 `8081`)
