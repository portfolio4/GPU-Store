import asyncio
import flop
import funx.webserver.FlopSession
import random
import pydantic

class OrderRequest(pydantic.BaseModel):
    product: int
    side: str
    price: int
    qty: int
    duration: str
    client_order_id: int

class CancelRequest(pydantic.BaseModel):
    client_order_id: int

class FlopGateway(object):
    def __init__(self, gw_json_cfg, sessions):
        self._sessions = []
        for sess_json_cfg in gw_json_cfg["sessions"]:
            if sess_json_cfg["name"] in sessions:
                self._sessions.append(funx.webserver.FlopSession.FlopSession(gw_json_cfg, sess_json_cfg))
        if len(self._sessions) == 0:
            raise ValueError("No sessions in gateway")

        self._clordid_to_sess = {}

    def SendOrder(self, order: OrderRequest, company_id: int):
        valid_sessions = [s for s in self._sessions if s.company_id == company_id]
        if not valid_sessions:
            raise ValueError(f"No sessions available for company_id={company_id}")
        selected_session = random.choice(valid_sessions)

        for name, enum_value in flop.Side.__members__.items():
            if order.side == name:
                flop_side = enum_value
                break
        else:
            raise ValueError(f"Invalid order side: {order.side}")

        for name, enum_value in flop.Duration.__members__.items():
            if order.duration == name:
                flop_duration = enum_value
                break
        else:
            raise ValueError(f"Invalid order duration: {order.duration}")

        flop_order = flop.NewOrder()
        flop_order.product = order.product
        flop_order.side = flop_side
        flop_order.qty = order.qty
        flop_order.price = order.price
        flop_order.duration = flop_duration
        flop_order.client_order_id = order.client_order_id
        selected_session.SendOrder(flop_order)

        self._clordid_to_sess[flop_order.client_order_id] = selected_session

    def CancelOrder(self, cancel: CancelRequest):
        flop_cancel = flop.CancelOrder()
        flop_cancel.client_order_id = cancel.client_order_id
        if cancel.client_order_id not in self._clordid_to_sess:
            raise ValueError(f"Unknown client order ID: {cancel.client_order_id}")

        selected_session = self._clordid_to_sess[cancel.client_order_id]
        selected_session.SendCancel(flop_cancel)

    def RemoveOrderTracking(self, client_order_id: int):
        if client_order_id in self._clordid_to_sess:
            del self._clordid_to_sess[client_order_id]

    async def SetUpSessionPolling(self, handler):
        tasks = []
        for s in self._sessions:
            await s.InitAsync()

            tasks.append(asyncio.create_task(s.PollTask(handler)))
            tasks.append(asyncio.create_task(s.HeartbeatTask()))
        return tasks

