import asyncio


class Session:
    def __init__(self, websocket, address: "int | None"):
        self.websocket = websocket
        self.address = address
        self.send_queue = asyncio.Queue()  # Queue for outgoing messages
