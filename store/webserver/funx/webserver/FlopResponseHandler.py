import flop
import json
import funx.webserver.WebSocketManager
import funx.webserver.FlopGateway


class FlopResponseHandler(object):
    def __init__(self,
            websocket_mgr: funx.webserver.WebSocketManager.WebSocketManager,
            flop_gateway: funx.webserver.FlopGateway.FlopGateway
            ):
        self._websocket_mgr = websocket_mgr
        self._flop_gateway = flop_gateway

    async def __call__(self, data):
        response = {}
        if isinstance(data, flop.OrderAccepted):
            msg_type_str = "OrderAccepted"
        elif isinstance(data, flop.OrderRemoved):
            msg_type_str = "OrderRemoved"
            self._flop_gateway.RemoveOrderTracking(data.client_order_id)
        elif isinstance(data, flop.Trade):
            msg_type_str = "Trade"
        elif isinstance(data, flop.OrderReject):
            msg_type_str = "OrderReject"
            self._flop_gateway.RemoveOrderTracking(data.client_order_id)
        elif isinstance(data, flop.CancelReject):
            msg_type_str = "CancelReject"
            self._flop_gateway.RemoveOrderTracking(data.client_order_id)
        else:
            return

        response["msg_type"] = msg_type_str

        for attr in dir(data):
            if not attr.startswith("_"):
                value = getattr(data, attr)
                if callable(value):
                    continue
                flop_enum_types = [flop.Duration, flop.Side, flop.FeeCode, flop.Reason]
                if any(isinstance(value, T) for T in flop_enum_types):
                    value = value.name
                response[attr] = value
        await self._websocket_mgr.broadcast(json.dumps(response))

