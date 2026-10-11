#!/usr/bin/env python3
"""A stand-in for GitHub's releases API, for the update check's tests
(run_update_check_test.sh, run_update_check_harness_test.sh).

  update_test_server.py PORT_FILE REQUEST_LOG [--tls CERT KEY]

Listens on 127.0.0.1 (a free port, written to PORT_FILE), answers each
request by its path, and appends each request's path and User-Agent to
REQUEST_LOG. With --tls, the same over TLS with the certificate given (the
tests' own, which the update check must refuse).

  /releases/<tag>[/<prerelease 0|1>]   GitHub's list: that release first,
                                       then a draft and older ones
  /latest/<tag>                        /releases/latest's one release
  /chunked/<tag>                       the list, chunked
  /redirect                            301 to another host
  /status/<n>                          that status, a JSON error body
  /big                                 Content-Length 300000
  /big-chunked                         300000 bytes in chunks
  /cut                                 Content-Length 1000, 20 bytes sent
  /cut-chunked                         chunks, no last chunk
  /bad-chunk                           a chunk size that is not hex
  /no-length                           neither length nor chunks
  /headers                             a 20 KB header
  /slow                                headers, then nothing for 30 s
"""

import json
import os
import socket
import ssl
import sys
import threading
import time

port_file, log_path = sys.argv[1], sys.argv[2]
tls = len(sys.argv) > 5 and sys.argv[3] == "--tls"
lock = threading.Lock()


def releases(tag, prerelease):
    def release(name, pre, draft=False):
        return {"url": "https://api.github.com/x", "html_url": "https://evil.example/" + name, "tag_name": name,
                "name": "Name " + name, "draft": draft, "prerelease": pre, "body": "notes " * 200,
                "author": {"login": "someone", "tag_name": "v99.0.0"}, "assets": [{"name": "halo.vpk"}]}
    return [release(tag, prerelease), release("v99.0.0-beta.1", True, draft=True), release("v1.0.3", False),
            release("v1.0.3-beta.6", True), release("v1.0.2.2", False)]


def respond(path):
    """the raw response"""
    parts = path.strip("/").split("/")
    body = None
    if parts[0] == "releases" and len(parts) >= 2:
        body = json.dumps(releases(parts[1], len(parts) < 3 or parts[2] == "1")).encode()
    elif parts[0] == "latest" and len(parts) == 2:
        body = json.dumps(releases(parts[1], False)[0]).encode()
    if body is not None:
        return b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n" % len(
            body) + body
    if parts[0] == "chunked" and len(parts) == 2:
        body = json.dumps(releases(parts[1], True)).encode()
        out = b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        for at in range(0, len(body), 777):
            piece = body[at:at + 777]
            out += b"%x;ext=1\r\n" % len(piece) + piece + b"\r\n"
        return out + b"0\r\n\r\n"
    if parts[0] == "redirect":
        return b"HTTP/1.1 301 Moved\r\nLocation: https://evil.example/releases\r\nContent-Length: 0\r\n\r\n"
    if parts[0] == "status":
        body = b'{"message":"API rate limit exceeded"}'
        return b"HTTP/1.1 %s Error\r\nContent-Length: %d\r\n\r\n" % (parts[1].encode(), len(body)) + body
    if parts[0] == "big":
        return b"HTTP/1.1 200 OK\r\nContent-Length: 300000\r\n\r\n" + b"[" + b" " * 299998 + b"]"
    if parts[0] == "big-chunked":
        out = b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        for _ in range(30):
            out += b"2710\r\n" + b" " * 10000 + b"\r\n"
        return out + b"0\r\n\r\n"
    if parts[0] == "cut":
        return b"HTTP/1.1 200 OK\r\nContent-Length: 1000\r\n\r\n" + b"[" * 20
    if parts[0] == "cut-chunked":
        return b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\n[1,2]\r\n"
    if parts[0] == "bad-chunk":
        return b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n[]\r\n0\r\n\r\n"
    if parts[0] == "no-length":
        return b"HTTP/1.1 200 OK\r\n\r\n[]"
    if parts[0] == "headers":
        return b"HTTP/1.1 200 OK\r\nX-Pad: " + b"a" * 20000 + b"\r\nContent-Length: 2\r\n\r\n[]"
    if parts[0] == "slow":
        time.sleep(30)
        return b""
    return b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"


def serve(connection):
    try:
        connection.settimeout(10)
        request = b""
        while b"\r\n\r\n" not in request and len(request) < 65536:
            data = connection.recv(4096)
            if not data:
                return
            request += data
        lines = request.decode("latin-1").split("\r\n")
        path = lines[0].split(" ")[1]
        agent = next((line.split(":", 1)[1].strip() for line in lines if line.lower().startswith("user-agent:")), "")
        cookie = any(line.lower().startswith("cookie:") for line in lines)
        with lock, open(log_path, "a") as log:
            log.write("%s %s%s\n" % (path, agent, " COOKIE" if cookie else ""))
        connection.sendall(respond(path))
    except (OSError, ssl.SSLError):
        pass
    finally:
        try:
            connection.close()
        except OSError:
            pass


def main():
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", 0))
    listener.listen(16)
    context = None
    if tls:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(sys.argv[4], sys.argv[5])
    with open(port_file + ".new", "w") as file:
        file.write(str(listener.getsockname()[1]))
    os.replace(port_file + ".new", port_file)
    while True:
        connection, _ = listener.accept()
        if context:
            try:
                connection = context.wrap_socket(connection, server_side=True)
            except (OSError, ssl.SSLError):
                connection.close()
                continue
        threading.Thread(target=serve, args=(connection,), daemon=True).start()


main()
