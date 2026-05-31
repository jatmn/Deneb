#!/usr/bin/python3
# SPDX-License-Identifier: MPL-2.0
"""Small Digital Factory bridge for the native Deneb UI.

The stock Digital Factory pairing flow lives behind Gershwin IPC rather than
the legacy print-command ZMQ socket. This helper sends the same coordinator
requests as the stock menu and prints a compact status line for the C UI.
"""

import argparse
import errno
import sys
import time

import umsgpack
import zmq

from cygnus.marshal.types.digital_factory import (
    DigitalFactoryInstruction,
    DigitalFactoryRequest,
    DigitalFactoryState,
    DigitalFactoryStatusBreadcrumb,
)


IPC_BASE = "tcp://127.0.0.1:"
GERSHWIN_PUB_BASE = 5546
# Deneb replaces the stock Cygnus menu and reuses the stock GUI pubinstance.
# Coordinator and Digital Factory subscribe only to pubbase slots 5546-5549.
DENEB_GUI_PUB_PORT = 5547
SOURCE = "deneb/df-bridge"


def pack(obj):
    return umsgpack.packb(obj)


def unpack(obj):
    return umsgpack.unpackb(obj)


def open_sockets():
    ctx = zmq.Context()

    pub = ctx.socket(zmq.PUB)
    pub.setsockopt(zmq.LINGER, 0)
    try:
        pub.bind(f"{IPC_BASE}{DENEB_GUI_PUB_PORT}")
    except zmq.ZMQError as exc:
        pub.close(0)
        ctx.term()
        if exc.errno == errno.EADDRINUSE:
            raise RuntimeError(
                "status=error reason=stock-menu-slot-in-use"
            ) from exc
        raise

    sub = ctx.socket(zmq.SUB)
    sub.setsockopt(zmq.LINGER, 0)
    sub.setsockopt_string(zmq.SUBSCRIBE, "")
    for offset in range(4):
        sub.connect(f"{IPC_BASE}{GERSHWIN_PUB_BASE + offset}")

    # ZMQ PUB/SUB drops messages until subscriptions settle.
    time.sleep(1.0)
    return ctx, pub, sub


def send_request(pub, action):
    instruction = {
        "connect": DigitalFactoryInstruction.CONNECT,
        "disconnect": DigitalFactoryInstruction.DISCONNECT,
    }[action]

    req = DigitalFactoryRequest.create(tracker=0, instruction=instruction)
    key = {
        "ts": int(time.monotonic() * 1000),
        "action": "rpc-request",
        "source": SOURCE,
        "target": "coordinator/coordinator::digitalfactory::handling@execute|D1",
    }
    pub.send_multipart([pack(key), pack(req.serialize())])


def read_status(sub, timeout_s):
    deadline = time.monotonic() + timeout_s
    poller = zmq.Poller()
    poller.register(sub, zmq.POLLIN)
    last_status = None
    accepted = None

    while time.monotonic() < deadline:
        remaining = max(0, int((deadline - time.monotonic()) * 1000))
        if not dict(poller.poll(remaining)).get(sub):
            break

        key_msg, data_msg = sub.recv_multipart()
        key = unpack(key_msg)
        data = unpack(data_msg)
        action = key.get("action")
        target = key.get("target", "")

        if action == "rpc-reply" and target == f"{SOURCE}/rpc@reply|D1":
            accepted = data.get("accepted")
        elif action == "drop" and target == DigitalFactoryStatusBreadcrumb.id:
            last_status = data
            if data.get("pin") or data.get("state") == DigitalFactoryState.CONNECTED:
                break

    return accepted, last_status


def format_status(accepted, status):
    parts = []
    if accepted is not None:
        parts.append(f"accepted={1 if accepted else 0}")
    if status:
        state = status.get("state", "")
        try:
            state = DigitalFactoryState(state).name.lower()
        except Exception:
            state = str(state)
        parts.append(f"state={state}")
        if status.get("pin"):
            parts.append(f"pin={status['pin']}")
    if not parts:
        parts.append("status=timeout")
    return " ".join(parts)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["connect", "disconnect", "status"])
    parser.add_argument("--timeout", type=float, default=20.0)
    args = parser.parse_args()

    try:
        ctx, pub, sub = open_sockets()
        try:
            if args.action in ("connect", "disconnect"):
                send_request(pub, args.action)
            accepted, status = read_status(sub, args.timeout)
            print(format_status(accepted, status), flush=True)
        finally:
            pub.close(0)
            sub.close(0)
            ctx.term()
    except RuntimeError as exc:
        print(str(exc), flush=True)
        return 1


if __name__ == "__main__":
    sys.exit(main())
