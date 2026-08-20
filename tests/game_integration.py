"""End-to-end offline login, play, chunk, and chat test."""

from __future__ import annotations

import socket
import struct
import subprocess
import sys
import time
import os
import zlib
from pathlib import Path


def encode_varint(value: int) -> bytes:
    value &= 0xFFFFFFFF
    output = bytearray()
    while True:
        current = value & 0x7F
        value >>= 7
        if value:
            current |= 0x80
        output.append(current)
        if not value:
            return bytes(output)


def protocol_string(value: str) -> bytes:
    encoded = value.encode()
    return encode_varint(len(encoded)) + encoded


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
    raise RuntimeError("oversized VarInt")


def decode_varint(data: bytes, offset: int = 0) -> tuple[int, int]:
    result = 0
    for index in range(5):
        if offset >= len(data):
            raise RuntimeError("truncated VarInt")
        current = data[offset]
        offset += 1
        result |= (current & 0x7F) << (7 * index)
        if not current & 0x80:
            return result, offset
    raise RuntimeError("oversized VarInt")


def receive_packet(connection: socket.socket, compression_threshold: int | None = None) -> bytes:
    length = receive_varint(connection)
    if length <= 0 or length > 16 * 1024 * 1024:
        raise RuntimeError(f"invalid packet length: {length}")
    wire_body = receive_exact(connection, length)
    if compression_threshold is None:
        return wire_body
    data_length, offset = decode_varint(wire_body)
    if data_length == 0:
        return wire_body[offset:]
    body = zlib.decompress(wire_body[offset:])
    if len(body) != data_length:
        raise RuntimeError("incorrect uncompressed packet length")
    return body


def send_packet(
    connection: socket.socket,
    body: bytes,
    compression_threshold: int | None = None,
) -> None:
    if compression_threshold is None:
        wire_body = body
    elif len(body) >= compression_threshold:
        wire_body = encode_varint(len(body)) + zlib.compress(body)
    else:
        wire_body = b"\x00" + body
    connection.sendall(encode_varint(len(wire_body)) + wire_body)


def packet_id(body: bytes) -> int:
    return decode_varint(body)[0]


def expect_packet(
    connection: socket.socket,
    expected_id: int,
    compression_threshold: int | None = None,
) -> bytes:
    body = receive_packet(connection, compression_threshold)
    actual = packet_id(body)
    if actual != expected_id:
        raise RuntimeError(f"expected packet 0x{expected_id:02x}, got 0x{actual:02x}")
    return body


def run_test(executable: Path) -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(5)
        port = listener.getsockname()[1]

        environment = os.environ.copy()
        environment["MINBOT_AUTH_PASSWORD"] = "test-secret"
        client = subprocess.Popen(
            [
                str(executable),
                "play",
                "127.0.0.1",
                "TestBot",
                str(port),
                "--auth",
                "auto",
                "--register-template",
                "/register {username} {password} {password}",
                "--auth-delay-ms",
                "0",
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=environment,
        )

        try:
            connection, _ = listener.accept()
            with connection:
                connection.settimeout(5)
                expect_packet(connection, 0x00)  # handshake
                login_start = expect_packet(connection, 0x00)
                if b"TestBot" not in login_start:
                    raise RuntimeError("login username is missing")

                compression_threshold = 16
                send_packet(connection, b"\x03" + encode_varint(compression_threshold))

                login_success = (
                    b"\x02"
                    + bytes.fromhex("12345678123456781234567812345678")
                    + protocol_string("TestBot")
                    + b"\x00"  # properties
                    + b"\x00"  # strict error handling
                )
                send_packet(connection, login_success, compression_threshold)
                expect_packet(connection, 0x03, compression_threshold)  # login acknowledged
                expect_packet(connection, 0x00, compression_threshold)  # client settings

                keep_alive_id = 0x0102030405060708
                send_packet(
                    connection,
                    b"\x04" + struct.pack(">q", keep_alive_id),
                    compression_threshold,
                )
                response = expect_packet(connection, 0x04, compression_threshold)
                if response[-8:] != struct.pack(">q", keep_alive_id):
                    raise RuntimeError("configuration keep-alive did not match")

                send_packet(connection, b"\x05" + struct.pack(">i", 42), compression_threshold)
                expect_packet(connection, 0x05, compression_threshold)

                send_packet(connection, b"\x0e\x00", compression_threshold)
                packs = expect_packet(connection, 0x07, compression_threshold)
                if packs != b"\x07\x00":
                    raise RuntimeError("expected an empty known-pack response")

                send_packet(connection, b"\x03", compression_threshold)
                expect_packet(connection, 0x03, compression_threshold)

                register = expect_packet(connection, 0x04, compression_threshold)
                login = expect_packet(connection, 0x04, compression_threshold)
                if b"register TestBot test-secret test-secret" not in register:
                    raise RuntimeError("custom register command was not expanded")
                if b"login test-secret" not in login:
                    raise RuntimeError("login command was not expanded")

                send_packet(
                    connection,
                    b"\x26" + struct.pack(">q", keep_alive_id),
                    compression_threshold,
                )
                send_packet(connection, b"\x27" + struct.pack(">ii", 5, -2), compression_threshold)
                player_chat = (
                    b"\x39"
                    + bytes(16)
                    + b"\x00"  # message index
                    + b"\x00"  # no signature
                    + protocol_string("hello from mock player")
                )
                send_packet(connection, player_chat, compression_threshold)
                position = b"\x40" + struct.pack(">dddffB", 1.0, 64.0, 2.0, 0.0, 0.0, 0) + encode_varint(17)
                send_packet(connection, position, compression_threshold)
                send_packet(connection, b"\x0c\x01", compression_threshold)

                if client.stdin is None:
                    raise RuntimeError("client stdin is unavailable")
                client.stdin.write("hello from bot\n")
                client.stdin.flush()

                received_ids: set[int] = set()
                chat_body = b""
                for _ in range(4):
                    body = receive_packet(connection, compression_threshold)
                    current_id = packet_id(body)
                    received_ids.add(current_id)
                    if current_id == 0x06:
                        chat_body = body

                if received_ids != {0x00, 0x06, 0x08, 0x18}:
                    raise RuntimeError(f"unexpected play responses: {sorted(received_ids)}")
                if b"hello from bot" not in chat_body:
                    raise RuntimeError("outgoing chat message is missing")

                time.sleep(0.2)
                client.stdin.write("/quit\n")
                client.stdin.flush()
                stdout, stderr = client.communicate(timeout=5)
        finally:
            if client.poll() is None:
                client.kill()
                client.wait(timeout=5)

    if client.returncode != 0:
        raise RuntimeError(f"minbot exited with {client.returncode}: {stderr.strip()}")
    if "test-secret" in stdout or "test-secret" in stderr:
        raise RuntimeError("authentication password leaked to process output")
    for expected in ("Joined the play state", "[chunk] loaded 5, -2", "hello from mock player"):
        if expected not in stdout:
            raise RuntimeError(f"missing output: {expected}")

    print("Local game integration test passed")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: game_integration.py <minbot-executable>")
    raise SystemExit(run_test(Path(sys.argv[1]).resolve()))
