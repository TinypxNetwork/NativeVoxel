#!/usr/bin/env python3
"""Differentially verify native drops against actual MCP block/item methods."""
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
    parser.add_argument("--native", type=Path, default=root / "tests/drops_tests.exe")
    args = parser.parse_args()
    mcp = args.mcp.resolve()
    build = root / "build/drops_reference"
    classes = build / "classes"
    classes.mkdir(parents=True, exist_ok=True)
    classpath = os.pathsep.join(map(str, [classes, root / "build/drops_export/classes", root / "build/physics_reference/oracle_classes", root / "build/crafting_export/classes", root / "build/registry_export/classes", mcp / "bin/minecraft", mcp / "jars/versions/1.8.9/1.8.9.jar", *sorted((mcp / "jars/libraries").rglob("*.jar"))]))
    source = mcp / "src/minecraft/net/minecraft"
    originals = [source / name for name in ("entity/EntityLivingBase.java", "entity/player/EntityPlayer.java", "entity/player/InventoryPlayer.java", "entity/item/EntityItem.java", "entity/item/EntityXPOrb.java", "item/ItemStack.java", "item/ItemTool.java", "item/ItemSword.java", "item/ItemShears.java", "enchantment/EnchantmentHelper.java", "enchantment/EnchantmentDurability.java")]
    run(["javac", "-encoding", "UTF-8", "--release", "8", "-cp", classpath, "-d", str(classes), *map(str, originals), str(root / "tests/java/DropsReference.java")], build)
    data = json.loads((root / "build/drops_export/drops.json").read_text(encoding="utf-8"))
    states = [value for value in data["states"] if value["behavior"] != "Unsupported"]
    lines: list[str] = []
    labels: list[str] = []
    counts = {"state_harvest": 0, "chance_and_fortune": 0, "tool_ordering": 0}

    def case(mode: str, state: int, *, tool: int = 278, count: int = 1, damage: int = 0, silk: int = 0, fortune: int = 0, unbreaking: int = 0, immutable: int = 0, seed: int = 189, player_seed: int = 918, tile_drops: int = 1, nether: int = 0, below: int = 1, creative: int = 0, chance: float = 1, group: str = "state_harvest") -> None:
        values = [state, tool, count, damage, silk, fortune, unbreaking, immutable, seed, player_seed, tile_drops, nether, below, creative, chance]
        lines.append(mode + " " + " ".join(map(str, values)))
        labels.append(lines[-1])
        counts[group] += 1

    for value in states:
        state = value["state"]
        case("H", state)
        case("H", state, silk=1, fortune=3)
        case("H", state, tool=359, fortune=3)
        case("D", state, fortune=3, tile_drops=0, group="chance_and_fortune")
    # Distinct valid canonical states avoid redundant invalid-metadata aliases.
    dynamic = [value for value in states if value["state"] % 16 == value["canonical"] and (value["quantity"] != "Fixed" or value["item_rule"] != "Fixed" or value["behavior"] in ("Crops", "Cocoa", "Wart", "Stem"))]
    for value in dynamic:
        for seed in range(32):
            for fortune in (0, 1, 3):
                case("D", value["state"], fortune=fortune, seed=seed, chance=(0, .25, 1)[seed % 3], tile_drops=int(seed % 5 != 0), group="chance_and_fortune")
    for meta in range(8):
        case("H", 79 * 16 + meta, nether=1)
        case("H", 79 * 16 + meta, below=0)
    registered = json.loads((root / "build/registry_export/registry.json").read_text(encoding="utf-8"))
    maxima = {value["id"]: value["maxDamage"] for value in registered["items"]}
    for value in states:
        if value["state"] % 16 != 0:
            continue
        for tool in (-1, 257, 270, 278, 256, 258, 267, 276, 359):
            for variant in range(6):
                case("B", value["state"], tool=tool, damage=maxima.get(tool, 0) if variant in (1, 2, 3) else 0, silk=int(variant in (1, 2)), fortune=3 if variant in (0, 3) else 0, unbreaking=3 if variant in (2, 3) else 0, immutable=int(variant == 4), creative=int(variant == 5), seed=variant * 19, player_seed=variant * 31, group="tool_ordering")
    fixture = build / "drops.fixture"
    fixture.write_text("\n".join(lines) + "\n", encoding="ascii")
    run(["java", "-Djava.awt.headless=true", "-cp", classpath, "DropsReference", str(fixture), str(build / "java.trace")], build)
    native_text = run([str(args.native.resolve()), "--trace", str(fixture)], root)
    (build / "native.trace").write_text(native_text, encoding="ascii")
    java = (build / "java.trace").read_text(encoding="ascii").splitlines()
    native = native_text.splitlines()
    if len(java) != len(native):
        raise RuntimeError(f"Trace size mismatch: Java={len(java)}, native={len(native)}")
    failures = [{"row": i, "case": labels[i], "java": a, "native": b} for i, (a, b) in enumerate(zip(java, native)) if a != b]
    originals += sorted((source / "block").rglob("*.java"))
    report = {"oracle": "Original Block harvest/drop, ItemStack.onBlockDestroyed, ItemTool/Sword/Shears; native server removal ordering adapter; no neighbor/tile callbacks or entity motion", "rows": len(java), "counts": counts, "mismatches": len(failures), "failures": failures[:40], "source_sha256": {str(path.relative_to(mcp)): hashlib.sha256(path.read_bytes()).hexdigest() for path in originals}}
    (build / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    if failures:
        for failure in failures[:5]:
            print(json.dumps(failure))
        raise RuntimeError(f"{len(failures)} drop differences; see {build / 'report.json'}")
    print(f"PASS {len(java)} exact drop rows; {counts}")


if __name__ == "__main__":
    main()
