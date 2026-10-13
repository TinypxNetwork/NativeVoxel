#!/usr/bin/env python3
"""Open a native-generated Anvil save using the original Java 1.8.9 loader."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import time
from verify_physics import make_reference, run


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcp", type=Path, default=root.parent / "MCP-919-main")
    parser.add_argument("--cxx", default="D:/Strawberry/c/bin/g++.exe" if os.name == "nt" else "g++")
    parser.add_argument("--native", type=Path, help="Use an already compiled world_io_tests executable")
    args = parser.parse_args()
    mcp = args.mcp.resolve()
    build = root / "build/world_io_reference"; build.mkdir(parents=True, exist_ok=True)
    exported = build / ("native_export_" + str(time.time_ns()))
    native = args.native.resolve() if args.native else build / ("world_io_tests.exe" if os.name == "nt" else "world_io_tests")
    if not args.native:
        run([args.cxx, "-std=c++17", "-O2", "-ffp-contract=off", "-I", str(root / "include"),
             str(root / "src/world.cpp"), str(root / "src/world_io.cpp"), str(root / "src/protocol.cpp"),
             str(root / "tests/world_io_tests.cpp"), "-lstdc++fs", "-lws2_32", "-lwinhttp", "-ldnsapi", "-lcrypto", "-lz",
             "-static-libgcc", "-static-libstdc++", "-o", str(native)], root)
    run([str(native)], root)
    run([str(native), "--export-preview", str(exported)], root)
    before = {str(p.relative_to(exported)): hashlib.sha256(p.read_bytes()).hexdigest() for p in exported.rglob("*") if p.is_file()}
    make_reference(root, mcp, build)
    classpath = os.pathsep.join(str(p) for p in [build, root / "build/physics_reference/oracle_classes", mcp / "bin/minecraft",
        mcp / "jars/versions/1.8.9/1.8.9.jar", *sorted((mcp / "jars/libraries").rglob("*.jar"))])
    source = mcp / "src/minecraft/net/minecraft"
    run(["javac", "-encoding", "UTF-8", "--release", "8", "-cp", classpath, "-d", str(build),
         str(source / "world/chunk/storage/AnvilChunkLoader.java"), str(source / "world/chunk/storage/RegionFile.java"),
         str(source / "world/chunk/storage/RegionFileCache.java"), str(source / "nbt/CompressedStreamTools.java"),
         str(build / "PhysicsReference.java"), str(root / "tests/java/WorldIOReference.java")], build)
    report_path = build / "report.json"
    run(["java", "-Djava.awt.headless=true", "-cp", classpath, "WorldIOReference", str(exported), str(report_path)], build)
    after = {str(p.relative_to(exported)): hashlib.sha256(p.read_bytes()).hexdigest() for p in exported.rglob("*") if p.is_file()}
    if before != after: raise RuntimeError("Java read changed exported save file bytes")
    report = json.loads(report_path.read_text(encoding="utf-8"))
    report["save_directory"] = str(exported)
    report["save_file_bytes_unchanged"] = True
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("PASS: native Anvil save opened by original Java loader: " + json.dumps(report))
    tile_export=build/('native_tiles_'+str(time.time_ns()));run([str(native),'--export-tile-fixture',str(tile_export)],root)
    before={str(p.relative_to(tile_export)):hashlib.sha256(p.read_bytes()).hexdigest() for p in tile_export.rglob('*') if p.is_file()}
    tile_report=build/'tiles_report.json';run(['java','-Djava.awt.headless=true','-cp',classpath,'WorldIOReference',str(tile_export),str(tile_report),'--tiles'],build)
    after={str(p.relative_to(tile_export)):hashlib.sha256(p.read_bytes()).hexdigest() for p in tile_export.rglob('*') if p.is_file()}
    if before!=after:raise RuntimeError('Java tile read changed exported save bytes')
    result=json.loads(tile_report.read_text());result['save_directory']=str(tile_export);result['save_file_bytes_unchanged']=True;tile_report.write_text(json.dumps(result,indent=2))
    print('PASS: generated/deleted tiles, enchanted-book NBT and native generator metadata opened by Java: '+json.dumps(result))


if __name__ == "__main__":
    try: main()
    except Exception as error:
        print(f"World IO Java verification failed: {error}", file=sys.stderr)
        sys.exit(1)
