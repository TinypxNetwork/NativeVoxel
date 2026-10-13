#!/usr/bin/env python3
"""Refresh the small source-derived tables used to build without an MCP checkout."""
import argparse
import hashlib
from pathlib import Path
import re

import prepare_assets as base


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcp", type=Path, default=root.parent / "MCP-919-main")
    args = parser.parse_args()
    source = args.mcp / "src/minecraft/net/minecraft"
    registry, classes = base.read_registry(source.parent.parent)
    if not classes:
        parser.error("A complete MCP source tree is required to refresh constants")
    font = (source / "client/gui/FontRenderer.java").read_text(encoding="utf-8")
    alphabet = re.search(r'"((?:\\.|[^"\\])*)"\.indexOf\(ch\)', font)[1].encode("ascii").decode("unicode_escape")
    patterns = re.findall(r'^\s*[A-Z_]+\("([a-z_]+)",\s*"([a-z]+)"', (source / "tileentity/TileEntityBanner.java").read_text(encoding="utf-8"), re.M)
    colors = {name: int(value) for name, value in re.findall(r'MapColor\s+(\w+)\s*=\s*new MapColor\(\d+,\s*(\d+)\)', (source / "block/material/MapColor.java").read_text(encoding="utf-8"))}
    palette = [None] * 16
    for damage, name in re.findall(r'^\s*[A-Z_]+\(\d+,\s*(\d+),[^\n]*?MapColor\.(\w+)', (source / "item/EnumDyeColor.java").read_text(encoding="utf-8"), re.M):
        palette[int(damage)] = colors[name]
    potions = [-1] * 32
    for number, color in re.findall(r'new Potion\w*\((\d+),\s*new ResourceLocation\("[^"]+"\),\s*(?:true|false),\s*(\d+)\)', (source / "potion/Potion.java").read_text(encoding="utf-8")):
        potions[int(number)] = int(color)
    if len(alphabet) != 256 or len(patterns) != 39 or None in palette or sum(c >= 0 for c in potions) != 23:
        raise RuntimeError("Source constants do not match Minecraft 1.8.9")
    paths = sorted((source / "block").glob("*.java")) + [source / name for name in (
        "client/gui/FontRenderer.java", "tileentity/TileEntityBanner.java", "item/EnumDyeColor.java",
        "block/material/MapColor.java", "potion/Potion.java", "client/renderer/ItemRenderer.java",
        "client/renderer/entity/RenderItem.java", "client/renderer/block/model/ItemModelGenerator.java",
        "client/renderer/block/model/ItemCameraTransforms.java", "client/renderer/block/model/ItemTransformVec3f.java",
        "client/renderer/block/model/ModelBlock.java",
    )]
    base.write_json(root / "tools/reference/asset_constants.json", {
        "minecraft_version": "1.8.9",
        "generator": "tools/export_asset_constants.py",
        "blocks": [dict(block, **base.block_flags(block, classes)) for block in registry.values()],
        "ascii_alphabet": alphabet, "banner_patterns": patterns, "dye_map_colors": palette,
        "potion_colors": potions,
        "source_sha256": {p.relative_to(source).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
    })
    print("Exported asset constants; no Java source or resource images were copied.")


if __name__ == "__main__":
    main()
