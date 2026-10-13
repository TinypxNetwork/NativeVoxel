#!/usr/bin/env python3
"""Differentially test C++ movement against the supplied, compiled MCP classes.

Java is a development oracle only. The native client has no JVM dependency.
This runner never edits the reference MCP tree. Run from any directory:
    python tools/verify_physics.py --mcp ../MCP-919-main
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys


def java_block(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def make_reference(root: Path, mcp: Path, build: Path) -> None:
    source = (mcp / "src/minecraft/net/minecraft/client/entity/EntityPlayerSP.java").read_text(encoding="utf-8")
    template = (root / "tests/java/PhysicsReference.java.in").read_text(encoding="utf-8")
    living = java_block(source, "public void onLivingUpdate()")
    timers = living[living.index("if (this.sprintingTicksLeft > 0)"):living.index("this.prevTimeInPortal")]
    movement = living[living.index("boolean flag = this.movementInput.jump;"):living.index("if (this.isRidingHorse())")]
    reset = java_block(living, "if (this.onGround && this.capabilities.isFlying")
    replacements = {
        "@SP_TIMERS@": timers,
        "@SP_MOVEMENT@": movement,
        "@SP_FLIGHT_RESET@": reset,
        "@PUSH_OUT@": java_block(source, "protected boolean pushOutOfBlocks("),
        "@OPEN_SPACE@": java_block(source, "private boolean isOpenBlockSpace("),
    }
    for key, value in replacements.items():
        template = template.replace(key, value)
    (build / "PhysicsReference.java").write_text(template, encoding="utf-8")


def make_fixtures(path: Path) -> dict[str, int]:
    lines: list[str] = []
    labels: list[str] = []
    current = "setup"
    counts = {"sine_samples": 0, "shape_samples": 0, "movement_ticks": 0}

    def command(s: str) -> None:
        lines.append(s)
        if s[0] in "SGTM":
            labels.append(current)
            counts[{"S": "sine_samples", "G": "shape_samples", "T": "movement_ticks", "M": "movement_ticks"}[s[0]]] += 1

    def block(x: int, y: int, z: int, id: int, meta: int = 0) -> None:
        command(f"B {x} {y} {z} {(id << 4) | meta}")

    def tick(n: int = 1, forward: int = 0, strafe: int = 0, jump: int = 0, sneak: int = 0, sprint: int = 0, item: int = 0) -> None:
        for _ in range(n):
            command(f"T {forward} {strafe} {jump} {sneak} {sprint} {item}")

    def scene(id: int = 1, x: float = .5, y: float = 64, z: float = .5, yaw: float = 0, extent: int = 32) -> None:
        command("W")
        for bx in range(-extent, extent + 1):
            for bz in range(-extent, extent + 1):
                block(bx, 63, bz, id)
        command(f"P {x} {y} {z} {yaw} 0 1")

    current = "MathHelper sine/cosine lookup"
    for i in range(65536):
        command(f"S {i * math.pi * 2.0 / 65536.0:.17g}")
    for angle in [-100000.5, -1000, -math.pi, -.0001, 0, 1000, 100000.5]:
        command(f"S {angle:.17g}")

    # AABB samples use vanilla registered block classes, including block families
    # whose appearance differs from their actual collision bounds (e.g. farmland).
    for id in range(198):
        current = f"block {id} metadata 0"
        command("W")
        block(0, 64, 0, id)
        command("G 0 64 0")
    metadata = {
        44: range(16), 126: [0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13], 182: [0, 8],
        53: range(8), 64: range(16), 71: range(16), 96: range(16), 167: range(16),
        107: range(16), 65: range(2, 6), 78: range(8), 92: range(7), 120: range(8),
        127: range(12), 144: range(1, 6), 145: range(12),
        29: [0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13],
        34: [0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13],
    }
    for id, metas in metadata.items():
        for meta in metas:
            current = f"block {id} metadata {meta}"
            command("W"); block(0, 64, 0, id, meta); command("G 0 64 0")
    for id in [53, 67, 108, 109, 114, 128, 134, 135, 136, 156, 163, 164, 180]:
        for meta in range(8):
            for other in range(8):
                current = f"stair {id}:{meta} against {other}"
                command("W"); block(0, 64, 0, id, meta)
                block(1, 64, 0, 67, other); block(0, 64, -1, 53, other)
                command("G 0 64 0")
    for id in [85, 113, 139, 101, 102, 160]:
        for neighbor in [1, 20, 18, 85, 113, 107, 152, 165, 166, 174, 46, 103]:
            for direction, (dx, dz) in enumerate([(0, -1), (1, 0), (0, 1), (-1, 0)]):
                current = f"connected block {id} neighbor {neighbor} direction {direction}"
                command("W"); block(0, 64, 0, id); block(dx, 64, dz, neighbor); command("G 0 64 0")

    current = "walking then friction stop"; scene(); tick(80, forward=1); tick(20)
    current = "sprint toggle, sprint jump, air factor"; scene(yaw=37.5); tick(20, forward=1, sprint=1); tick(40, forward=1, sprint=1, jump=1); tick(20)
    current = "diagonal normalization at non-cardinal yaw"; scene(yaw=-135.73); tick(80, forward=1, strafe=1); tick(30, forward=-1, strafe=-1)
    current = "forward double tap sprint"; scene(); tick(2, forward=1); tick(2); tick(40, forward=1); tick(10)
    current = "repeated held jumping"; scene(); tick(60, jump=1); tick(20)
    current = "jump boost level 3"; scene(); command("J 2"); tick(1, jump=1); tick(60)
    current = "item use and sneaking slowdown"; scene(); tick(30, forward=1, item=1); tick(30, forward=1, strafe=1, sneak=1); tick(20)
    current = "low food prevents sprint"; scene(); command("A 0.10000000149011612 6 0 0 0.05 0"); tick(60, forward=1, sprint=1)
    current = "server modified movement attribute"; scene(yaw=91.2); command("A 0.16375 20 0 0 0.05 0"); tick(40, forward=1, sprint=1); tick(20)
    current = "ice ground friction"; scene(id=79); tick(30, forward=1); tick(60)
    current = "slime bounce and landing drag"; scene(id=165, y=69); command("V 0.11 -0.3 0.07"); tick(100)
    current = "soul sand intersection slowdown"; scene(id=88, y=63.875); tick(70, forward=1)
    current = "half slab and two-path stepping"; scene()
    for bx in range(-2, 3):
        block(bx, 64, 2, 44); block(bx, 64, 3, 1); block(bx, 65, 4, 44); block(bx, 64, 4, 1)
    tick(45, forward=1)
    current = "stair automatic ascending and descending"; scene()
    for bx in range(-2, 3):
        for bz in range(2, 8):
            for by in range(64, 64 + bz - 2): block(bx, by, bz, 1)
            block(bx, 64 + bz - 2, bz, 53, 2)
    tick(40, forward=1); tick(40, forward=-1)
    current = "head obstruction during step"; scene()
    for bx in range(-2, 3):
        block(bx, 64, 2, 44)
        for bz in range(2, 5): block(bx, 66, bz, 44, 8)
    tick(30, forward=1, sprint=1, jump=1)
    current = "fence horizontal collision"; scene()
    for bx in range(-2, 3): block(bx, 64, 2, 85)
    tick(30, forward=1, sprint=1)
    current = "sneak ledge guard"; command("W"); block(0, 63, 0, 1); command("P 0.5 64 0.5 0 0 1"); tick(70, forward=1, sneak=1)
    current = "ladder climb and sneak hold"; scene()
    for by in range(64, 80): block(0, by, 2, 1); block(0, by, 1, 65, 2)
    command("P 0.5 64 1.5 0 0 1"); tick(50, forward=1); tick(25, sneak=1); tick(30)
    current = "web movement, next-tick flag"; scene()
    for by in range(64, 69):
        for bz in range(1, 6): block(0, by, bz, 30)
    tick(100, forward=1, jump=1)
    current = "water buoyancy and drag"; scene()
    for bx in range(-3, 4):
        for by in range(64, 67):
            for bz in range(-3, 22): block(bx, by, bz, 9)
    command("P 0.5 64.5 0.5 0 0 0"); tick(50, forward=1, jump=1); tick(30)
    current = "flowing water acceleration"; scene()
    for bx in range(-3, 4):
        for bz in range(-3, 22): block(bx, 64, bz, 8, (bz + 3) % 8)
    command("P 0.5 64 0.5 0 0 0"); tick(60, forward=1); tick(30)
    current = "lava drag"; scene()
    for bx in range(-3, 4):
        for by in range(64, 67):
            for bz in range(-3, 22): block(bx, by, bz, 11)
    command("P 0.5 64.5 0.5 0 0 0"); tick(40, forward=1, jump=1); tick(30)
    current = "creative flight"; scene(y=75, yaw=-33.25); command("A 0.10000000149011612 20 1 1 0.05 0")
    tick(40, forward=1, jump=1, sprint=1); tick(20, sneak=1); tick(20)
    current = "double-jump flight toggles"; scene(); command("A 0.10000000149011612 20 1 0 0.05 0")
    tick(1, jump=1); tick(2); tick(1, jump=1); tick(10, forward=1); tick(1, jump=1); tick(2); tick(1, jump=1); tick(40)
    current = "world border virtual stone collision"; scene(); command("O 0 0 10 29999984")
    tick(50, forward=1, sprint=1); tick(20, forward=1, jump=1)
    current = "outside world border hysteresis and reentry"; scene(z=8); command("O 0 0 10 29999984")
    tick(30, forward=-1); tick(30, forward=1)
    current = "world border fractional block edges"; scene(yaw=-90); command("O 0.75 0.25 7.5 29999984")
    tick(50, forward=1); tick(30, forward=-1)
    current = "world border above build height"; scene(y=270); command("O 0 0 10 29999984")
    command("A 0.10000000149011612 20 1 1 0.05 0"); tick(60, forward=1)
    current = "missing chunk skip"; command("W"); command("P 0.5 70 0.5 0 0 0"); tick(3, forward=1, jump=1)
    path.write_text("\n".join(lines) + "\n", encoding="ascii")
    path.with_suffix(".labels.json").write_text(json.dumps(labels), encoding="utf-8")
    return counts


def run(argv: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(argv, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        print(result.stdout, file=sys.stderr)
        raise RuntimeError(f"command failed ({result.returncode}): {argv[0]}")
    return result


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcp", type=Path, default=root.parent / "MCP-919-main")
    parser.add_argument("--cxx", default="D:/Strawberry/c/bin/g++.exe" if os.name == "nt" else "g++")
    parser.add_argument("--native", type=Path, help="use an existing world_tests executable")
    args = parser.parse_args()
    mcp = args.mcp.resolve()
    build = root / "build/physics_reference"
    build.mkdir(parents=True, exist_ok=True)
    classes = mcp / "bin/minecraft"
    if not (classes / "net/minecraft/entity/Entity.class").is_file():
        raise RuntimeError("MCP bin/minecraft compiled classes are required as the Java oracle")
    for tool in ("java", "javac"):
        if not shutil.which(tool):
            raise RuntimeError(f"{tool} is required only for this development verification")
    make_reference(root, mcp, build)
    fixture = build / "movement.fixture"
    counts = make_fixtures(fixture)
    oracle_classes = build / "oracle_classes"
    oracle_classes.mkdir(exist_ok=True)
    classpath = os.pathsep.join(str(p) for p in [build, oracle_classes, classes, mcp / "jars/versions/1.8.9/1.8.9.jar",
                                               *sorted((mcp / "jars/libraries").rglob("*.jar"))])
    source_root = mcp / "src/minecraft/net/minecraft"
    oracle_sources = [source_root / name for name in ["entity/Entity.java", "entity/EntityLivingBase.java",
        "entity/player/EntityPlayer.java", "util/AxisAlignedBB.java", "util/MathHelper.java",
        "world/World.java", "world/border/WorldBorder.java"]]
    oracle_sources.extend(sorted((source_root / "block").rglob("*.java")))
    print("Compiling original Java movement, world and block sources for the oracle...", flush=True)
    # Recompile the methods under comparison; prebuilt MCP classes supply only
    # the remaining game dependencies. No Java source under MCP is changed.
    run(["javac", "-encoding", "UTF-8", "--release", "8", "-cp", classpath, "-d", str(oracle_classes),
         *map(str, oracle_sources)], build)
    print("Compiling the headless Java adapter...", flush=True)
    run(["javac", "-cp", classpath, "-d", str(build), str(build / "PhysicsReference.java")], build)
    native = args.native.resolve() if args.native else build / ("world_tests.exe" if os.name == "nt" else "world_tests")
    if not args.native:
        print("Compiling native physics with strict floating-point operations...", flush=True)
        run([args.cxx, "-std=c++17", "-O2", "-ffp-contract=off", "-I", str(root / "include"),
             str(root / "src/world.cpp"), str(root / "tests/world_tests.cpp"), "-o", str(native)], root)
    print(f"Running {counts}...", flush=True)
    run(["java", "-Djava.awt.headless=true", "-cp", classpath, "PhysicsReference", str(fixture), str(build / "java.trace")], build)
    native_result = run([str(native), "--trace", str(fixture)], root)
    (build / "native.trace").write_text(native_result.stdout, encoding="ascii")
    reference_lines = (build / "java.trace").read_text(encoding="ascii").splitlines()
    native_lines = native_result.stdout.splitlines()
    labels = json.loads(fixture.with_suffix(".labels.json").read_text(encoding="utf-8"))
    if len(reference_lines) != len(native_lines):
        raise RuntimeError(f"trace length differs: Java {len(reference_lines)}, C++ {len(native_lines)}")
    failures = []
    max_difference = 0.0
    for line_no, (reference, actual) in enumerate(zip(reference_lines, native_lines)):
        a, b = reference.split(), actual.split()
        if len(a) != len(b) or a[0] != b[0]:
            failures.append({"row": line_no, "scenario": labels[line_no], "java": reference, "native": actual})
            continue
        delta = max((abs(float(x) - float(y)) for x, y in zip(a[1:], b[1:])), default=0)
        max_difference = max(max_difference, delta)
        # Decimal output may round differently, but the parsed 17-digit values
        # retain all native/Java double bits. Require exact double equivalence.
        if delta != 0:
            failures.append({"row": line_no, "scenario": labels[line_no], "max_abs_delta": delta, "java": reference, "native": actual})
    source_hashes = {}
    for name in ["Entity", "EntityLivingBase", "player/EntityPlayer"]:
        source_path = mcp / f"src/minecraft/net/minecraft/entity/{name}.java"
        source_hashes[str(source_path.relative_to(mcp))] = hashlib.sha256(source_path.read_bytes()).hexdigest()
    report = {"oracle": "original MCP movement/world/block sources recompiled for the fixture + extracted EntityPlayerSP input prelude",
              "counts": counts, "rows": len(reference_lines), "mismatches": len(failures),
              "maximum_absolute_difference": max_difference, "source_sha256": source_hashes,
              "failures": failures[:100]}
    (build / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    if failures:
        for failure in failures[:12]: print(json.dumps(failure), file=sys.stderr)
        raise RuntimeError(f"{len(failures)} differential mismatches; see {build / 'report.json'}")
    print(f"PASS: {len(reference_lines)} Java/C++ rows match exactly; {counts}. Report: {build / 'report.json'}")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"Physics verification failed: {error}", file=sys.stderr)
        sys.exit(1)
