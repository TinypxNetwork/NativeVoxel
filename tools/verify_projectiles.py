#!/usr/bin/env python3
"""Run unmodified MCP projectile updates and all potion metadata against native code."""
import argparse
import hashlib
import json
import os
import random
import subprocess
from pathlib import Path


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcp", type=Path, default=root.parent / "MCP-919-main")
    parser.add_argument("--native", type=Path, default=root / "build/tests/projectile_tests.exe")
    args = parser.parse_args()
    mcp = args.mcp.resolve()
    build = root / "build/projectile_reference"
    classes = build / "classes"
    classes.mkdir(parents=True, exist_ok=True)
    cp = os.pathsep.join(map(str, [classes, root / "build/crafting_export/classes", root / "build/registry_export/classes",
                                      mcp / "bin/minecraft", mcp / "jars/versions/1.8.9/1.8.9.jar", *sorted((mcp / "jars/libraries").rglob("*.jar"))]))
    names = ["entity/projectile/EntityArrow.java", "entity/projectile/EntityThrowable.java", "entity/projectile/EntityFishHook.java",
             "entity/projectile/EntitySnowball.java", "entity/projectile/EntityEgg.java", "entity/item/EntityEnderPearl.java",
             "entity/projectile/EntityPotion.java", "entity/item/EntityExpBottle.java", "potion/PotionHelper.java", "potion/PotionEffect.java", "item/ItemPotion.java"]
    originals = [mcp / "src/minecraft/net/minecraft" / name for name in names]
    subprocess.run(["javac", "-encoding", "UTF-8", "--release", "8", "-cp", cp, "-d", str(classes), *map(str, originals),
                    str(root / "tests/java/DropEntityReference.java"), str(root / "tests/java/ProjectileReference.java")], check=True)
    rng = random.Random(189106)
    rows = []
    for kind in (60, 61, 62, 65, 73, 75, 90):
        for _ in range(32):
            rows.append([kind, 0, .5, 80, .5, rng.uniform(-2, 2), rng.uniform(-1, 2), rng.uniform(-2, 2), 32])
        for vy in (-4, -1, -.25, 0, .5):
            rows.append([kind, 1, .5, 66, .5, .15, vy, .075, 32])
        for vy in (-.15, 0, .15):
            rows.append([kind, 2, .5, 65, .5, .01, vy, .02, 32])
    fixture = build / "projectiles.fixture"
    fixture.write_text("\n".join(" ".join(map(str, row)) for row in rows) + "\n", encoding="ascii")
    java_command = ["java", "--add-opens", "java.base/java.util=ALL-UNNAMED", "-Djava.awt.headless=true", "-cp", cp, "ProjectileReference"]
    subprocess.run(java_command + [str(fixture), str(build / "java.trace")], check=True, cwd=root)
    subprocess.run(java_command + ["--potions", str(build / "java-potions.trace")], check=True, cwd=root)
    with (build / "native.trace").open("w", encoding="utf-8") as output:
        subprocess.run([str(args.native.resolve()), "--trace", str(fixture)], check=True, stdout=output, cwd=root)
    with (build / "native-potions.trace").open("w", encoding="utf-8") as output:
        subprocess.run([str(args.native.resolve()), "--potions-trace"], check=True, stdout=output, cwd=root)
    report = {"source_sha256": {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in zip(names, originals)}}
    for kind, left, right in (("projectile", "java.trace", "native.trace"), ("potion", "java-potions.trace", "native-potions.trace")):
        expected = (build / left).read_text(encoding="utf-8").splitlines()
        actual = (build / right).read_text(encoding="utf-8").splitlines()
        if len(expected) != len(actual):
            raise RuntimeError(f"{kind} row count differs: {len(expected)} != {len(actual)}")
        failures = [{"row": index, "java": first, "native": second} for index, (first, second) in enumerate(zip(expected, actual)) if first != second]
        report[kind] = {"rows": len(expected), "mismatches": len(failures), "first_failures": failures[:12]}
    (build / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({kind: {key: value for key, value in report[kind].items() if key != "first_failures"} for kind in ("projectile", "potion")}))
    if report["projectile"]["mismatches"] or report["potion"]["mismatches"]:
        raise RuntimeError(f"Original Java differences found; inspect {build / 'report.json'}")


if __name__ == "__main__":
    main()
