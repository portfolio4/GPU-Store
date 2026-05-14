import flip
import json
import funx.webserver.WebSocketManager


class FlipResponseHandler(object):
    def __init__(self,
            websocket_mgr: funx.webserver.WebSocketManager.WebSocketManager,
            ):
        self._websocket_mgr = websocket_mgr

    async def __call__(self, data):
        if isinstance(data, flip.Snapshot):
            response = {
                "msg_type": "Snapshot",
                "product": data.product,
                "bids": [
                    {"price": l.price, "qty": l.total_qty} for l in data.best_bids
                ],
                "asks": [
                    {"price": l.price, "qty": l.total_qty} for l in data.best_asks
                ]
            }
            await self._websocket_mgr.broadcast(json.dumps(response))

