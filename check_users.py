import asyncio
import websockets

async def get_list():
    try:
        async with websockets.connect("ws://127.0.0.1:8081/ws") as ws:
            await ws.send("/list\n")
            # Wait for the [LIST] message
            for _ in range(20):
                msg = await ws.recv()
                if "[LIST]" in msg:
                    users = msg.replace("[LIST]", "").strip().split(", ")
                    # Filter out empty or whitespace-only names
                    users = [u for u in users if u.strip()]
                    print(f"目前線上人數: {len(users)} 人")
                    print(f"使用者清單: {', '.join(users)}")
                    return
    except Exception as e:
        print("無法連線到伺服器:", e)

asyncio.run(get_list())
