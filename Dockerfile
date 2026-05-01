# 使用 Ubuntu 作為基礎鏡像，因為它對 C 語言編譯最友善
FROM ubuntu:22.04

# 設定非互動模式，避免安裝過程彈出選擇視窗
ENV DEBIAN_FRONTEND=noninteractive

# 安裝必要的套件：gcc, make, libev, python3, pip
RUN apt-get update && apt-get install -y \
    build-essential \
    libev-dev \
    python3 \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

# 安裝 Python WebSocket 依賴
RUN pip3 install websockets asyncio

# 設定工作目錄
WORKDIR /app

# 複製所有專案檔案
COPY . .

# 編譯 C 伺服器
RUN make

# 給予啟動腳本執行權限
RUN chmod +x start.sh

# Render 預設會使用 PORT 環境變數，我們的 WebSocket Proxy 跑在 8081
EXPOSE 8081

# 執行啟動腳本
CMD ["./start.sh"]
