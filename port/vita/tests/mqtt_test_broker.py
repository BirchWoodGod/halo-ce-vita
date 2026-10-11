#!/usr/bin/env python3
"""A small MQTT 5 and 3.1.1 broker for testing internet play's signalling
offline (port/linux/src/p2p_signal.c), in place of the public brokers.

It does what p2p_signal.c uses, as the public brokers do: CONNECT with a will
(published when the connection drops without a DISCONNECT), SUBSCRIBE and
UNSUBSCRIBE with the + and # wildcards (MQTT 5: No Local), PUBLISH at QoS 0
and 1 (PUBACK) with retained messages (an empty retained message removes the
one kept; a subscriber gets the kept ones at once with the retain flag set,
live ones without it; MQTT 5's Message Expiry Interval drops a kept one when
it lapses), and PINGREQ. As the public brokers do, a client silent for one
and a half times its keep alive is dropped (its will published), and a
CONNECT with the client identifier of a connection still open takes its
session over: the old connection is closed and its will published, late
(--takeover-will-delay seconds, 0.5 by default: a cluster's will can land
after the new connection's first publishes), and No Local keeps from a
connection whatever was published under its client identifier, its old
connection's will too. Everything is logged to standard output, one line per
packet, and each change of a retained message as "retained SECONDS TOPIC
SIZE B" (SECONDS since the broker started; "removed" or "expired" for one
gone), so that a test can tell what a topic held when.

    python3 mqtt_test_broker.py [--port 18830] [--host 127.0.0.1] [--mqtt311]
        [--takeover-will-delay 0.5]

--mqtt311 refuses MQTT 5 as a 3.1.1 broker does (CONNACK 1), so the client
must connect again with 3.1.1.
"""

import argparse
import asyncio
import sys
import time

# topic -> (payload, expiry time or None)
retained = {}
clients = set()
only_311 = False
takeover_will_delay = 0.5
started = time.monotonic()


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


def variable(data, offset):
    value = 0
    shift = 0
    while True:
        byte = data[offset]
        offset += 1
        value |= (byte & 127) << shift
        shift += 7
        if not byte & 128:
            return value, offset


def properties(data, offset):
    """MQTT 5's properties: (the message expiry if any, the offset after)"""
    length, offset = variable(data, offset)
    end = offset + length
    expiry = None
    while offset < end:
        identifier = data[offset]
        offset += 1
        if identifier == 0x02:
            expiry = int.from_bytes(data[offset:offset + 4], "big")
            offset += 4
        elif identifier in (0x01, 0x17, 0x19, 0x24, 0x25, 0x28, 0x29, 0x2A):
            offset += 1
        elif identifier in (0x13, 0x21, 0x22, 0x23):
            offset += 2
        elif identifier in (0x11, 0x18, 0x27):
            offset += 4
        elif identifier == 0x0B:
            _, offset = variable(data, offset)
        elif identifier == 0x26:
            _, offset = string(data, offset)
            _, offset = string(data, offset)
        else:
            _, offset = string(data, offset)
    return expiry, end


class Client:
    def __init__(self, reader, writer):
        self.reader = reader
        self.writer = writer
        self.subscriptions = []
        self.will = None
        self.name = "?"
        self.protocol = 4
        # pattern -> no local
        self.no_local = {}
        self.keep_alive = 0
        # closed by another connection's CONNECT with its client identifier
        self.taken_over = False

    def send(self, packet_type, body):
        try:
            self.writer.write(bytes([packet_type]) + encode_length(len(body)) + body)
        except Exception:
            pass

    def deliver(self, topic, payload, retain):
        body = len(topic).to_bytes(2, "big") + topic.encode() + (b"\x00" if self.protocol == 5 else b"") + payload
        self.send(0x31 if retain else 0x30, body)


def log_retained(topic, what):
    log(f"retained {time.monotonic() - started:.1f} {topic} {what}")


def kept():
    now = time.monotonic()
    for topic, (payload, expiry) in list(retained.items()):
        if expiry is not None and expiry <= now:
            del retained[topic]
            log_retained(topic, "expired")
    return {topic: payload for topic, (payload, _) in retained.items()}


def publish(topic, payload, retain, sender, expiry=None, qos=0, origin=None):
    """origin: the client identifier it was published under (No Local)"""
    if retain:
        if payload:
            retained[topic] = (payload, time.monotonic() + expiry if expiry is not None else None)
            log_retained(topic, f"{len(payload)} B")
        elif topic in retained:
            retained.pop(topic, None)
            log_retained(topic, "removed")
    log(f"publish {sender} {topic} {len(payload)} B{' retained' if retain else ''}{' qos1' if qos else ''}"
        f"{f' expiry={expiry}' if expiry is not None else ''}")
    for client in list(clients):
        wanted = [pattern for pattern in client.subscriptions if matches(pattern, topic)]
        if origin is not None and client.name == origin:
            wanted = [pattern for pattern in wanted if not client.no_local.get(pattern)]
        if wanted:
            client.deliver(topic, payload, False)


async def expire_retained():
    while True:
        await asyncio.sleep(1)
        kept()


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
            # (one and a half times the keep alive without a packet: dropped)
            if client.keep_alive:
                try:
                    packet_type, body = await asyncio.wait_for(read_packet(reader), client.keep_alive * 1.5)
                except asyncio.TimeoutError:
                    log(f"keep alive lapsed {client.name}")
                    break
            else:
                packet_type, body = await read_packet(reader)
            kind = packet_type >> 4
            if kind == 1:  # CONNECT
                _, offset = string(body, 0)
                level = body[offset]
                flags = body[offset + 1]
                offset += 4
                if level == 5 and only_311:
                    log("connect refused: MQTT 5 (a 3.1.1 broker)")
                    client.send(0x20, b"\x00\x01")
                    await writer.drain()
                    break
                client.protocol = 5 if level == 5 else 4
                client.keep_alive = body[offset - 2] << 8 | body[offset - 1]
                if client.protocol == 5:
                    _, offset = properties(body, offset)
                identifier, offset = string(body, offset)
                client.name = identifier.decode(errors="replace")
                if flags & 0x04:
                    if client.protocol == 5:
                        _, offset = properties(body, offset)
                    will_topic, offset = string(body, offset)
                    will_message, offset = string(body, offset)
                    client.will = (will_topic.decode(), will_message, bool(flags & 0x20))
                # (a connection still open under this identifier: its session
                # taken over, the old connection closed; its will follows)
                for old in [other for other in clients if other.name == client.name]:
                    log(f"takeover {client.name}")
                    old.taken_over = True
                    clients.discard(old)
                    old.writer.close()
                clients.add(client)
                log(f"connect {client.name} mqtt{client.protocol} will={client.will[0] if client.will else None}")
                client.send(0x20, b"\x00\x00\x00" if client.protocol == 5 else b"\x00\x00")
            elif kind == 3:  # PUBLISH
                topic, offset = string(body, 0)
                qos = (packet_type >> 1) & 3
                packet_id = None
                if qos:
                    packet_id = body[offset:offset + 2]
                    offset += 2
                expiry = None
                if client.protocol == 5:
                    expiry, offset = properties(body, offset)
                publish(topic.decode(), body[offset:], bool(packet_type & 1), client.name, expiry, qos, client.name)
                if qos == 1:
                    client.send(0x40, packet_id)
            elif kind == 4:  # PUBACK (of nothing this broker sends at QoS 1)
                pass
            elif kind == 8:  # SUBSCRIBE
                packet_id = body[:2]
                offset = 2
                if client.protocol == 5:
                    _, offset = properties(body, offset)
                codes = bytearray()
                patterns = []
                while offset < len(body):
                    pattern, offset = string(body, offset)
                    options = body[offset]
                    offset += 1
                    pattern = pattern.decode()
                    client.subscriptions.append(pattern)
                    client.no_local[pattern] = bool(options & 0x04) and client.protocol == 5
                    patterns.append(pattern)
                    codes.append(0)
                    log(f"subscribe {client.name} {pattern}")
                client.send(0x90, packet_id + (b"\x00" if client.protocol == 5 else b"") + bytes(codes))
                for topic, payload in kept().items():
                    if any(matches(pattern, topic) for pattern in patterns):
                        client.deliver(topic, payload, True)
            elif kind == 10:  # UNSUBSCRIBE
                packet_id = body[:2]
                offset = 2
                if client.protocol == 5:
                    _, offset = properties(body, offset)
                count = 0
                while offset < len(body):
                    pattern, offset = string(body, offset)
                    pattern = pattern.decode()
                    if pattern in client.subscriptions:
                        client.subscriptions.remove(pattern)
                    log(f"unsubscribe {client.name} {pattern}")
                    count += 1
                client.send(0xB0, packet_id + (b"\x00" + bytes(count) if client.protocol == 5 else b""))
            elif kind == 12:  # PINGREQ
                client.send(0xD0, b"")
            elif kind == 14:  # DISCONNECT
                clean = True
                break
            await writer.drain()
    except (asyncio.IncompleteReadError, ConnectionError, OSError):
        pass
    finally:
        clients.discard(client)
        log(f"disconnect {client.name}{' (clean)' if clean else ''}{' (taken over)' if client.taken_over else ''}")
        if client.will and not clean:
            will = (client.will[0], client.will[1], client.will[2], client.name + " (will)", None, 0, client.name)
            if client.taken_over:
                asyncio.get_running_loop().call_later(takeover_will_delay, publish, *will)
            else:
                publish(*will)
        writer.close()


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18830)
    parser.add_argument("--mqtt311", action="store_true")
    parser.add_argument("--takeover-will-delay", type=float, default=0.5)
    arguments = parser.parse_args()
    global only_311, takeover_will_delay
    only_311 = arguments.mqtt311
    takeover_will_delay = arguments.takeover_will_delay
    server = await asyncio.start_server(handle, arguments.host, arguments.port)
    log(f"listening on {arguments.host}:{arguments.port}")
    asyncio.get_running_loop().create_task(expire_retained())
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        sys.exit(0)
