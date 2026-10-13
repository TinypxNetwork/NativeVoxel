#!/usr/bin/env python3
"""Compare dropped item/XP motion and inventory behavior with MCP Java."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def run(arguments: list[str], cwd: Path) -> str:
    env = os.environ.copy()
    env["PATH"] = r"D:\Strawberry\c\bin" + os.pathsep + env.get("PATH", "")
    result = subprocess.run(arguments, cwd=cwd, env=env, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcp", type=Path, default=root.parent / "MCP-919-main")
    parser.add_argument("--native", type=Path, default=root / "tests/drop_entity_tests.exe")
    args = parser.parse_args()
    mcp = args.mcp.resolve()
    build = root / "build/drop_entity_reference"
    classes = build / "classes"
    classes.mkdir(parents=True, exist_ok=True)
    classpath = os.pathsep.join(map(str, [classes, root / "build/drops_reference/classes", root / "build/drops_export/classes", root / "build/physics_reference/oracle_classes", root / "build/crafting_export/classes", root / "build/registry_export/classes", mcp / "bin/minecraft", mcp / "jars/versions/1.8.9/1.8.9.jar", *sorted((mcp / "jars/libraries").rglob("*.jar"))]))
    source = mcp / "src/minecraft/net/minecraft"
    originals = [source / path for path in ("entity/Entity.java", "entity/item/EntityItem.java", "entity/item/EntityXPOrb.java", "entity/player/EntityPlayer.java", "entity/player/InventoryPlayer.java", "world/World.java", "util/Vec3.java", "util/AxisAlignedBB.java")]
    run(["javac", "-encoding", "UTF-8", "--release", "8", "-cp", classpath, "-d", str(classes), *map(str, originals), str(root / "tests/java/DropEntityReference.java")], build)
    lines: list[str] = []
    labels: list[str] = []
    counts = {"entity_ticks": 0, "inventory_cases": 0, "experience_cases": 0, "merge_cases": 0, "throw_cases": 0}

    def blocks(block: int, y: int, radius: int = 2) -> None:
        for x in range(-radius, radius + 1):
            for z in range(-radius, radius + 1):
                lines.append(f"B {x} {y} {z} {block}")

    def entity_case(label: str, orb: int, floor: int | None = 16, extras: list[str] | None = None, *, position: str = ".5 65 .5", motion: str = ".075 -.1 -.031", age: int = 0, delay: int = 10, first: int = 1, fire: int = 0, ticks: int = 100, identity: int = 57) -> None:
        lines.append("W")
        if floor is not None:
            blocks(floor, 63, 3)
        lines.extend(extras or [])
        lines.append(f"S {orb} {identity} {position} {motion} 189 {age} {delay} 5 {fire} {first} 0 0 0")
        lines.append(f"T {ticks}")
        labels.extend([label] * ticks)
        counts["entity_ticks"] += ticks

    for orb in (0, 1):
        for floor in (16, 79 * 16, 165 * 16, 88 * 16, 44 * 16, 53 * 16, 85 * 16):
            entity_case(f"{'XP' if orb else 'item'} floor {floor}", orb, floor)
        for shape in (44 * 16 + 8, 53 * 16 + 4, 85 * 16, 139 * 16, 154 * 16, 118 * 16, 33 * 16 + 8, 117 * 16):
            entity_case(f"compound shape {shape} orb={orb}", orb, extras=[f"B 0 64 0 {shape}"], position=".5 64.5 .5", motion="0 0 0", ticks=12)
            entity_case(f"push past compound {shape} orb={orb}", orb, extras=["B 0 64 0 16", f"B -1 64 0 {shape}"], position=".1 64.5 .5", motion="0 0 0", ticks=12)
        entity_case(f"void orb={orb}", orb, None, position="-.05 0 -.05", ticks=130)
        entity_case(f"push-out orb={orb}", orb, extras=["B 0 64 0 16"], position=".5 64 .5", motion="0 0 0")
        entity_case(f"web orb={orb}", orb, extras=["B 0 64 0 480"], position=".5 64.5 .5")
        entity_case(f"cactus orb={orb}", orb, extras=["B 0 64 0 1296"], position=".09 64 .5", motion="0 0 0", ticks=10)
        for liquid in (8 * 16, 8 * 16 + 3, 8 * 16 + 8, 10 * 16):
            extras = [f"B {x} 64 {z} {liquid}" for x in range(-1, 2) for z in range(-1, 2)]
            if liquid == 8 * 16 + 8:
                extras += ["B 1 64 0 16", "B 1 65 0 16"]
            entity_case(f"liquid {liquid} orb={orb}", orb, extras=extras, position=".5 64.5 .5", ticks=50 if liquid != 160 else 5)
        entity_case(f"fire orb={orb}", orb, extras=["B 0 64 0 816"], position=".5 64 .5", motion="0 0 0", ticks=8)
        entity_case(f"rain extinguishes orb={orb}", orb, extras=["R 1"], fire=3, ticks=10)
        entity_case(f"despawn orb={orb}", orb, age=5998, ticks=3)
    entity_case("infinite item countdown", 0, age=-32768, delay=32767, ticks=30)
    for identity in (1, 57, 99):
        for spectating in (0, 1):
            entity_case(f"XP attraction id={identity} spectator={spectating}", 1, extras=[f"P 1 2.5 64 .5 {spectating}"], identity=identity)
    for fill in (-1, 1):
        for item in (4, 35, 257, 325, 373):
            for count in (1, 2, 16, 64, 127):
                for damage in (0, 1):
                    for creative in (0, 1):
                        for slots in ("0", f"1 0 {item} 63 0", "1 8 -1 0 0", f"2 0 {item} 62 0 9 -1 0 0"):
                            line = f"I {fill} {item} {count} {damage} {creative} {slots}"
                            lines.append(line)
                            labels.append(line)
                            counts["inventory_cases"] += 1
    for level in (*range(0, 61), 100, 1000, 10000):
        for progress in (0, .5, .999):
            for amount in (0, 1, 3, 7, 100, 2477):
                line = f"X {level} 0 {progress} {amount}"
                lines.append(line)
                labels.append(line)
                counts["experience_cases"] += 1
    for first_id, second_id in ((4, 4), (4, 1), (35, 35), (257, 257)):
        for counts_pair in ((1, 2), (5, 2), (1, 63), (2, 63)):
            for tags in ((0, 0), (0, 1), (1, 1), (2, 2), (2, 3), (2, 4)):
                for variant in range(4):
                    line = f"M {first_id} {counts_pair[0]} 0 {(-32768 if variant == 3 else 100)} 3 {tags[0]} {second_id} {counts_pair[1]} {int(variant == 1)} 10 {(32767 if variant == 2 else 8)} {tags[1]}"
                    lines.append(line)
                    labels.append(line)
                    counts["merge_cases"] += 1
    for around in (0, 1):
        for sneaking in (0, 1):
            for yaw in (0, 45, 90, 179.9, 270, 360, -91, -360, 1080):
                for pitch in (-90, -45, 0, 45, 90):
                    for seed in range(64):
                        line = f"V {around} {seed % 2} {sneaking} {yaw} {pitch} {seed}"
                        lines.append(line)
                        labels.append(line)
                        counts["throw_cases"] += 1
    fixture = build / "entities.fixture"
    fixture.write_text("\n".join(lines) + "\n", encoding="ascii")
    run(["java", "--add-opens", "java.base/java.util=ALL-UNNAMED", "-Djava.awt.headless=true", "-cp", classpath, "DropEntityReference", str(fixture), str(build / "java.trace")], build)
    native_text = run([str(args.native.resolve()), "--trace", str(fixture)], root)
    (build / "native.trace").write_text(native_text, encoding="ascii")
    java = (build / "java.trace").read_text(encoding="ascii").splitlines()
    native = native_text.splitlines()
    if len(java) != len(native):
        raise RuntimeError(f"Trace lengths differ Java={len(java)}, native={len(native)}")
    failures = [{"row": i, "case": labels[i], "java": a, "native": b} for i, (a, b) in enumerate(zip(java, native)) if a != b]
    report = {"oracle": "Original EntityItem/EntityXPOrb.onUpdate, Entity.moveEntity/World collision, InventoryPlayer.addItemStackToInventory and EntityPlayer.addExperience; single entity, explicit initial motion and RNG", "rows": len(java), "counts": counts, "mismatches": len(failures), "failures": failures[:50], "source_sha256": {str(path.relative_to(mcp)): hashlib.sha256(path.read_bytes()).hexdigest() for path in originals}}
    (build / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    if failures:
        for failure in failures[:4]:
            print(json.dumps(failure))
        raise RuntimeError(f"{len(failures)} drop entity mismatches; see {build / 'report.json'}")
    print(f"PASS {len(java)} exact drop entity rows; {counts}")


if __name__ == "__main__":
    main()
