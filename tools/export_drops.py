#!/usr/bin/env python3
"""Export original block metadata/drop getters for the C++ drop kernels."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def run(arguments: list[str], cwd: Path) -> None:
    result = subprocess.run(arguments, cwd=cwd, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcp", type=Path, default=root.parent / "MCP-919-main")
    args = parser.parse_args()
    mcp = args.mcp.resolve()
    build = root / "build/drops_export"
    classes = build / "classes"
    classes.mkdir(parents=True, exist_ok=True)
    classpath = os.pathsep.join(map(str, [classes, root / "build/registry_export/classes", mcp / "bin/minecraft", mcp / "jars/versions/1.8.9/1.8.9.jar", *sorted((mcp / "jars/libraries").rglob("*.jar"))]))
    originals = sorted((mcp / "src/minecraft/net/minecraft/block").rglob("*.java"))
    javac_args = ["-encoding", "UTF-8", "--release", "8", "-cp", classpath, "-d", str(classes), *map(str, originals), str(root / "tests/java/DropsExport.java")]
    argfile = build / "javac.args"
    argfile.write_text("\n".join('"' + value.replace("\\", "/").replace('"', '\\"') + '"' for value in javac_args), encoding="utf-8")
    run(["javac", "@" + str(argfile)], build)
    raw = build / "drops.json"
    run(["java", "-Djava.awt.headless=true", "-cp", classpath, "DropsExport", str(raw)], build)
    data = json.loads(raw.read_text(encoding="utf-8"))
    lines = ["#pragma once", "// Generated from original MCP block getters by tools/export_drops.py.", "#include <array>", "#include <cstdint>", "namespace mc::drops::data {",
             "enum class Quantity : std::uint8_t { Fixed, Ore, Redstone, Glowstone, Melon, SeaLantern, Mushroom, TallGrass, Leaves };",
             "enum class ItemRule : std::uint8_t { Fixed, Gravel, Seeds };",
             "enum class Behavior : std::uint8_t { Default, None, Leaves, Crops, Cocoa, Wart, Stem, Bed, Unsupported };",
             "struct Rule { bool valid{},silk{}; std::uint8_t canonical{},age{}; std::int16_t item{-1},damage{},count{},silkItem{-1},silkCount{},silkDamage{}; Quantity quantity{}; ItemRule itemRule{}; Behavior behavior{}; };",
             "inline constexpr auto rules=[]() constexpr { std::array<Rule,3168> values{};"]
    for state in data["states"]:
        fields = ["true", str(state["silk"]).lower(), *map(str, (state["canonical"], state["age"], state["item"], state["damage"], state["count"], state["silk_item"], state["silk_count"], state["silk_damage"])), "Quantity::" + state["quantity"], "ItemRule::" + state["item_rule"], "Behavior::" + state["behavior"]]
        lines.append(f"    values[{state['state']}]={{" + ",".join(fields) + "};")
    lines += ["    return values; }();", "} // namespace mc::drops::data", ""]
    header = root / "include/mc/drops_data.hpp"
    header.write_text("\n".join(lines), encoding="ascii")
    report = {"valid_metadata_states": len(data["states"]), "unsupported_dynamic_states": sum(value["behavior"] == "Unsupported" for value in data["states"]), "header_sha256": hashlib.sha256(header.read_bytes()).hexdigest(), "source_sha256": {str(path.relative_to(mcp)): hashlib.sha256(path.read_bytes()).hexdigest() for path in originals}}
    (build / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Exported {report['valid_metadata_states']} block states; {report['unsupported_dynamic_states']} tile/entity-dependent states explicitly unsupported")


if __name__ == "__main__":
    main()
