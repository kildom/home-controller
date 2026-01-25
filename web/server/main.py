#!/usr/bin/env python

import os
import re
import http
import json
from pathlib import Path
import signal
import asyncio

from websockets.exceptions import ConnectionClosed
from websockets.datastructures import Headers
from websockets.asyncio.server import serve, ServerConnection, Request, Response
from urllib.parse import urlparse

from serve_file import serve_file

from commands import execute_command
from ecdsa import ecdsa_verify


async def handler(websocket: ServerConnection):
    try:
        ch2 = os.urandom(32)
        await websocket.send(json.dumps({"type": "challenge", "challenge": ch2.hex()}))
        response = json.loads(await websocket.recv())
        if response.get("type") != "challenge_response" or len(str(response.get("signature"))) != 128:
            raise Exception("Invalid response")
        sig2 = str(response.get("signature"))
        # Verify signature sig2 with public key (x, y) on message ch2
        with open(Path(__file__).parent.parent / "client" / "static" / "auth.json", 'r') as f:
            authData = json.load(f)
        x = int(authData['x'], 16)
        y = int(authData['y'], 16)
        r = int(sig2[:64], 16)
        s = int(sig2[64:], 16)
        ok = ecdsa_verify(x, y, r, s, ch2)
        if not ok:
            raise Exception("Invalid signature")
        await websocket.send(json.dumps({"type": "challenge_accepted"}))
        while True:
            message = await websocket.recv()
            await execute_command(websocket, message)
    except ConnectionClosed as e:
        pass
    except Exception as e:
        print("Error in handler:", e)
    try:
        await websocket.close()
    except:
        pass


def process_request(connection: ServerConnection, request: Request):
    global challenges
    path = urlparse(request.path).path
    path = path.split('/')
    path = map(lambda x: re.sub(r'[^a-z0-9._-]', '', x, flags=re.IGNORECASE), path)
    path = filter(lambda x: x != '' and not x.startswith('.'), path)
    path = '/' + '/'.join(path)
    if path == '/':
        path = '/index.html'
    if path == '/connect':
        cookie = list(filter(lambda x: x[0].lower() == 'cookie', request.headers.items()))
        if len(cookie) != 1:
            return connection.respond(http.HTTPStatus.FORBIDDEN, 'Forbidden')
        cookie_value = cookie[0][1]
        match = re.search(r'connectKey=([0-9a-fA-F]{128})', cookie_value)
        if match is None:
            return connection.respond(http.HTTPStatus.FORBIDDEN, 'Forbidden')
        sig1 = match.group(1).lower()
        with open(Path(__file__).parent.parent / "client" / "static" / "auth.json", 'r') as f:
            authData = json.load(f)
        ch1 = bytes.fromhex(authData['ch1'])
        x = int(authData['x'], 16)
        y = int(authData['y'], 16)
        # Verify signature sig1 with public key (x, y) on message ch1
        r = int(sig1[:64], 16)
        s = int(sig1[64:], 16)
        ok = ecdsa_verify(x, y, r, s, ch1)
        if not ok:
            print("Invalid signature")
            return connection.respond(http.HTTPStatus.FORBIDDEN, 'Forbidden')
        return None
    else:
        return serve_file(path.strip('/'), connection, request)

async def main():
    port = int(os.environ.get("PORT", "8001"))
    async with serve(handler, "", port, process_request=process_request) as server:
        loop = asyncio.get_running_loop()
        loop.add_signal_handler(signal.SIGTERM, server.close)
        await server.wait_closed()


if __name__ == "__main__":
    asyncio.run(main())
