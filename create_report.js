const { Document, Packer, Paragraph, TextRun, HeadingLevel, AlignmentType, LevelFormat } = require('docx');
const fs = require('fs');

const doc = new Document({
    styles: {
        default: {
            document: {
                run: { font: "Arial", size: 24 }
            }
        },
        paragraphStyles: [
            {
                id: "Heading1",
                name: "Heading 1",
                basedOn: "Normal",
                next: "Normal",
                quickFormat: true,
                run: { size: 36, bold: true, color: "2E74B5", font: "Arial" },
                paragraph: { spacing: { before: 400, after: 200 }, outlineLevel: 0 }
            },
            {
                id: "Heading2",
                name: "Heading 2",
                basedOn: "Normal",
                next: "Normal",
                quickFormat: true,
                run: { size: 30, bold: true, color: "2E74B5", font: "Arial" },
                paragraph: { spacing: { before: 300, after: 150 }, outlineLevel: 1 }
            }
        ]
    },
    sections: [{
        children: [
            new Paragraph({
                alignment: AlignmentType.CENTER,
                spacing: { after: 800 },
                children: [
                    new TextRun({ text: "通訊網路實驗：期末結報", bold: true, size: 48 }),
                ]
            }),
            new Paragraph({
                alignment: AlignmentType.CENTER,
                spacing: { after: 200 },
                children: [
                    new TextRun({ text: "專題名稱：Libev-RealTime-Collaboration (無 IP 即時共編系統)", size: 28 }),
                ]
            }),
            new Paragraph({
                alignment: AlignmentType.CENTER,
                spacing: { after: 800 },
                children: [
                    new TextRun({ text: "學號：111501525    姓名：李科逸", size: 24 }),
                ]
            }),

            // 一、研究動機
            new Paragraph({ text: "一、研究動機", heading: HeadingLevel.HEADING_1 }),
            new Paragraph({
                children: [
                    new TextRun("傳統基於 TCP/IP (或 WebSockets over HTTP) 的協作系統在區域網路 (LAN) 中，經過了不必要的網路層與傳輸層封裝，產生了額外的標頭開銷 (Overhead)。"),
                ]
            }),
            new Paragraph({
                children: [
                    new TextRun("通訊網路實驗課程期末專題明確要求「不封裝 TCP/UDP/IP，聚焦於 LAN 內部的底層通訊 (Ethernet 或 Wi-Fi)」。本研究欲打造一個跳脫傳統 IP 尋址，完全基於網卡實體 MAC 地址路由的極低延遲即時共編與聊天系統。"),
                ]
            }),

            // 二、目標
            new Paragraph({ text: "二、目標", heading: HeadingLevel.HEADING_1 }),
            new Paragraph({
                children: [
                    new TextRun("1. 實作 Raw Socket 伺服器：以 C 語言與 libev 實作基於 AF_PACKET 的伺服器，直接處理 Layer 2 乙太網路幀。"),
                ]
            }),
            new Paragraph({
                children: [
                    new TextRun("2. 自定義通訊協定：捨棄 IP，自定義 EtherType (0x88B5) 與專屬的通訊協定標頭 (Message Type, Payload Length)。"),
                ]
            }),
            new Paragraph({
                children: [
                    new TextRun("3. 異構環境橋接：開發 Python 橋接代理 (Proxy)，將網頁端的高階 WebSocket 轉換為底層 Raw Ethernet 封包，突破瀏覽器沙盒限制。"),
                ]
            }),
            new Paragraph({
                children: [
                    new TextRun("4. 服務自動發現：實作基於 MAC 廣播 (FF:FF:FF:FF:FF:FF) 的 LAN Server 尋找機制。"),
                ]
            }),

            // 三、架構
            new Paragraph({ text: "三、架構", heading: HeadingLevel.HEADING_1 }),
            new Paragraph({
                children: [
                    new TextRun("系統由三個核心組件構成：Web Client (UI)、Local Proxy (Python) 與 C Server。"),
                ]
            }),
            new Paragraph({
                children: [
                    new TextRun("資料流向：Web Client 透過標準 WebSocket 與本機 Proxy 通訊，Proxy 將訊息封裝為 Layer 2 Raw Frame 並注入網卡；C Server 監聽網卡介面，根據來源 MAC 地址識別使用者並進行廣播同步。"),
                ]
            }),

            // 四、實現方法
            new Paragraph({ text: "四、實現方法", heading: HeadingLevel.HEADING_1 }),
            new Paragraph({ text: "1. 自定義通訊協定 (Protocol Design)", heading: HeadingLevel.HEADING_2 }),
            new Paragraph({
                children: [
                    new TextRun("Ethernet Header: [Dest MAC (6B)] + [Src MAC (6B)] + [EtherType 0x88B5 (2B)]"),
                ]
            }),
            new Paragraph({
                children: [
                    new TextRun("Custom Header: [Msg Type (1B): JOIN/DATA/LEAVE] + [Payload Length (2B)]"),
                ]
            }),
            new Paragraph({ text: "2. C Server 實作細節", heading: HeadingLevel.HEADING_2 }),
            new Paragraph({
                children: [
                    new TextRun("取代傳統 bind/listen/accept，改為將 Socket 綁定至指定網卡。建立基於 MAC 地址的 Linked List 來追蹤連線客戶端狀態。"),
                ]
            }),
            new Paragraph({ text: "3. Python 代理程式", heading: HeadingLevel.HEADING_2 }),
            new Paragraph({
                children: [
                    new TextRun("使用 asyncio 處理非同步 I/O，確保 WebSocket 與 Raw Socket 轉換不互相阻塞。"),
                ]
            }),

            // 五、結果
            new Paragraph({ text: "五、結果", heading: HeadingLevel.HEADING_1 }),
            new Paragraph({
                children: [
                    new TextRun("成功實現區域網路內的多節點即時聊天與共編。透過 Wireshark 側錄，證實封包中僅有 Ethernet II 標頭與自定義 Payload，完全無 IPv4/TCP 標頭，完美達成專題要求。"),
                ]
            }),

            // 六、未來展望
            new Paragraph({ text: "六、未來展望", heading: HeadingLevel.HEADING_1 }),
            new Paragraph({
                children: [
                    new TextRun("1. 解決 MTU 限制：實作自訂的封包分片與重組機制。"),
                ]
            }),
            new Paragraph({
                children: [
                    new TextRun("2. 跨網域通訊：探討 Layer 2 Tunneling 技術之應用。"),
                ]
            }),
            new Paragraph({
                children: [
                    new TextRun("3. 資料加密：加入對稱式加密防止 LAN 側錄。"),
                ]
            }),

            // 七、AI 使用
            new Paragraph({ text: "七、AI 使用", heading: HeadingLevel.HEADING_1 }),
            new Paragraph({
                children: [
                    new TextRun("開發過程中，輔以 AI 工具進行 C 語言 Raw Socket 的底層 API 查詢、Python 非同步框架除錯及前端 UI 樣式生成。惟核心之「無 IP 架構設計」與「協定定義」由作者本人構思與驗證。"),
                ]
            }),

            // 八、參考資料
            new Paragraph({ text: "八、參考資料", heading: HeadingLevel.HEADING_1 }),
            new Paragraph({
                children: [
                    new TextRun("[1] Linux Programmer's Manual: packet(7)"),
                ]
            }),
            new Paragraph({
                children: [
                    new TextRun("[2] IEEE 802.3 Ethernet Standard (Experimental EtherType: 0x88B5)"),
                ]
            }),
        ]
    }]
});

Packer.toBuffer(doc).then((buffer) => {
    fs.writeFileSync("final_report.docx", buffer);
    console.log("Document created successfully: final_report.docx");
});
