#!/usr/bin/env python3
"""A stand-in for GitHub's release downloads, for the Vita updater's tests
(run_update_install_test.sh, update_install_harness.c).

  update_install_server.py ROOT PORT_FILE REQUEST_LOG [--tls CERT KEY]

Listens on 127.0.0.1 (a free port, written to PORT_FILE) and serves the
files under ROOT; each request's path and Range header go to REQUEST_LOG.
With --tls, the same over TLS with the certificate given.

  /dl/<tag>/<file>        the file (Range: bytes=N- answered 206)
  /redirect/<tag>/<file>  302 to /dl/<tag>/<file> on this server (http)
  /redirect2/<tag>/<file> 302 to /redirect/... (two hops)
  /evil/<tag>/<file>      302 to https://evil.example/<file>
  /cut/<tag>/<file>       the first request for each file: its full
                          Content-Length, half the bytes, then closed; the
                          next ones as /dl/
  /chunked/<tag>/<file>   the file, chunked
  /wrong-range/<tag>/<f>  a Range answered with another part
  /big/<tag>/<file>       Content-Length 70000000, nothing sent
"""

import os
import socket
import ssl
import sys
import threading

root, port_file, log_path = sys.argv[1], sys.argv[2], sys.argv[3]
tls = len(sys.argv) > 6 and sys.argv[4] == "--tls"
lock = threading.Lock()
cut_done = set()
port = 0


def read_file(tag, name):
    path = os.path.realpath(os.path.join(root, tag, name))
    if not path.startswith(os.path.realpath(root) + os.sep) or not os.path.isfile(path):
        return None
    with open(path, "rb") as file:
        return file.read()


def respond(path, range_start, connection):
    parts = path.strip("/").split("/")
    if len(parts) != 3:
        connection.sendall(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n")
        return
    kind, tag, name = parts
    if kind == "redirect":
        location = "http://127.0.0.1:%d/dl/%s/%s" % (port, tag, name)
        connection.sendall(b"HTTP/1.1 302 Found\r\nLocation: %s\r\nContent-Length: 0\r\n\r\n" % location.encode())
        return
    if kind == "redirect2":
        location = "http://127.0.0.1:%d/redirect/%s/%s" % (port, tag, name)
        connection.sendall(b"HTTP/1.1 302 Found\r\nLocation: %s\r\nContent-Length: 0\r\n\r\n" % location.encode())
        return
    if kind == "evil":
        location = "https://evil.example/%s" % name
        connection.sendall(b"HTTP/1.1 302 Found\r\nLocation: %s\r\nContent-Length: 0\r\n\r\n" % location.encode())
        return
    if kind == "big":
        connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 70000000\r\n\r\n")
        return
    data = read_file(tag, name)
    if data is None:
        connection.sendall(b"HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found")
        return
    if kind == "chunked":
        out = b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        out += b"%x\r\n" % len(data) + data + b"\r\n0\r\n\r\n"
        connection.sendall(out)
        return
    if kind == "cut":
        with lock:
            first = (tag, name) not in cut_done
            cut_done.add((tag, name))
        if first and range_start is None:
            connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n" % len(data) + data[:len(data) // 2])
            return
    if kind == "wrong-range" and range_start is not None:
        start = max(0, range_start - 10)
        body = data[start:]
        connection.sendall(b"HTTP/1.1 206 Partial Content\r\nContent-Range: bytes %d-%d/%d\r\nContent-Length: %d\r\n\r\n"
                           % (start, len(data) - 1, len(data), len(body)) + body)
        return
    if kind not in ("dl", "cut", "wrong-range"):
        connection.sendall(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n")
        return
    if range_start is not None and 0 < range_start < len(data):
        body = data[range_start:]
        connection.sendall(b"HTTP/1.1 206 Partial Content\r\nContent-Range: bytes %d-%d/%d\r\nContent-Length: %d\r\n\r\n"
                           % (range_start, len(data) - 1, len(data), len(body)) + body)
        return
    connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %d\r\n\r\n"
                       % len(data) + data)


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
        range_start = None
        range_text = ""
        for line in lines:
            if line.lower().startswith("range:"):
                range_text = line.split(":", 1)[1].strip()
                if range_text.startswith("bytes=") and range_text.endswith("-"):
                    range_start = int(range_text[6:-1])
        agent = next((line.split(":", 1)[1].strip() for line in lines if line.lower().startswith("user-agent:")), "")
        with lock, open(log_path, "a") as log:
            log.write("%s range=%s agent=%s\n" % (path, range_text or "-", agent))
        respond(path, range_start, connection)
    except (OSError, ssl.SSLError, ValueError):
        pass
    finally:
        try:
            connection.close()
        except OSError:
            pass


def main():
    global port
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", 0))
    listener.listen(16)
    port = listener.getsockname()[1]
    context = None
    if tls:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(sys.argv[5], sys.argv[6])
    with open(port_file + ".new", "w") as file:
        file.write(str(port))
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
