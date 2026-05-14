import asyncio
import flip
import socket
import struct

class FlipListener(object):
    def __init__(self, md_json_cfg):
        self._snapshot_ip = md_json_cfg["multicast_snapshot_ip4"]
        self._snapshot_port = md_json_cfg["multicast_snapshot_port"]

        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        self._socket.bind(('', self._snapshot_port))

        group = socket.inet_aton(self._snapshot_ip)
        mreq = struct.pack('4sL', group, socket.INADDR_ANY)
        self._socket.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

    async def InitAsync(self, handler):
        self._handler = handler
        self._message_queue = asyncio.Queue()
        self._consumer_task = asyncio.create_task(self._ConsumeMessages())

        flip_listener = self
        class FlipProtocol(asyncio.DatagramProtocol):
            def connection_made(self, transport):
                self._transport = transport

            def datagram_received(self, data, addr):
                flip_listener._message_queue.put_nowait(data)

        loop = asyncio.get_running_loop()
        self._transport, self._protocol = await loop.create_datagram_endpoint(
            FlipProtocol,
            sock=self._socket
        )

    async def _ConsumeMessages(self):
        while True:
            data = await self._message_queue.get()
            try:
                await self.HandleFlipMsg(data)
            except Exception as error:
                print(f"Failed to handle FLIP message: {error}")
            finally:
                self._message_queue.task_done()

    async def HandleFlipMsg(self, data):
        """
        handler needs to handle every response message type defined in the FLOP
        protocol, e.g. OrderAccepted, OrderRemoved, Trade, OrderReject,
        CancelReject
        """
        if len(data) < flip.HEADER_SIZE:
            print(f"FLIP msg smaller than header: len={len(data)}")
            return
        header = flip.Header.fromBytes(bytes(data))

        # msg_size excludes the two bytes occupied by the msg_size field itself.
        expected_size = header.msg_size + 2
        if len(data) != expected_size:
            print(f"FLIP msg size mismatch: expected={expected_size}, actual={len(data)}")
            return
        match header.msg_type:
            case flip.MsgType.AddOrder_:
                parsed_msg = flip.MessageAddOrder.fromBytes(bytes(data))
            case flip.MsgType.RemoveOrder_:
                parsed_msg = flip.MessageRemoveOrder.fromBytes(bytes(data))
            case flip.MsgType.ModifyOrder_:
                parsed_msg = flip.MessageModifyOrder.fromBytes(bytes(data))
            case flip.MsgType.Trade_:
                parsed_msg = flip.MessageTrade.fromBytes(bytes(data))
            case flip.MsgType.Snapshot_:
                parsed_msg = flip.MessageSnapshot.fromBytes(bytes(data))
            case flip.MsgType.Heartbeat_:
                return
            case _:
                print(f"Got unexpected FLIP msg type: {header.msg_type}")
                return

        await self._handler(parsed_msg.body)

