#!/usr/bin/env python3
"""A small MQTT 3.1.1 broker for testing internet play's signalling offline
(port/linux/src/p2p_signal.c), in place of the public brokers.

It does what p2p_signal.c uses, as the public brokers do: CONNECT with a will
(published when the connection drops without a DISCONNECT), SUBSCRIBE and
UNSUBSCRIBE with the + and # wildcards, PUBLISH at QoS 0 with retained
messages (an empty retained message removes the one kept; a subscriber gets
the kept ones at once with the retain flag set, live ones without it), and
PINGREQ. Everything is logged to standard output, one line per packet.

    python3 mqtt_test_broker.py [--port 18830] [--host 127.0.0.1]
"""

import argparse
import asyncio
import sys

retained = {}
clients = set()


def log(*parts):
    print(*parts, flush=True)


def matches(pattern, topic):
    pattern_parts = pattern.split("/")
    topic_parts = topic.split("/")
    for index, part in enumerate(pattern_parts):
        if part == "#":
            return True
        if index >= len(topic_parts):
            return False
        if part != "+" and part != topic_parts[index]:
            return False
    return len(pattern_parts) == len(topic_parts)


def encode_length(length):
    out = bytearray()
    while True:
        byte = length & 127
        length >>= 7
        out.append(byte | 128 if length else byte)
        if not length:
            return bytes(out)


def string(data, offset):
    size = data[offset] << 8 | data[offset + 1]
    return data[offset + 2:offset + 2 + size], offset + 2 + size


class Client:
    def __init__(self, reader, writer):
        self.reader = reader
        self.writer = writer
        self.subscriptions = []
        self.will = None
        self.name = "?"

    def send(self, packet_type, body):
        try:
            self.writer.write(bytes([packet_type]) + encode_length(len(body)) + body)
        except Exception:
            pass

    def deliver(self, topic, payload, retain):
        body = len(topic).to_bytes(2, "big") + topic.encode() + payload
        self.send(0x31 if retain else 0x30, body)


def publish(topic, payload, retain, sender):
    if retain:
        if payload:
            retained[topic] = payload
        else:
            retained.pop(topic, None)
    log(f"publish {sender} {topic} {len(payload)} B{' retained' if retain else ''}")
    for client in list(clients):
        if any(matches(pattern, topic) for pattern in client.subscriptions):
            client.deliver(topic, payload, False)


async def read_packet(reader):
    header = await reader.readexactly(1)
    length = 0
    shift = 0
    while True:
        byte = (await reader.readexactly(1))[0]
        length |= (byte & 127) << shift
        shift += 7
        if not byte & 128:
            break
    body = await reader.readexactly(length) if length else b""
    return header[0], body


async def handle(reader, writer):
    client = Client(reader, writer)
    clean = False
    try:
        while True:
            packet_type, body = await read_packet(reader)
            kind = packet_type >> 4
            if kind == 1:  # CONNECT
                _, offset = string(body, 0)
                flags = body[offset + 1]
                offset += 4
                identifier, offset = string(body, offset)
                client.name = identifier.decode(errors="replace")
                if flags & 0x04:
                    will_topic, offset = string(body, offset)
                    will_message, offset = string(body, offset)
                    client.will = (will_topic.decode(), will_message, bool(flags & 0x20))
                clients.add(client)
                log(f"connect {client.name} will={client.will[0] if client.will else None}")
                client.send(0x20, b"\x00\x00")
            elif kind == 3:  # PUBLISH
                topic, offset = string(body, 0)
                if (packet_type >> 1) & 3:
                    offset += 2
                publish(topic.decode(), body[offset:], bool(packet_type & 1), client.name)
            elif kind == 8:  # SUBSCRIBE
                packet_id = body[:2]
                offset = 2
                codes = bytearray()
                while offset < len(body):
                    pattern, offset = string(body, offset)
                    offset += 1
                    pattern = pattern.decode()
                    client.subscriptions.append(pattern)
                    codes.append(0)
                    log(f"subscribe {client.name} {pattern}")
                client.send(0x90, packet_id + bytes(codes))
                for topic, payload in list(retained.items()):
                    if any(matches(pattern, topic) for pattern in client.subscriptions[-len(codes):]):
                        client.deliver(topic, payload, True)
            elif kind == 10:  # UNSUBSCRIBE
                packet_id = body[:2]
                offset = 2
                while offset < len(body):
                    pattern, offset = string(body, offset)
                    pattern = pattern.decode()
                    if pattern in client.subscriptions:
                        client.subscriptions.remove(pattern)
                    log(f"unsubscribe {client.name} {pattern}")
                client.send(0xB0, packet_id)
            elif kind == 12:  # PINGREQ
                client.send(0xD0, b"")
            elif kind == 14:  # DISCONNECT
                clean = True
                break
            await writer.drain()
    except (asyncio.IncompleteReadError, ConnectionError):
        pass
    finally:
        clients.discard(client)
        log(f"disconnect {client.name}{' (clean)' if clean else ''}")
        if client.will and not clean:
            publish(client.will[0], client.will[1], client.will[2], client.name + " (will)")
        writer.close()


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18830)
    arguments = parser.parse_args()
    server = await asyncio.start_server(handle, arguments.host, arguments.port)
    log(f"listening on {arguments.host}:{arguments.port}")
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        sys.exit(0)
