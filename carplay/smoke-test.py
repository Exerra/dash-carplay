#!/usr/bin/env python3
"""Exercise the real bundled receiver on loopback; no Bluetooth/phone or discovery."""
import argparse
import json
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
import urllib.request


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def auth_setup(port):
    # MFi-SAP v1 message with the X25519 base-point public key. Session-only test input.
    body = b"\x01\x09" + bytes(31)
    with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
        sock.sendall(("POST /auth-setup RTSP/1.0\r\nCSeq: 1\r\n"
                      f"Content-Type: application/octet-stream\r\nContent-Length: {len(body)}\r\n\r\n").encode() + body)
        response = bytearray()
        while b"\r\n\r\n" not in response:
            chunk = sock.recv(8192)
            if not chunk or len(response) > 65536: raise RuntimeError("Invalid RTSP headers")
            response.extend(chunk)
        headers, payload = bytes(response).split(b"\r\n\r\n", 1)
        if b" 200 " not in headers.split(b"\r\n", 1)[0]:
            raise RuntimeError("Local auth-setup request was rejected")
        fields = dict(line.split(b":", 1) for line in headers.split(b"\r\n")[1:] if b":" in line)
        length = int(next(v for k, v in fields.items() if k.lower() == b"content-length"))
        if not 40 <= length <= 1024 * 1024: raise RuntimeError("Invalid RTSP payload length")
        while len(payload) < length:
            chunk = sock.recv(length - len(payload))
            if not chunk: raise RuntimeError("Truncated RTSP payload")
            payload += chunk
        certificate_length = struct.unpack_from(">I", payload, 32)[0]
        offset = 36 + certificate_length
        if offset + 4 > len(payload): raise RuntimeError("Truncated certificate")
        signature_length = struct.unpack_from(">I", payload, offset)[0]
        if signature_length != 64 or offset + 4 + signature_length != len(payload):
            raise RuntimeError("Unexpected MFi-v3 signature response")
        return payload[36:offset]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--identity-dir", type=Path, required=True)
    args = parser.parse_args()
    receiver = Path(__file__).resolve().parent / "receiver"
    launcher = receiver / "server/build/install/server/bin/server"
    if not launcher.is_file(): raise SystemExit("Build the receiver with carplay/build.sh first")
    airplay, web = free_port(), free_port()
    while web == airplay: web = free_port()
    with tempfile.TemporaryDirectory(prefix="dash-carplay-smoke-") as temporary:
        state = Path(temporary)
        with (state / "stderr.log").open("wb") as log:
            process = subprocess.Popen([
                str(launcher), "--identity-dir", str(args.identity_dir.resolve()),
                "--state-dir", str(state), "--bind", "127.0.0.1", "--web-bind", "127.0.0.1",
                "--http", "--mdns", "none", "--airplay-port", str(airplay), "--http-port", str(web),
            ], cwd=receiver, stdout=subprocess.DEVNULL, stderr=log)
            try:
                descriptor = state / "connection.json"
                deadline = time.monotonic() + 20
                while not descriptor.exists() and time.monotonic() < deadline:
                    if process.poll() is not None: raise RuntimeError("Receiver exited before it became ready")
                    time.sleep(.05)
                if not descriptor.exists(): raise RuntimeError("Receiver startup timed out")
                connection = json.loads(descriptor.read_text())
                assert connection["url"].startswith(f"ws://127.0.0.1:{web}/ws?token=")
                assert descriptor.stat().st_mode & 0o777 == 0o600
                with urllib.request.urlopen(f"http://127.0.0.1:{web}/api/status", timeout=5) as response:
                    assert response.status == 200
                    json.loads(response.read())
                certificate = auth_setup(airplay)
                assert certificate == (args.identity_dir / "offline-mfi/certificate.p7b").read_bytes()
                print("PASS: receiver startup, private native connection file, HTTP status and real MFi-SAP response")
                print("This checks the local responder; it does not prove iPhone acceptance.")
            finally:
                process.terminate()
                try: process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)
                assert not (state / "connection.json").exists(), "Receiver did not remove its connection file"


if __name__ == "__main__":
    main()
