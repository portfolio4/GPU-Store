#!/usr/bin/env python3
#
# Requires fastapi, uvicorn, websockets are installed, e.g.
# `apt-get install python3-fastapi python3-uvicorn python3-websockets`

import sys
sys.path.append("python/")

import asyncio
import contextlib
import fastapi
import fastapi.staticfiles
import flop
import funx.webserver.FlipListener
import funx.webserver.FlipResponseHandler
import funx.webserver.FlopGateway
import funx.webserver.FlopResponseHandler
import funx.webserver.WebSocketManager
import json
import os
import uvicorn

def LoadConfig():
    if len(sys.argv) != 3:
        print("Provide 2 arguments: the config.json file and the instance name")
        sys.exit(1)

    config_file = sys.argv[1]

    with open(config_file, "r") as f:
        config_json = json.load(f)

    return config_json


def CreateFlopGateway(config_json, instance):
    cfg = config_json[instance]
    gateway_name = cfg["gateway"]
    sessions = cfg["gateway_sessions"]

    gateway_cfg = config_json[gateway_name]

    return funx.webserver.FlopGateway.FlopGateway(gateway_cfg, sessions)

def CreateFlipListeners(config_json, instances):
    cfg = config_json[instance]
    md_services = cfg["md_services"]
    listeners = []
    for md_instance in md_services:
        md_cfg = config_json[md_instance]
        listeners.append(funx.webserver.FlipListener.FlipListener(md_cfg))
    return listeners

instance = sys.argv[2]
config_json = LoadConfig()
flop_gateway = CreateFlopGateway(config_json, instance)
flip_listeners = CreateFlipListeners(config_json, instance)

# The focus here is not on building a full-fledged web app, so we're going to
# defer on OAuth and logging in. We'll just assume the client has logged in
# with company ID 2 which is the retail ID
LOGGED_IN_COMPANY_ID = 2


# ------------------ FAST API ----------------------------------- #

websocket_mgr = funx.webserver.WebSocketManager.WebSocketManager()

@contextlib.asynccontextmanager
async def FastAPILifespan(app: fastapi.FastAPI):
    flop_handler = funx.webserver.FlopResponseHandler.FlopResponseHandler(websocket_mgr, flop_gateway)
    tasks = await flop_gateway.SetUpSessionPolling(flop_handler)

    flip_handler = funx.webserver.FlipResponseHandler.FlipResponseHandler(websocket_mgr)
    for listener in flip_listeners:
        await listener.InitAsync(flip_handler)


    yield

    for task in tasks:
        task.cancel()
    for task in tasks:
        await task


app = fastapi.FastAPI(lifespan=FastAPILifespan)

@app.post("/api/order")
async def send_order(order: funx.webserver.FlopGateway.OrderRequest):
    try:
        flop_gateway.SendOrder(order, company_id=LOGGED_IN_COMPANY_ID)
    except ValueError as e:
        raise fastapi.HTTPException(status_code=400, detail=str(e))

@app.delete("/api/order")
async def send_order(cancel: funx.webserver.FlopGateway.CancelRequest):
    try:
        flop_gateway.CancelOrder(cancel)
    except ValueError as e:
        raise fastapi.HTTPException(status_code=400, detail=str(e))

@app.websocket("/ws")
async def websocket_endpoint(websocket: fastapi.WebSocket):
    global websocket_mgr
    await websocket_mgr.connect(websocket)
    try:
        while True:
            data = await websocket.receive_text()
    except fastapi.WebSocketDisconnect:
        websocket_mgr.disconnect(websocket)

app.mount("/", fastapi.staticfiles.StaticFiles(directory="webstatic", html=True), name="static")
uvicorn.run(app, host=config_json[instance]["webserver_ip4"], port=config_json[instance]["webserver_port"])

