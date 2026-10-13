#!/usr/bin/env python3
"""Loopback-only independent protocol-47 socket fixtures.

The executable must be compiled with MC_PROTOCOL_TESTING. The test-only session
hook replaces HTTPS authorization; RSA, SHA-1, AES-CFB8, zlib and TCP handling are
the production implementation. No real access token or Minecraft server is used.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import threading
import time
import zlib


def varint(value: int) -> bytes:
    value &= 0xFFFFFFFF
    result = bytearray()
    while True:
        byte = value & 127
        value >>= 7
        result.append(byte | (128 if value else 0))
        if not value:
            return bytes(result)


def read_varint(data: bytes, position: int = 0) -> tuple[int, int]:
    value = 0
    for index in range(5):
        byte = data[position]
        position += 1
        value |= (byte & 127) << (7 * index)
        if not byte & 128:
            return value - (1 << 32) if value & (1 << 31) else value, position
    raise AssertionError("oversize VarInt from native client")


def string(value: str) -> bytes:
    encoded = value.encode("utf-8")
    return varint(len(encoded)) + encoded


def read_string(data: bytes, position: int = 0) -> tuple[str, int]:
    length, position = read_varint(data, position)
    return data[position:position + length].decode("utf-8"), position + length


def byte_array(data: bytes) -> bytes:
    return varint(len(data)) + data


class Wire:
    def __init__(self, connection: socket.socket):
        self.connection = connection
        self.threshold = -1
        self.encryptor = None
        self.decryptor = None

    def enable_cipher(self, secret: bytes) -> None:
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms
        try:
            from cryptography.hazmat.decrepit.ciphers.modes import CFB8
        except ImportError:
            from cryptography.hazmat.primitives.ciphers.modes import CFB8
        self.encryptor = Cipher(algorithms.AES(secret), CFB8(secret)).encryptor()
        self.decryptor = Cipher(algorithms.AES(secret), CFB8(secret)).decryptor()

    def exact(self, size: int) -> bytes:
        result = bytearray()
        while len(result) < size:
            data = self.connection.recv(size - len(result))
            if not data:
                raise EOFError("native client disconnected")
            result.extend(self.decryptor.update(data) if self.decryptor else data)
        return bytes(result)

    def read(self) -> tuple[int, bytes]:
        length = 0
        for i in range(3):
            byte = self.exact(1)[0]
            length |= (byte & 127) << (7 * i)
            if not byte & 128:
                break
        else:
            raise AssertionError("invalid outer length")
        data = self.exact(length)
        if self.threshold >= 0:
            plain_length, position = read_varint(data)
            data = data[position:]
            if plain_length:
                assert plain_length >= self.threshold
                decoder = zlib.decompressobj()
                data = decoder.decompress(data) + decoder.flush()
                assert decoder.eof and not decoder.unused_data
                assert len(data) == plain_length
        packet_id, position = read_varint(data)
        return packet_id, data[position:]

    def frame(self, packet_id: int, data: bytes, threshold: int | None = None) -> bytes:
        threshold = self.threshold if threshold is None else threshold
        data = varint(packet_id) + data
        if threshold >= 0:
            data = (varint(len(data)) + zlib.compress(data)) if len(data) >= threshold else b"\0" + data
        return varint(len(data)) + data

    def send_bytes(self, data: bytes, fragmented: bool = True) -> None:
        if self.encryptor:
            data = self.encryptor.update(data)
        if not fragmented:
            self.connection.sendall(data)
            return
        steps = (1, 2, 1, 7, 3, 31, 127, 5, 1024)
        index = 0
        position = 0
        while position < len(data):
            count = steps[index % len(steps)]
            self.connection.sendall(data[position:position + count])
            position += count
            index += 1
            if index < 8:
                time.sleep(0.001)

    def send(self, packet_id: int, data: bytes) -> None:
        self.send_bytes(self.frame(packet_id, data))


def handshake(wire: Wire, expected_state: int, port: int) -> None:
    packet_id, data = wire.read()
    assert packet_id == 0
    version, position = read_varint(data)
    host, position = read_string(data, position)
    server_port, = struct.unpack_from(">H", data, position)
    state, position = read_varint(data, position + 2)
    assert (version, host, server_port, state, position) == (47, "127.0.0.1", port, expected_state, len(data))


def fixture_server(connection: socket.socket, port: int, mode: str, threshold: int, facts: dict) -> None:
    wire = Wire(connection)
    handshake(wire, 2, port)
    packet_id, body = wire.read()
    assert packet_id == 0 and read_string(body) == ("NativeTest", len(body))

    if mode in ("encrypted", "auth-reject", "no-auth"):
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric import padding, rsa
        private_key = rsa.generate_private_key(public_exponent=65537, key_size=1024)
        public_key = private_key.public_key().public_bytes(serialization.Encoding.DER,
                                                        serialization.PublicFormat.SubjectPublicKeyInfo)
        token = b"\x18\x09\x47\xff"
        server_id = "native-test"
        wire.send(1, string(server_id) + byte_array(public_key) + byte_array(token))
        if mode != "encrypted":
            assert connection.recv(1) == b"", "rejected authentication must close without encryption response"
            return
        packet_id, body = wire.read()
        assert packet_id == 1
        secret_length, position = read_varint(body)
        encrypted_secret = body[position:position + secret_length]
        token_length, position = read_varint(body, position + secret_length)
        encrypted_token = body[position:position + token_length]
        assert position + token_length == len(body)
        secret = private_key.decrypt(encrypted_secret, padding.PKCS1v15())
        verify = private_key.decrypt(encrypted_token, padding.PKCS1v15())
        assert len(secret) == 16 and verify == token
        digest = hashlib.sha1(server_id.encode("latin-1") + secret + public_key).digest()
        facts["hash"] = format(int.from_bytes(digest, "big", signed=True), "x")
        wire.enable_cipher(secret)

    if mode == "malformed":
        wire.send(3, varint(16))
        wire.threshold = 16
        bad = varint(100) + b"not-a-zlib-stream"
        wire.send_bytes(varint(len(bad)) + bad)
        assert connection.recv(1) == b""
        return

    login_success = string("12345678-1234-1234-1234-1234567890ab") + string("NativeTest")
    keepalive_token = -1234567
    batch = b""
    if threshold >= 0:
        batch += wire.frame(3, varint(threshold))
        wire.threshold = threshold
    batch += wire.frame(2, login_success)
    batch += wire.frame(0, varint(keepalive_token))
    # Valid Join Game plus another opaque packet tests login->play coalescing.
    batch += wire.frame(1, struct.pack(">iBbBB", 99, 0, 0, 2, 20) + string("default") + b"\0")
    batch += wire.frame(0x41, b"\x01")
    wire.send_bytes(batch)

    got_keepalive = False
    roundtrip = None
    for _ in range(4):
        packet_id, body = wire.read()
        if packet_id == 0:
            assert read_varint(body) == (keepalive_token, len(body))
            got_keepalive = True
        elif packet_id == 0x17:
            name, position = read_string(body)
            assert name == "TEST|ROUNDTRIP"
            assert body[position:] == bytes((i * 37 + 11) & 255 for i in range(4096))
            roundtrip = body
        else:
            raise AssertionError(f"unexpected client packet {packet_id}")
        if got_keepalive and roundtrip is not None:
            break
    assert got_keepalive and roundtrip is not None
    compression_change = b""
    if threshold == 64:
        compression_change = wire.frame(0x46, varint(0))
        wire.threshold = 0
    wire.send_bytes(compression_change + wire.frame(0x3F, roundtrip) + wire.frame(0, varint(2147483647)))
    packet_id, body = wire.read()
    assert packet_id == 0 and read_varint(body) == (2147483647, len(body))
    wire.send(0x40, string('{"text":"test complete"}'))


def status_server(connection: socket.socket, port: int, wrong_pong: bool, facts: dict) -> None:
    wire = Wire(connection)
    handshake(wire, 1, port)
    assert wire.read() == (0, b"")
    status = {"version": {"name": "Independent 1.8.9 fixture", "protocol": 47},
              "players": {"online": 2, "max": 10}, "description": {"text": "Native test \u4e2d\u6587"}}
    facts["json"] = json.dumps(status, ensure_ascii=False, separators=(",", ":"))
    wire.send(0, string(facts["json"]))
    packet_id, body = wire.read()
    assert packet_id == 1 and len(body) == 8
    if wrong_pong:
        value, = struct.unpack(">q", body)
        body = struct.pack(">q", value + 1)
    wire.send(1, body)


def run_case(executable: Path, mode: str, threshold: int = 64) -> None:
    failures: list[BaseException] = []
    facts: dict = {}
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(15)
        port = listener.getsockname()[1]

        def serve() -> None:
            try:
                connection, _ = listener.accept()
                with connection:
                    connection.settimeout(10)
                    connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    if mode.startswith("status"):
                        status_server(connection, port, mode == "status-wrong-pong", facts)
                    else:
                        fixture_server(connection, port, mode, threshold, facts)
            except BaseException as error:
                failures.append(error)

        server = threading.Thread(target=serve, daemon=True)
        server.start()
        environment = os.environ.copy()
        environment["PATH"] = r"D:\Strawberry\c\bin" + os.pathsep + environment.get("PATH", "")
        arguments = [str(executable), "--status", "127.0.0.1", str(port)] if mode.startswith("status") else [
            str(executable), "--client", "127.0.0.1", str(port), mode]
        process = subprocess.run(arguments, capture_output=True, text=True, encoding="utf-8",
                                 errors="replace", timeout=20, env=environment)
        server.join(timeout=12)
        assert not server.is_alive(), f"fixture server did not finish: {mode}"
        if failures:
            raise AssertionError(f"server fixture {mode}/{threshold}: {failures[0]!r}\n{process.stdout}\n{process.stderr}")
        expected_code = 1 if mode == "status-wrong-pong" else 0
        assert process.returncode == expected_code, f"{mode}/{threshold}: {process.returncode}\n{process.stdout}\n{process.stderr}"
        assert "unit-test-token-not-a-credential" not in process.stdout + process.stderr, "access token leaked"
        if mode == "encrypted":
            assert "TEST_JOIN_HASH=" + facts["hash"] in process.stdout, "signed Minecraft SHA-1 mismatch"
            assert "PLAY encrypted=1" in process.stdout
        if mode in ("offline", "encrypted"):
            assert "ROUNDTRIP_OK" in process.stdout
        if mode == "status":
            assert "STATUS_JSON=" + facts["json"] in process.stdout
        print(f"PASS {mode} threshold={threshold}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, default=Path(__file__).with_name("protocol_tests.exe"))
    args = parser.parse_args()
    executable = args.executable.resolve()
    for threshold in (-1, 0, 64, 1000000):
        run_case(executable, "offline", threshold)
    for threshold in (0, 64):
        run_case(executable, "encrypted", threshold)
    for mode in ("no-auth", "auth-reject", "malformed", "status", "status-wrong-pong"):
        run_case(executable, mode)
    print("11 protocol integration scenarios passed; all traffic stayed on loopback.")


if __name__ == "__main__":
    main()
