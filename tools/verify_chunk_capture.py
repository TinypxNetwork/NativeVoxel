#!/usr/bin/env python3
"""Compare a captured protocol-47 S21 with the original Java Chunk.fillChunk.

Default input is build/server-play.nvpc. Extra arrays are intentionally consumed
according to the dimension, not inferred from remaining payload length.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
from verify_physics import make_reference, run


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, default=root / "build/server-play.nvpc")
    parser.add_argument("--mcp", type=Path, default=root.parent / "MCP-919-main")
    parser.add_argument("--dimension", type=int)
    parser.add_argument("--cxx", default="D:/Strawberry/c/bin/g++.exe" if os.name == "nt" else "g++")
    args = parser.parse_args()
    capture = args.capture.read_bytes()
    if capture[:8] != b"NVPC\x01\x00\x00\x00": raise RuntimeError("unrecognized packet capture header")
    position, dimension, payload = 8, args.dimension, None
    while position < len(capture):
        packet, size = struct.unpack_from("<II", capture, position); position += 8
        if size > len(capture) - position: raise RuntimeError("truncated packet capture")
        body = capture[position:position+size]; position += size
        if args.dimension is None:
            if packet == 1: dimension = struct.unpack_from("b", body, 5)[0]
            if packet == 7: dimension = struct.unpack_from(">i", body, 0)[0]
        if packet == 0x21 and len(body) >= 12 and body[9:11] != b"\x00\x00": payload = body; break
    if payload is None or dimension is None: raise RuntimeError("capture lacks dimension and nonempty S21")
    build = root / "build/chunk_reference"; build.mkdir(parents=True, exist_ok=True)
    mcp = args.mcp.resolve()
    make_reference(root, mcp, build)
    packet_path = build / "packet21.bin"; packet_path.write_bytes(payload)
    classpath = os.pathsep.join(str(p) for p in [build, root / "build/physics_reference/oracle_classes", mcp / "bin/minecraft",
        mcp / "jars/versions/1.8.9/1.8.9.jar", *sorted((mcp / "jars/libraries").rglob("*.jar"))])
    source = mcp / "src/minecraft/net/minecraft"
    run(["javac", "-encoding", "UTF-8", "--release", "8", "-cp", classpath, "-d", str(build),
         str(source / "world/chunk/Chunk.java"), str(source / "world/chunk/storage/ExtendedBlockStorage.java"),
         str(source / "network/play/server/S21PacketChunkData.java"), str(build / "PhysicsReference.java"),
         str(root / "tests/java/ChunkImportReference.java")], build)
    native = build / ("world_tests.exe" if os.name == "nt" else "world_tests")
    run([args.cxx, "-std=c++17", "-O2", "-ffp-contract=off", "-I", str(root / "include"), str(root / "src/world.cpp"),
         str(root / "tests/world_tests.cpp"), "-o", str(native)], root)
    java_out, native_out = build / "java.chunk", build / "native.chunk"
    run(["java", "-Djava.awt.headless=true", "-cp", classpath, "ChunkImportReference", str(dimension), str(packet_path), str(java_out)], build)
    run([str(native), "--chunk47", str(dimension), str(packet_path), str(native_out)], root)
    reference, actual = java_out.read_bytes(), native_out.read_bytes()
    cx, cz, full, mask = struct.unpack_from(">iiBH", payload)
    report = {"dimension": dimension, "chunk_x": cx, "chunk_z": cz, "ground_up": bool(full), "mask": mask,
        "packet_body_bytes": len(payload), "normalized_bytes": len(reference), "exact_match": reference == actual,
        "packet_sha256": hashlib.sha256(payload).hexdigest(), "normalized_sha256": hashlib.sha256(reference).hexdigest()}
    (build / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    if reference != actual:
        mismatch = next((i for i, (a, b) in enumerate(zip(reference, actual)) if a != b), min(len(reference), len(actual)))
        raise RuntimeError(f"Java/native normalized S21 differs at byte {mismatch}")
    print("PASS: captured S21 matches original Java Chunk.fillChunk exactly: " + json.dumps(report))


if __name__ == "__main__":
    try: main()
    except Exception as error:
        print(f"Chunk capture verification failed: {error}", file=sys.stderr)
        sys.exit(1)
