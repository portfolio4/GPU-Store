import asyncio
import flop

class FlopSession(object):
    def __init__(self, gw_json_cfg, sess_json_cfg):
        self._server_ip = gw_json_cfg["server_ip4"]
        self._server_port = sess_json_cfg["port"]
        self._sess_name = sess_json_cfg["name"]
        self._password = sess_json_cfg["password"]
        self._company_id = sess_json_cfg["company_id"]
        self._hb_interval_seconds = sess_json_cfg["heartbeat_timeout_seconds"] // 3

    async def InitAsync(self):
        self._reader, self._writer = await asyncio.open_connection(self._server_ip, self._server_port)

        self.SendLogin()

    @property
    def company_id(self):
        return self._company_id

    def SendLogin(self):
        login = flop.MessageLogin()
        login.body.session = self._sess_name
        login.body.password = self._password
        self._writer.write(bytes(login))

    def SendOrder(self, order: flop.NewOrder):
        msg = flop.MessageNewOrder()
        msg.body = order
        self._writer.write(bytes(msg))

    def SendCancel(self, cancel: flop.CancelOrder):
        msg = flop.MessageCancelOrder()
        msg.body = cancel
        self._writer.write(bytes(msg))

    def SendHeartbeat(self):
        msg = flop.MessageHeartbeat()
        self._writer.write(bytes(msg))

    async def HeartbeatTask(self):
        while True:
            self.SendHeartbeat()
            try:
                await asyncio.sleep(self._hb_interval_seconds)
            except asyncio.exceptions.CancelledError:
                break

    async def PollTask(self, handler):
        """
        handler needs to handle every response message type defined in the FLOP
        protocol, e.g. OrderAccepted, OrderRemoved, Trade, OrderReject,
        CancelReject
        """
        while True:
            try:
                buffer = bytearray()
                buffer += await self._reader.readexactly(2)
                msg_size = int.from_bytes(buffer[0:2], byteorder='little', signed=False)
                buffer += await self._reader.readexactly(msg_size)

                # buffer now contains the entire message

                header = flop.Header.fromBytes(bytes(buffer))

                parsedMsg = None
                match header.msg_type:
                    case flop.MsgType.LoginResponse_:
                        pass
                    case flop.MsgType.Heartbeat_:
                        pass
                    case flop.MsgType.LogoutResponse_:
                        pass
                    case flop.MsgType.OrderAccepted_:
                        parsedMsg = flop.MessageOrderAccepted.fromBytes(bytes(buffer))
                    case flop.MsgType.OrderRemoved_:
                        parsedMsg = flop.MessageOrderRemoved.fromBytes(bytes(buffer))
                    case flop.MsgType.Trade_:
                        parsedMsg  = flop.MessageTrade.fromBytes(bytes(buffer))
                    case flop.MsgType.OrderReject_:
                        parsedMsg = flop.MessageOrderReject.fromBytes(bytes(buffer))
                    case flop.MsgType.CancelReject_:
                        parsedMsg = flop.MessageCancelReject.fromBytes(bytes(buffer))
                    case _:
                        print("Unexpected msg type: {}", header.msg_type)
                if parsedMsg:
                    await handler(parsedMsg.body)
            except asyncio.exceptions.CancelledError:
                break

