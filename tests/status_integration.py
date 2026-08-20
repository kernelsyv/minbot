"""End-to-end status protocol test using only the Python standard library."""

from __future__ import annotations

import socket
import subprocess
import sys
from pathlib import Path


def encode_varint(value: int) -> bytes:
    output = bytearray()
    while True:
        current = value & 0x7F
        value >>= 7
        if value:
            current |= 0x80
        output.append(current)
        if not value:
            return bytes(output)


def receive_exact(connection: socket.socket, length: int) -> bytes:
    output = bytearray()
    while len(output) < length:
        chunk = connection.recv(length - len(output))
        if not chunk:
            raise RuntimeError("client closed the test connection early")
        output.extend(chunk)
    return bytes(output)


def receive_varint(connection: socket.socket) -> int:
    result = 0
    for index in range(5):
        current = receive_exact(connection, 1)[0]
        result |= (current & 0x7F) << (7 * index)
        if not current & 0x80:
            return result
    raise RuntimeError("client sent an oversized VarInt")


def receive_packet(connection: socket.socket) -> bytes:
    length = receive_varint(connection)
    if length <= 0 or length > 1024 * 1024:
        raise RuntimeError(f"invalid client packet length: {length}")
    return receive_exact(connection, length)


def send_packet(connection: socket.socket, body: bytes) -> None:
    connection.sendall(encode_varint(len(body)) + body)


def run_test(executable: Path) -> int:
    status_json = b'{"version":{"name":"1.21.1","protocol":767},"players":{"max":20,"online":0},"description":{"text":"local integration test"}}'

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(5)
        port = listener.getsockname()[1]

        client = subprocess.Popen(
            [str(executable), "status", "127.0.0.1", str(port), "767"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        try:
            connection, _ = listener.accept()
            with connection:
                connection.settimeout(5)

                handshake = receive_packet(connection)
                if not handshake or handshake[0] != 0x00:
                    raise RuntimeError("expected handshake packet ID 0x00")

                status_request = receive_packet(connection)
                if status_request != b"\x00":
                    raise RuntimeError("expected status request packet ID 0x00")

                status_body = b"\x00" + encode_varint(len(status_json)) + status_json
                send_packet(connection, status_body)

                ping = receive_packet(connection)
                if len(ping) != 9 or ping[0] != 0x01:
                    raise RuntimeError("expected a ping packet with an eight-byte payload")
                send_packet(connection, ping)

            stdout, stderr = client.communicate(timeout=5)
        finally:
            if client.poll() is None:
                client.kill()
                client.wait(timeout=5)

    if client.returncode != 0:
        raise RuntimeError(f"minbot exited with {client.returncode}: {stderr.strip()}")
    if status_json.decode() not in stdout:
        raise RuntimeError("status JSON is missing from minbot output")
    if "Ping:" not in stdout:
        raise RuntimeError("ping measurement is missing from minbot output")

    print("Local status integration test passed")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: status_integration.py <minbot-executable>")
    raise SystemExit(run_test(Path(sys.argv[1]).resolve()))
