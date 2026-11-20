#!/usr/bin/env python3
"""Dev-only TLS test server for the `devhost` console command (FIX-06).

It stands in for api.wanikani.com so the parent can watch how WkClient behaves
on a stalled or slow peer. Nothing here talks to WaniKani. Stdlib only.

Modes:
  stall   accept TLS, read the request, never answer. A GET should give
          httpCode 0 only after its timeoutMs, not after 1 s.
  slow    wait --delay seconds (default 3), send the headers, then trickle the
          body at --rate bytes/s (default 1). A 200 that arrives after the
          delay shows the status-line read is not capped at 1 s; a body longer
          than the request timeout shows truncation at the deadline.
  log     answer every request at once with 200 and "{}". Use it with a POST
          /reviews or PUT /start: the log shows every request that arrived, so
          a resent POST would appear as a second line.

Every mode prints one line per request (time, client, method, path, body length,
body preview). Modes stall and slow print the request before they hold it.

The server binds 0.0.0.0 and prints the LAN IP. The TLS certificate is
self-signed; the device accepts it only while `devhost` is active, because that
is the only time it skips certificate checks.

Usage (from firmware/ or any directory):
  python3 tools/slow_server.py stall
  python3 tools/slow_server.py slow --delay 3 --rate 1 --body-len 200
  python3 tools/slow_server.py log --port 8443

The certificate comes from the openssl CLI into --certdir (default
/tmp/kanikey-devhost, outside the repository). Without openssl, make one by hand
and pass --cert/--key:
  openssl req -x509 -newkey rsa:2048 -nodes -days 3 -keyout key.pem -out cert.pem \
    -subj "/CN=kanikey-devhost" -addext "subjectAltName=DNS:kanikey-devhost"
"""
import argparse
import datetime
import os
import shutil
import socket
import ssl
import subprocess
import sys
import threading
import time

PREVIEW = 300


def lan_ip(override=None):
    if override:
        return override
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        # Connecting a UDP socket sends nothing; it only picks the route's source IP.
        probe.connect(("10.255.255.255", 1))
        return probe.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        probe.close()


def make_cert(certdir, ip):
    """Create cert.pem and key.pem with the openssl CLI; return their paths."""
    if shutil.which("openssl") is None:
        sys.exit(
            "openssl not found. Make the certificate by hand, then pass --cert and --key:\n"
            '  openssl req -x509 -newkey rsa:2048 -nodes -days 3 -keyout key.pem -out cert.pem '
            '-subj "/CN=kanikey-devhost" -addext "subjectAltName=DNS:kanikey-devhost,IP:%s"' % ip
        )
    os.makedirs(certdir, exist_ok=True)
    cert = os.path.join(certdir, "cert.pem")
    key = os.path.join(certdir, "key.pem")
    cmd = [
        "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "3",
        "-keyout", key, "-out", cert, "-subj", "/CN=kanikey-devhost",
        "-addext", "subjectAltName=DNS:kanikey-devhost,IP:%s" % ip,
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit("openssl failed (%d): %s" % (result.returncode, result.stderr.strip()))
    return cert, key


def stamp():
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def log_request(peer, method, path, body, counter):
    preview = body[:PREVIEW].decode("utf-8", "replace").replace("\r", "\\r").replace("\n", "\\n")
    print("%s #%d %s %s %s body %d B %s" % (stamp(), counter, peer, method, path, len(body), preview),
          flush=True)


def read_request(conn):
    """Read one request. Returns (method, path, body) or None on a bad request."""
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = conn.recv(4096)
        if not chunk:
            return None
        data += chunk
        if len(data) > 64 * 1024:
            return None
    head, _, rest = data.partition(b"\r\n\r\n")
    lines = head.split(b"\r\n")
    parts = lines[0].decode("latin-1", "replace").split(" ")
    if len(parts) < 2:
        return None
    method, path = parts[0], parts[1]
    length = 0
    for line in lines[1:]:
        name, _, value = line.partition(b":")
        if name.strip().lower() == b"content-length":
            try:
                length = int(value.strip())
            except ValueError:
                return None
    body = rest
    while len(body) < length and len(body) < 1024 * 1024:
        chunk = conn.recv(min(4096, length - len(body)))
        if not chunk:
            break
        body += chunk
    return method, path, body[:length]


class Counter:
    def __init__(self):
        self._lock = threading.Lock()
        self._n = 0

    def next(self):
        with self._lock:
            self._n += 1
            return self._n


def respond(conn, status, body, reason="OK"):
    head = (
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n" % (status, reason, len(body))
    ).encode("ascii")
    conn.sendall(head + body)


def handle(conn, peer, mode, args, counter):
    try:
        conn.settimeout(15)
        request = read_request(conn)
        if request is None:
            return
        method, path, body = request
        log_request(peer, method, path, body, counter)
        if mode == "log":
            respond(conn, 200, b"{}")
        elif mode == "stall":
            # Hold the socket open with no answer until the device gives up.
            conn.settimeout(900)
            while conn.recv(1024):
                pass
        elif mode == "slow":
            time.sleep(args.delay)
            payload = (b'{"pad":"' + b"x" * max(0, args.body_len - 10) + b'"}')[: args.body_len]
            head = (
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %d\r\n"
                "Connection: close\r\n\r\n" % len(payload)
            ).encode("ascii")
            conn.sendall(head)
            for i in range(len(payload)):
                conn.sendall(payload[i:i + 1])
                time.sleep(1.0 / args.rate)
            print("%s #%d sent %d body bytes" % (stamp(), counter, len(payload)), flush=True)
    except (OSError, ssl.SSLError) as exc:
        print("%s #%d %s ended: %s" % (stamp(), counter, peer, exc), flush=True)
    finally:
        try:
            conn.close()
        except OSError:
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("mode", choices=["stall", "slow", "log"])
    parser.add_argument("--port", type=int, default=8443)
    parser.add_argument("--ip", help="LAN IP to print and put in the certificate (default: auto)")
    parser.add_argument("--cert", help="PEM certificate (default: made with openssl)")
    parser.add_argument("--key", help="PEM private key (default: made with openssl)")
    parser.add_argument("--certdir", default="/tmp/kanikey-devhost")
    parser.add_argument("--delay", type=float, default=3.0, help="slow: seconds before headers")
    parser.add_argument("--rate", type=float, default=1.0, help="slow: body bytes per second")
    parser.add_argument("--body-len", type=int, default=200, help="slow: body length in bytes")
    args = parser.parse_args()
    if args.rate <= 0 or args.body_len < 1 or args.delay < 0:
        sys.exit("--rate must be > 0, --body-len >= 1, --delay >= 0")

    ip = lan_ip(args.ip)
    if args.cert or args.key:
        if not (args.cert and args.key and os.path.isfile(args.cert) and os.path.isfile(args.key)):
            sys.exit("--cert and --key must both name existing files")
        cert, key = args.cert, args.key
    else:
        cert, key = make_cert(args.certdir, ip)

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    # The ESP32-S3 core in use speaks TLS 1.2 only.
    ctx.maximum_version = ssl.TLSVersion.TLSv1_2
    ctx.load_cert_chain(cert, key)

    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("0.0.0.0", args.port))
    listener.listen(8)

    print("kanikey devhost server: mode %s on https://%s:%d (bound 0.0.0.0)" % (args.mode, ip, args.port),
          flush=True)
    print("device console:  devhost %s %d" % (ip, args.port), flush=True)
    print("restore:         devhost off", flush=True)
    if args.mode == "slow":
        print("slow: headers after %.1f s, body %d B at %.2f B/s" % (args.delay, args.body_len, args.rate),
              flush=True)
    counter = Counter()

    def serve(raw, peer, n):
        try:
            conn = ctx.wrap_socket(raw, server_side=True)
        except (OSError, ssl.SSLError) as exc:
            print("%s #%d %s TLS handshake failed: %s" % (stamp(), n, peer, exc), flush=True)
            raw.close()
            return
        handle(conn, peer, args.mode, args, n)

    try:
        while True:
            raw, addr = listener.accept()
            peer = "%s:%d" % addr
            n = counter.next()
            threading.Thread(target=serve, args=(raw, peer, n), daemon=True).start()
    except KeyboardInterrupt:
        print("stopped", flush=True)
    finally:
        listener.close()


if __name__ == "__main__":
    main()
