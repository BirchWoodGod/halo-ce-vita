#!/usr/bin/env python3
"""Servers for broker_tls_test.c (run_broker_tls_test.sh): what a broker's
TLS port can do to internet play's TLS client (posix_tls_*, posix_https.c),
on 127.0.0.1, a port of the system's choosing written to --port-file.

    python3 broker_tls_server.py --mode MODE --port-file FILE
        [--cert CERT.pem --key KEY.pem] [--delay SECONDS]

  echo     TLS (--cert, --key): echoes what it is sent, starting --delay
           seconds after the handshake (its reads held back till then, so
           the client's sends fill the socket and wait)
  garbage  sends random bytes (no TLS) and closes
  silent   takes the connection and never answers
  close    closes each connection at once
The certificates are the test's, made at run time (never a real key).
"""

import argparse
import os
import socket
import ssl
import threading
import time


def serve_echo(connection, context, delay):
    try:
        with context.wrap_socket(connection, server_side=True) as tls:
            time.sleep(delay)
            while True:
                data = tls.recv(65536)
                if not data:
                    break
                tls.sendall(data)
    except (OSError, ssl.SSLError):
        pass


def serve_garbage(connection):
    try:
        connection.recv(4096)
        for _ in range(8):
            connection.sendall(os.urandom(2048))
    except OSError:
        pass
    connection.close()


def serve_silent(connection):
    try:
        while connection.recv(4096):
            pass
    except OSError:
        pass
    connection.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=["echo", "garbage", "silent", "close"], required=True)
    parser.add_argument("--port-file", required=True)
    parser.add_argument("--cert")
    parser.add_argument("--key")
    parser.add_argument("--delay", type=float, default=0)
    arguments = parser.parse_args()
    context = None
    if arguments.mode == "echo":
        context = ssl.create_default_context(ssl.Purpose.CLIENT_AUTH)
        context.load_cert_chain(arguments.cert, arguments.key)
    listener = socket.socket()
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", 0))
    listener.listen(16)
    with open(arguments.port_file + ".new", "w") as file:
        file.write(str(listener.getsockname()[1]))
    os.rename(arguments.port_file + ".new", arguments.port_file)
    while True:
        connection, _ = listener.accept()
        if arguments.mode == "echo":
            target, extra = serve_echo, (context, arguments.delay)
        elif arguments.mode == "garbage":
            target, extra = serve_garbage, ()
        elif arguments.mode == "silent":
            target, extra = serve_silent, ()
        else:
            connection.close()
            continue
        threading.Thread(target=target, args=(connection,) + extra, daemon=True).start()


if __name__ == "__main__":
    main()
