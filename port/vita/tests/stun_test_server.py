#!/usr/bin/env python3
"""A small STUN server (RFC 5389 binding requests only) for testing internet
play offline (port/linux/src/p2p.c's stun_send), in place of the public STUN
servers: it answers each binding request with the address and port it came
from, as XOR-MAPPED-ADDRESS and MAPPED-ADDRESS.

    python3 stun_test_server.py [--host 0.0.0.0] [--port 3478]
"""

import argparse
import socket
import struct

MAGIC = 0x2112A442


def answer(request, address):
    if len(request) < 20 or request[0:2] != b"\x00\x01":
        return None
    transaction = request[8:20]
    ip = socket.inet_aton(address[0])
    port = address[1]
    xport = port ^ (MAGIC >> 16)
    xip = bytes(a ^ b for a, b in zip(ip, struct.pack(">I", MAGIC)))
    attributes = struct.pack(">HHBBH", 0x0020, 8, 0, 1, xport) + xip
    attributes += struct.pack(">HHBBH", 0x0001, 8, 0, 1, port) + ip
    return struct.pack(">HHI", 0x0101, len(attributes), MAGIC) + transaction + attributes


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=3478)
    args = parser.parse_args()
    server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server.bind((args.host, args.port))
    print(f"stun: listening on {args.host}:{args.port}", flush=True)
    while True:
        request, address = server.recvfrom(2048)
        reply = answer(request, address)
        if reply:
            server.sendto(reply, address)
            print(f"stun: {address[0]}:{address[1]}", flush=True)


if __name__ == "__main__":
    main()
