import asyncio
import fastapi

class WebSocketManager(object):
    def __init__(self):
        self._active_connections: list[fastapi.WebSocket] = []
        self._broadcast_lock = asyncio.Lock()

    async def connect(self, websocket: fastapi.WebSocket):
        await websocket.accept()
        self._active_connections.append(websocket)

    def disconnect(self, websocket: fastapi.WebSocket):
        if websocket in self._active_connections:
            self._active_connections.remove(websocket)

    async def broadcast(self, msg: str):
        async with self._broadcast_lock:
            failed_connections = []
            for connection in list(self._active_connections):
                try:
                    await connection.send_text(msg)
                except Exception:
                    failed_connections.append(connection)

            for connection in failed_connections:
                self.disconnect(connection)

