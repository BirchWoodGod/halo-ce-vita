#!/usr/bin/env python3
"""A stand-in for the "crossplay-table" release's files on GitHub, for the
crossplay version table's fetch (port/linux/src/netversion_fetch.c;
run_netversion_fetch_harness_test.sh).

  netversions_test_server.py ROOT PORT_FILE REQUEST_LOG

Listens on 127.0.0.1 (a free port, written to PORT_FILE), answers each
request by its path, and appends each request's path and User-Agent to
REQUEST_LOG:

  /files/<case>/<name>      ROOT/<case>/<name>, by Content-Length; 404 if none
  /redirect/<case>/<name>   302 to /files/<case>/<name> on this server (as
                            GitHub's release download redirects to its storage)
  /elsewhere/<case>/<name>  302 to https://evil.example/<name> (refused)
  /big/<case>/<name>        Content-Length 100000, that many bytes
  /chunked/<case>/<name>    the file, chunked (refused: no Content-Length)
"""

import os
import socket
import sys
import threading

root, port_file, log_path = sys.argv[1], sys.argv[2], sys.argv[3]
lock = threading.Lock()


def respond(path):
    parts = path.split("?")[0].strip("/").split("/")
    if len(parts) != 3 or ".." in parts:
        return b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"
    kind, case, name = parts
    file_path = os.path.join(root, case, name)
    if kind == "redirect":
        return b"HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:%d/files/%s/%s\r\nContent-Length: 0\r\n\r\n" % (
            server_port, case.encode(), name.encode())
    if kind == "elsewhere":
        return b"HTTP/1.1 302 Found\r\nLocation: https://evil.example/%s\r\nContent-Length: 0\r\n\r\n" % name.encode()
    if kind == "big":
        return b"HTTP/1.1 200 OK\r\nContent-Length: 100000\r\n\r\n" + b"a" * 100000
    if not os.path.isfile(file_path):
        return b"HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found"
    with open(file_path, "rb") as file:
        body = file.read()
    if kind == "chunked":
        return b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n%x\r\n" % len(body) + body + b"\r\n0\r\n\r\n"
    if kind == "files":
        return b"HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %d\r\n\r\n" % len(
            body) + body
    return b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"


def serve(connection):
    try:
        connection.settimeout(10)
        request = b""
        while b"\r\n\r\n" not in request and len(request) < 65536:
            piece = connection.recv(4096)
            if not piece:
                break
            request += piece
        lines = request.decode("latin-1").split("\r\n")
        path = lines[0].split(" ")[1] if len(lines[0].split(" ")) > 1 else "/"
        agent = next((line.split(":", 1)[1].strip() for line in lines if line.lower().startswith("user-agent:")), "")
        cookie = any(line.lower().startswith("cookie:") for line in lines)
        with lock, open(log_path, "a") as log:
            log.write(f"{path} {agent}{' COOKIE' if cookie else ''}\n")
        connection.sendall(respond(path))
    except OSError:
        pass
    finally:
        connection.close()


listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
listener.bind(("127.0.0.1", 0))
listener.listen(16)
server_port = listener.getsockname()[1]
with open(port_file + ".tmp", "w") as file:
    file.write(str(server_port))
os.replace(port_file + ".tmp", port_file)
while True:
    connection, _ = listener.accept()
    threading.Thread(target=serve, args=(connection,), daemon=True).start()
