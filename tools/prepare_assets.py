#!/usr/bin/env python3
"""Import verified local Minecraft 1.8.9 resources and bake native block meshes.

Only the build/import step uses Python and Pillow. The client needs neither Python
nor a JVM. No network requests are made. Source files remain unchanged.
"""
from __future__ import annotations

import argparse
from collections import Counter
from functools import lru_cache
import hashlib
import io
import json
import math
from pathlib import Path, PurePosixPath
import re
import struct
import sys
import zipfile

from PIL import Image


FORMAT_VERSION = 1
FACES = ("down", "up", "north", "south", "west", "east")
NORMALS = ((0, -1, 0), (0, 1, 0), (0, 0, -1), (0, 0, 1), (-1, 0, 0), (1, 0, 0))
# EnumFaceDirection.java's corner ordering, in min/max x, y, z coordinates.
CORNERS = (
    ((0, 0, 1), (0, 0, 0), (1, 0, 0), (1, 0, 1)),
    ((0, 1, 0), (0, 1, 1), (1, 1, 1), (1, 1, 0)),
    ((1, 1, 0), (1, 0, 0), (0, 0, 0), (0, 1, 0)),
    ((0, 1, 1), (0, 0, 1), (1, 0, 1), (1, 1, 1)),
    ((0, 1, 0), (0, 0, 0), (0, 0, 1), (0, 1, 1)),
    ((1, 1, 1), (1, 0, 1), (1, 0, 0), (1, 1, 0)),
)
WOODS = ("oak", "spruce", "birch", "jungle", "acacia", "dark_oak")
COLORS = ("white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
          "silver", "cyan", "purple", "blue", "brown", "green", "red", "black")
HORIZONTALS = ("south", "west", "north", "east")
RAILS = ("north_south", "east_west", "ascending_east", "ascending_west", "ascending_north",
         "ascending_south", "south_east", "south_west", "north_west", "north_east")
SLABS = ("stone", "sandstone", "wood_old", "cobblestone", "brick", "stone_brick", "nether_brick", "quartz")
PLANTS = ("sunflower", "syringa", "double_grass", "double_fern", "double_rose", "paeonia")
STAIRS = {53, 67, 108, 109, 114, 128, 134, 135, 136, 156, 163, 164, 180}
FENCES = {85, 113, 188, 189, 190, 191, 192}
GATES = {107, 183, 184, 185, 186, 187}
DOORS = {64, 71, 193, 194, 195, 196, 197}
PANES = {101, 102, 160}
BUILTINS = {0, 8, 9, 10, 11, 36, 54, 63, 68, 119, 130, 144, 146, 166, 176, 177}


class AssetError(RuntimeError):
    pass


@lru_cache(maxsize=1)
def asset_constants():
    """Source-derived constants needed for builds without a local MCP checkout."""
    path = Path(__file__).resolve().parent / "reference/asset_constants.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    if data["minecraft_version"] != "1.8.9" or len(data["blocks"]) != 198:
        raise AssetError("Invalid checked-in asset constants")
    return data


def source_hash(mcp: Path, name: str) -> str:
    path = mcp / "src/minecraft/net/minecraft" / name
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else asset_constants()["source_sha256"][name]


def sha1(data: bytes) -> str:
    return hashlib.sha1(data).hexdigest()


def safe_resource_name(name: str) -> str:
    """Reject ZIP-slip paths and Windows special path forms before any writes."""
    if not name or "\\" in name or ":" in name or "\0" in name:
        raise AssetError(f"Unsafe resource path: {name!r}")
    path = PurePosixPath(name)
    if path.is_absolute() or any(x in ("", ".", "..") for x in name.split("/")):
        raise AssetError(f"Unsafe resource path: {name!r}")
    if any(part.rstrip(". ") != part for part in path.parts):
        raise AssetError(f"Unsafe Windows resource path: {name!r}")
    if any(re.fullmatch(r"(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])", part.split(".", 1)[0], re.IGNORECASE) for part in path.parts):
        raise AssetError(f"Reserved Windows resource path: {name!r}")
    return path.as_posix()


def write_bytes(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    # Avoid rewriting 100+ MB of sound data every time a model changes.
    if path.exists() and path.stat().st_size == len(data) and path.read_bytes() == data:
        return
    temporary = path.with_name(path.name + ".native-import.tmp")
    temporary.write_bytes(data)
    temporary.replace(path)


def write_json(path: Path, value: object) -> None:
    write_bytes(path, (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))


def identifier(value: str, default_namespace: str = "minecraft") -> tuple[str, str]:
    if ":" in value:
        namespace, path = value.split(":", 1)
    else:
        namespace, path = default_namespace, value
    safe_resource_name(namespace + "/" + path)
    return namespace, path


def texture_name(value: str, namespace: str = "minecraft") -> str:
    ns, path = identifier(value, namespace)
    return f"{ns}:{path}"


def canonical_properties(value: str | dict) -> str:
    if isinstance(value, str):
        if "=" not in value:
            return value
        value = dict(piece.split("=", 1) for piece in value.split(","))
    def string(v):
        return str(v).lower() if isinstance(v, bool) else str(v)
    return ",".join(f"{k}={string(v)}" for k, v in sorted(value.items())) or "normal"


def method_body(source: str, signature: str) -> str | None:
    found = re.search(re.escape(signature) + r"\([^)]*\)\s*\{", source)
    if not found:
        return None
    depth, end = 1, found.end()
    while depth and end < len(source):
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[found.end():end - 1]


def read_registry(source_root: Path) -> tuple[dict, dict]:
    block_dir = source_root / "net/minecraft/block"
    if not (block_dir / "Block.java").is_file():
        return {block["id"]: dict(block) for block in asset_constants()["blocks"]}, {}
    source = (block_dir / "Block.java").read_text(encoding="utf-8")
    variables = dict(re.findall(r"Block\s+(block\d*)\s*=\s*\(?new\s+(Block\w*)\(", source))
    registry = {}
    for match in re.finditer(r'registerBlock\((\d+),\s*(?:"([^"\n]+)"|(AIR_ID)),\s*([^;]+);', source):
        number, name, air, expression = match.groups()
        class_match = re.search(r"new\s+(Block[\w.]*)\(", expression)
        variable_match = re.match(r"(block\d*)\)", expression)
        cls = class_match.group(1) if class_match else variables.get(variable_match.group(1), "Block") if variable_match else "Block"
        registry[int(number)] = {"id": int(number), "name": name or "air", "java_class": cls,
                                 "source_line": source[:match.start()].count("\n") + 1}
    if set(registry) != set(range(198)):
        raise AssetError("Block.java registry is not the expected 1.8.9 IDs 0..197")
    classes = {}
    for path in sorted(block_dir.glob("*.java")):
        code = path.read_text(encoding="utf-8")
        parent = re.search(r"class\s+" + re.escape(path.stem) + r"\s+extends\s+(\w+)", code)
        classes[path.stem] = {"parent": parent.group(1) if parent else None, "source": code}
    return registry, classes


def class_method(classes: dict, cls: str, signature: str) -> str:
    cls = cls.split(".")[0]
    visited = set()
    while cls and cls not in visited:
        visited.add(cls)
        item = classes.get(cls)
        if not item:
            break
        body = method_body(item["source"], signature)
        if body is not None:
            return body.strip()
        cls = item["parent"]
    return ""


def block_flags(block: dict, classes: dict) -> dict:
    if not classes and "layer" in block:
        return {key: block[key] for key in ("layer", "occludes", "full_cube", "approximate")}
    number, cls = block["id"], block["java_class"]
    layer_body = class_method(classes, cls, "EnumWorldBlockLayer getBlockLayer")
    layer = 2 if "TRANSLUCENT" in layer_body else 1 if "CUTOUT" in layer_body else 0
    if number in (10, 11):
        layer = 0
    opaque_body = class_method(classes, cls, "boolean isOpaqueCube")
    full_body = class_method(classes, cls, "boolean isFullCube")
    def boolean(body):
        if "isDouble()" in body:
            return number in (43, 125, 181)
        return body == "return true;"
    return {"layer": layer, "occludes": boolean(opaque_body), "full_cube": boolean(full_body),
            "approximate": False}


def enum_value(values, meta, fallback=0):
    return values[meta] if 0 <= meta < len(values) else values[fallback]


def state_mapping(number: int, meta: int, name: str) -> tuple[str, str, bool]:
    """Port getStateFromMeta and BlockModelShapes' mapper for each vanilla block.

    The bool marks invalid metadata (not a legal IBlockState in Java). Such states
    are deliberately mapped to a visible/default model instead of crashing.
    Actual-state properties that need neighbors start at the Java default.
    """
    resource, props, invalid = name, {}, False
    horizontal = HORIZONTALS[meta & 3]
    front = FACES[meta % 6]
    side = front if front not in ("up", "down") else "north"
    connections = {face: False for face in ("east", "north", "south", "west")}
    if number in BUILTINS:
        return "native:" + name, canonical_properties({"meta": meta}), False
    if number == 1:
        resource = enum_value(("stone", "granite", "smooth_granite", "diorite", "smooth_diorite", "andesite", "smooth_andesite"), meta)
    elif number == 2:
        props = {"snowy": False}
    elif number == 3:
        resource = enum_value(("dirt", "coarse_dirt", "podzol"), meta)
        if resource == "podzol": props = {"snowy": False}
    elif number == 5:
        resource = enum_value(WOODS, meta) + "_planks"
    elif number == 6:
        resource, props = enum_value(WOODS, meta & 7) + "_sapling", {"stage": meta >> 3}
    elif number == 12:
        resource = enum_value(("sand", "red_sand"), meta)
    elif number in (17, 162):
        wood = (meta & 3) + (4 if number == 162 else 0)
        resource, props = enum_value(WOODS, wood) + "_log", {"axis": ("y", "x", "z", "none")[(meta >> 2) & 3]}
        invalid = number == 162 and wood > 5
    elif number in (18, 161):
        wood = (meta & 3) + (4 if number == 161 else 0)
        resource = enum_value(WOODS, wood) + "_leaves"
        invalid = number == 161 and wood > 5
    elif number == 19:
        props = {"wet": bool(meta & 1)}
    elif number in (23, 158):
        props = {"facing": FACES[(meta & 7) % 6]}
    elif number in (24, 179):
        prefix = "red_" if number == 179 else ""
        resource = enum_value((prefix + "sandstone", "chiseled_" + prefix + "sandstone", "smooth_" + prefix + "sandstone"), meta)
    elif number == 26:
        props = {"facing": horizontal, "part": "head" if meta & 8 else "foot"}
    elif number in (27, 28, 157):
        invalid = (meta & 7) > 5
        props = {"shape": RAILS[(meta & 7) if not invalid else 0], "powered": bool(meta & 8)}
    elif number in (29, 33):
        invalid = (meta & 7) > 5
        props = {"facing": FACES[(meta & 7) if not invalid else 0], "extended": bool(meta & 8)}
    elif number == 31:
        resource = enum_value(("dead_bush", "tall_grass", "fern"), meta)
    elif number == 32:
        resource = "dead_bush"
    elif number == 34:
        invalid = (meta & 7) > 5
        props = {"facing": FACES[(meta & 7) if not invalid else 0], "short": False,
                 "type": "sticky" if meta & 8 else "normal"}
    elif number in (35, 95, 159, 160, 171):
        resource = COLORS[meta] + "_" + name
        if number == 160: props = connections
    elif number == 37:
        resource = "dandelion"
    elif number == 38:
        resource = enum_value(("poppy", "blue_orchid", "allium", "houstonia", "red_tulip", "orange_tulip", "white_tulip", "pink_tulip", "oxeye_daisy"), meta)
    elif number in (43, 44, 181, 182):
        material = "red_sandstone" if number in (181, 182) else SLABS[meta & 7]
        if number in (43, 181):
            return "minecraft:" + material + "_double_slab", "all" if meta & 8 else "normal", False
        resource, props = material + "_slab", {"half": "top" if meta & 8 else "bottom"}
    elif number in (50, 75, 76):
        props = {"facing": {1: "east", 2: "west", 3: "south", 4: "north"}.get(meta, "up")}
    elif number == 51:
        props = {**connections, "alt": False, "flip": False, "upper": 0}
    elif number in STAIRS:
        props = {"facing": FACES[5 - (meta & 3)], "half": "top" if meta & 4 else "bottom", "shape": "straight"}
    elif number == 55:
        props = {face: "none" for face in connections}
    elif number in (59, 141, 142):
        props, invalid = {"age": meta if meta < 8 else 0}, meta > 7
    elif number == 60:
        props = {"moisture": meta & 7}
    elif number in (61, 62, 65):
        props = {"facing": side}
    elif number in DOORS:
        if meta & 8:
            props = {"half": "upper", "hinge": "right" if meta & 1 else "left", "facing": "north", "open": False}
        else:
            props = {"half": "lower", "hinge": "left", "facing": ("east", "south", "west", "north")[meta & 3], "open": bool(meta & 4)}
    elif number == 66:
        props = {"shape": enum_value(RAILS, meta)}
    elif number == 69:
        props = {"facing": ("down_x", "east", "west", "south", "north", "up_z", "up_x", "down_z")[meta & 7], "powered": bool(meta & 8)}
    elif number in (70, 72):
        props = {"powered": meta == 1}
    elif number in (77, 143):
        props = {"facing": enum_value(("down", "east", "west", "south", "north", "up"), meta & 7, 5), "powered": bool(meta & 8)}
    elif number == 78:
        props = {"layers": (meta & 7) + 1}
    elif number in FENCES or number in (101, 102):
        props = connections
    elif number in (86, 91):
        props = {"facing": horizontal}
    elif number == 90:
        props = {"axis": "z" if (meta & 3) == 2 else "x"}
    elif number == 92:
        props, invalid = {"bites": meta if meta <= 6 else 0}, meta > 6
    elif number in (93, 94):
        props = {"facing": horizontal, "locked": False, "delay": 1 + (meta >> 2)}
    elif number in (96, 167):
        props = {"facing": ("north", "south", "west", "east")[meta & 3], "open": bool(meta & 4), "half": "top" if meta & 8 else "bottom"}
    elif number == 97:
        resource = enum_value(("stone", "cobblestone", "stone_brick", "mossy_brick", "cracked_brick", "chiseled_brick"), meta) + "_monster_egg"
    elif number == 98:
        resource = enum_value(("stonebrick", "mossy_stonebrick", "cracked_stonebrick", "chiseled_stonebrick"), meta)
    elif number in (99, 100):
        mushroom = {0: "all_inside", 1: "north_west", 2: "north", 3: "north_east", 4: "west", 5: "center", 6: "east", 7: "south_west", 8: "south", 9: "south_east", 10: "stem", 14: "all_outside", 15: "all_stem"}
        props = {"variant": mushroom.get(meta, "all_inside")}
    elif number in (104, 105):
        props, invalid = {"age": meta if meta < 8 else 0, "facing": "up"}, meta > 7
    elif number == 106:
        props = {"south": bool(meta & 1), "west": bool(meta & 2), "north": bool(meta & 4), "east": bool(meta & 8), "up": False}
    elif number in GATES:
        props = {"facing": horizontal, "open": bool(meta & 4), "in_wall": False}
    elif number == 110:
        props = {"snowy": False}
    elif number == 115:
        props, invalid = {"age": meta if meta < 4 else 0}, meta > 3
    elif number == 117:
        props = {f"has_bottle_{i}": bool(meta & (1 << i)) for i in range(3)}
    elif number == 118:
        props, invalid = {"level": meta if meta < 4 else 0}, meta > 3
    elif number == 120:
        props = {"eye": bool(meta & 4), "facing": horizontal}
    elif number in (125, 126):
        resource = enum_value(WOODS, meta & 7) + ("_double_slab" if number == 125 else "_slab")
        if number == 126: props = {"half": "top" if meta & 8 else "bottom"}
    elif number == 127:
        props, invalid = {"age": (meta >> 2) if meta < 12 else 0, "facing": horizontal}, meta >= 12
    elif number == 131:
        props = {"facing": horizontal, "powered": bool(meta & 8), "attached": bool(meta & 4), "suspended": False}
    elif number == 132:
        props = {**connections, "suspended": bool(meta & 2), "attached": bool(meta & 4)}
    elif number == 139:
        resource, props = enum_value(("cobblestone", "mossy_cobblestone"), meta) + "_wall", {**connections, "up": False}
    elif number == 140:
        props = {"contents": "empty"}
    elif number == 145:
        props, invalid = {"facing": horizontal, "damage": (meta >> 2) if meta < 12 else 0}, meta >= 12
    elif number in (147, 148, 151, 178):
        props = {"power": meta}
    elif number in (149, 150):
        props = {"facing": horizontal, "powered": bool(meta & 8), "mode": "subtract" if meta & 4 else "compare"}
    elif number == 154:
        facing = FACES[(meta & 7) % 6]
        props, invalid = {"facing": "down" if facing == "up" else facing}, facing == "up"
    elif number == 155:
        resource = enum_value(("quartz_block", "chiseled_quartz_block", "quartz_column", "quartz_column", "quartz_column"), meta)
        if 2 <= meta <= 4: props = {"axis": ("y", "x", "z")[meta - 2]}
    elif number == 168:
        resource = enum_value(("prismarine", "prismarine_bricks", "dark_prismarine"), meta)
    elif number == 170:
        props = {"axis": "x" if meta & 12 == 4 else "z" if meta & 12 == 8 else "y"}
    elif number == 175:
        resource = "sunflower" if meta & 8 else enum_value(PLANTS, meta & 7)
        props = {"half": "upper" if meta & 8 else "lower"}
    return "minecraft:" + resource, canonical_properties(props), invalid


def import_resources(mcp: Path, out: Path, packs: list[Path], allow_missing=False) -> tuple[dict, dict]:
    version_dir = mcp / "jars/versions/1.8.9"
    metadata = json.loads((version_dir / "1.8.9.json").read_text(encoding="utf-8"))
    jar_path = version_dir / "1.8.9.jar"
    jar = jar_path.read_bytes()
    expected_jar = metadata["downloads"]["client"]
    index_path = mcp / "jars/assets/indexes/1.8.json"
    index_bytes = index_path.read_bytes()
    index = json.loads(index_bytes)
    errors, records, resources = [], [], {}
    report = {"format": FORMAT_VERSION, "minecraft_version": "1.8.9", "downloads_performed": 0,
              "jar_sha1": sha1(jar), "jar_expected_sha1": expected_jar["sha1"],
              "index_sha1": sha1(index_bytes), "index_expected_sha1": metadata["assetIndex"]["sha1"]}
    if sha1(jar) != expected_jar["sha1"] or len(jar) != expected_jar["size"]:
        errors.append({"source": str(jar_path), "error": "client jar SHA-1/size does not match version metadata"})
    if sha1(index_bytes) != metadata["assetIndex"]["sha1"]:
        errors.append({"source": str(index_path), "error": "asset index SHA-1 does not match version metadata"})
    if errors:
        report["errors"] = errors
        write_json(out / "import-report.json", report)
        raise AssetError("Local jar/index integrity mismatch; see import-report.json")
    write_bytes(out / "vanilla/1.8.9.json", (version_dir / "1.8.9.json").read_bytes())
    write_bytes(out / "vanilla/indexes/1.8.json", index_bytes)
    with zipfile.ZipFile(io.BytesIO(jar)) as archive:
        for info in sorted(archive.infolist(), key=lambda x: x.filename):
            if info.is_dir() or not (info.filename.startswith("assets/") or info.filename in ("pack.png", "pack.mcmeta")):
                continue
            name = safe_resource_name(info.filename)
            data = archive.read(info)
            write_bytes(out / "vanilla/jar" / name, data)
            resources[name] = data
            records.append({"path": name, "source": "jar", "sha1": sha1(data), "size": len(data)})
    verified_objects = {}
    for logical_name, entry in sorted(index["objects"].items()):
        safe_resource_name(logical_name)
        digest = entry["hash"]
        if not re.fullmatch(r"[0-9a-f]{40}", digest):
            raise AssetError("Invalid SHA-1 in asset index")
        path = mcp / "jars/assets/objects" / digest[:2] / digest
        if digest in verified_objects:
            data = verified_objects[digest]
        elif not path.is_file():
            errors.append({"path": logical_name, "hash": digest, "error": "missing object"})
            continue
        else:
            data = path.read_bytes()
            if len(data) != entry["size"] or sha1(data) != digest:
                errors.append({"path": logical_name, "hash": digest, "error": "object SHA-1/size mismatch"})
                continue
            verified_objects[digest] = data
        name = "assets/" + logical_name
        resources[name] = data
        write_bytes(out / "vanilla/index" / logical_name, data)
        records.append({"path": name, "logical_index_path": logical_name, "source": "index",
                        "sha1": digest, "size": len(data)})
    report.update({"jar_resource_files": sum(r["source"] == "jar" for r in records),
                   "index_entries": len(index["objects"]), "index_imported_entries": sum(r["source"] == "index" for r in records),
                   "verified_unique_objects": len(verified_objects), "errors": errors, "resource_packs": []})
    write_json(out / "vanilla-manifest.json", records)
    if errors and not allow_missing:
        write_json(out / "import-report.json", report)
        raise AssetError("Missing/corrupt indexed resources; see import-report.json (no downloads attempted)")
    # SoundHandler loads every sounds.json layer, then applies per-event replace.
    # Keeping only the effective top-level JSON would incorrectly discard base events.
    sound_layers = {}
    for record in records:
        match = re.fullmatch(r"assets/([^/]+)/sounds\.json", record["path"])
        if match:
            physical = "vanilla/" + record["source"] + "/" + (record["path"] if record["source"] == "jar" else record["logical_index_path"])
            sound_layers[match[1]] = physical
    sound_layer_index = [(ns, path) for ns, path in sorted(sound_layers.items())]
    for position, pack in enumerate(packs):
        pack = pack.resolve()
        overrides = read_pack(pack)
        if "pack.mcmeta" not in overrides:
            raise AssetError(f"Resource pack has no pack.mcmeta: {pack}")
        try:
            pack_metadata = json.loads(overrides["pack.mcmeta"])
        except ValueError as e:
            raise AssetError(f"Invalid pack.mcmeta in {pack}: {e}") from e
        if pack_metadata.get("pack", {}).get("pack_format") != 1:
            raise AssetError(f"Resource pack must target 1.8.9 pack_format 1: {pack}")
        pack_records = []
        for name, data in sorted(overrides.items()):
            write_bytes(out / "resourcepacks" / str(position) / name, data)
            if name.startswith("assets/"):
                resources[name] = data
            sound_match = re.fullmatch(r"assets/([^/]+)/sounds\.json", name)
            if sound_match:
                sound_layer_index.append((sound_match[1], f"resourcepacks/{position}/{name}"))
            pack_records.append({"path": name, "sha1": sha1(data), "size": len(data)})
        report["resource_packs"].append({"path": str(pack), "priority": position,
                                          "description": pack_metadata["pack"].get("description", ""),
                                          "files": pack_records})
    effective = []
    for name, data in sorted(resources.items()):
        write_bytes(out / "resources" / name, data)
        effective.append({"path": name, "sha1": sha1(data), "size": len(data)})
    # Remove only stale files from this importer's previous manifest, never user files.
    previous_path = out / "effective-manifest.json"
    if previous_path.exists():
        previous = json.loads(previous_path.read_text(encoding="utf-8"))
        root = (out / "resources").resolve()
        for entry in previous:
            name = safe_resource_name(entry["path"])
            stale = (root / name).resolve()
            if name not in resources and stale.is_relative_to(root) and stale.is_file():
                stale.unlink()
    write_json(previous_path, effective)
    report["effective_files"] = len(effective)
    report["effective_bytes"] = sum(e["size"] for e in effective)
    report["sound_files"] = sum(p.endswith(".ogg") for p in resources)
    report["language_files"] = sum("/lang/" in p and p.endswith(".lang") for p in resources)
    write_bytes(out / "runtime/audio/sounds-layers.txt", ("".join(ns + "\t" + path + "\n" for ns, path in sound_layer_index)).encode("utf-8"))
    sound_files = []
    for name in sorted(resources):
        match = re.fullmatch(r"assets/([^/]+)/(sounds/.+\.ogg)", name)
        if match:
            sound_files.append(match[1] + ":" + match[2])
    write_bytes(out / "runtime/audio/ogg-files.txt", ("\n".join(sound_files) + "\n").encode("utf-8"))
    write_json(out / "import-report.json", report)
    return resources, report


def prepare_audio_runtime(mcp, out):
    metadata = json.loads((mcp / "jars/versions/1.8.9/1.8.9.json").read_text(encoding="utf-8"))
    report = {"available": False, "downloads_performed": 0}
    for library in metadata.get("libraries", []):
        if library.get("name") != "org.lwjgl.lwjgl:lwjgl-platform:2.9.4-nightly-20150209":
            continue
        expected = library["downloads"]["classifiers"]["natives-windows"]
        archive_path = mcp / "jars/libraries" / expected["path"]
        if not archive_path.is_file():
            report["error"] = "Bundled Windows natives archive is missing; audio remains optional"
            break
        archive_data = archive_path.read_bytes()
        if sha1(archive_data) != expected["sha1"] or len(archive_data) != expected["size"]:
            report["error"] = "Bundled Windows natives archive failed SHA-1/size check; DLL was not copied"
            break
        with zipfile.ZipFile(io.BytesIO(archive_data)) as archive:
            dll = archive.read("OpenAL64.dll")
        extracted = mcp / "jars/versions/1.8.9/1.8.9-natives/OpenAL64.dll"
        extracted_matches = extracted.is_file() and extracted.read_bytes() == dll
        write_bytes(out / "runtime/OpenAL64.dll", dll)
        report.update({"available": True, "source_archive": str(archive_path), "archive_sha1": expected["sha1"],
                       "extracted_dll_matches_archive": extracted_matches, "dll_sha1": sha1(dll),
                       "dll_sha256": hashlib.sha256(dll).hexdigest(), "dll_bytes": len(dll),
                       "implementation": "OpenAL Soft 1.15.1 (version string in original bundled DLL)",
                       "license": "LGPL; see third_party/OpenAL-Soft-COPYING and docs/AUDIO.md"})
        break
    write_json(out / "runtime/audio/openal-provenance.json", report)
    return report


def read_pack(path: Path) -> dict[str, bytes]:
    entries = {}
    case_names = set()
    def add(name, data):
        safe_resource_name(name)
        casefolded = name.casefold()
        if casefolded in case_names:
            raise AssetError(f"Duplicate case-insensitive path in resource pack: {name}")
        case_names.add(casefolded)
        entries[name] = data
    if path.is_dir():
        for file in sorted(path.rglob("*")):
            if not file.is_file():
                continue
            if not file.resolve().is_relative_to(path):
                raise AssetError(f"Resource pack symlink escapes root: {file}")
            add(file.relative_to(path).as_posix(), file.read_bytes())
    else:
        with zipfile.ZipFile(path) as archive:
            for info in archive.infolist():
                if info.is_dir():
                    continue
                if info.file_size > 128 * 1024 * 1024:
                    raise AssetError(f"Resource pack file exceeds 128 MiB: {info.filename}")
                add(info.filename, archive.read(info))
    return entries


class ModelResolver:
    def __init__(self, resources):
        self.resources, self.cache = resources, {}

    def resolve(self, name: str, stack=()) -> dict:
        namespace, path = identifier(name)
        canonical = namespace + ":" + path
        if canonical in self.cache:
            return self.cache[canonical]
        if canonical in stack:
            raise AssetError("Model parent cycle: " + " -> ".join((*stack, canonical)))
        if path.startswith("builtin/"):
            return {"textures": {}, "elements": [], "ambientocclusion": False, "builtin": path}
        filename = f"assets/{namespace}/models/{path}.json"
        if filename not in self.resources:
            raise AssetError(f"Missing model: {filename}")
        raw = json.loads(self.resources[filename])
        if "parent" in raw:
            if raw.get("elements"):
                raise AssetError(f"1.8.9 model cannot specify both parent and elements: {filename}")
            # ResourceLocation(String) defaults to minecraft, not the child's namespace.
            parent = self.resolve(texture_name(raw["parent"]), (*stack, canonical))
            result = dict(parent)
            result["textures"] = {**parent.get("textures", {}), **raw.get("textures", {})}
        else:
            result = {"textures": dict(raw.get("textures", {})), "elements": [], "ambientocclusion": True}
        for key, value in raw.items():
            if key not in ("parent", "textures") and not ("parent" in raw and key in ("elements", "ambientocclusion")):
                result[key] = value
        result["namespace"] = namespace
        self.cache[canonical] = result
        return result

    @staticmethod
    def texture(model: dict, reference: str) -> str:
        seen = set()
        while reference.startswith("#"):
            key = reference[1:]
            if key in seen:
                raise AssetError("Cyclic texture reference: " + reference)
            seen.add(key)
            if key not in model.get("textures", {}):
                raise AssetError("Unresolved texture reference: " + reference)
            reference = model["textures"][key]
        return texture_name(reference)


def rotation(point, axis, angle, origin=(0, 0, 0), rescale=False):
    values = [point[i] - origin[i] for i in range(3)]
    cosine, sine = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    a, b = {"x": (1, 2), "y": (2, 0), "z": (0, 1)}[axis]
    values[a], values[b] = values[a] * cosine - values[b] * sine, values[a] * sine + values[b] * cosine
    if rescale:
        factor = 1.0 / math.cos(math.radians(22.5 if abs(angle) == 22.5 else 45))
        values[a] *= factor
        values[b] *= factor
    return tuple(round(values[i] + origin[i], 10) for i in range(3))


def model_rotation(point, x, y, origin=(0.5, 0.5, 0.5)):
    return rotation(rotation(point, "x", -x, origin), "y", -y, origin)


def closest_face(normal):
    return max(range(6), key=lambda face: sum(a * b for a, b in zip(normal, NORMALS[face])))


def normalized_cross(a, b, c):
    u, v = [b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)]
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    length = math.sqrt(sum(x * x for x in n))
    return tuple(x / length for x in n) if length > 1e-9 else (0.0, 1.0, 0.0)


def default_uv(face, start, end):
    if face in (0, 1): return (start[0], start[2], end[0], end[2])
    if face in (2, 3): return (start[0], 16 - end[1], end[0], 16 - start[1])
    return (start[2], 16 - end[1], end[2], 16 - start[1])


def locked_uv(point, face):
    x, y, z = (v - math.floor(v) if v < -0.1 or v >= 1.1 else v for v in point)
    return ((x, 1 - z), (x, z), (1 - x, 1 - y), (x, 1 - y), (z, 1 - y), (1 - z, 1 - y))[face]


def bake_model(model, option, atlas, flags):
    xrot, yrot = option.get("x", 0), option.get("y", 0)
    if xrot not in (0, 90, 180, 270) or yrot not in (0, 90, 180, 270):
        raise AssetError("Blockstate rotations must be 0, 90, 180, 270")
    quads = []
    for part in model.get("elements", []):
        start, end = part["from"], part["to"]
        if len(start) != 3 or len(end) != 3:
            raise AssetError("Model element must have 3-dimensional bounds")
        if any(not math.isfinite(v) or v < -16 or v > 32 for v in (*start, *end)):
            raise AssetError("Model element coordinates must be finite and in [-16,32]")
        for direction, face_data in part.get("faces", {}).items():
            face = FACES.index(direction)
            texture = ModelResolver.texture(model, face_data["texture"])
            if texture not in atlas:
                raise AssetError(f"Texture absent from terrain atlas: {texture}")
            tile = atlas[texture]
            bounds = [(start[i] / 16, end[i] / 16) for i in range(3)]
            original = [tuple(bounds[i][bits[i]] for i in range(3)) for bits in CORNERS[face]]
            uv = face_data.get("uv", default_uv(face, start, end))
            uvrot = face_data.get("rotation", 0)
            if uvrot not in (0, 90, 180, 270): raise AssetError("Invalid face UV rotation")
            raw_uv = ((uv[0], uv[1]), (uv[0], uv[3]), (uv[2], uv[3]), (uv[2], uv[1]))
            positions, uvs = [None] * 4, [None] * 4
            # Match ModelRotation.rotateVertex before UV-lock and applyFacing.
            rotated_face = closest_face(model_rotation(NORMALS[face], xrot, 0, (0, 0, 0)))
            shift = (xrot // 90 if face in (4, 5) else 0) + (yrot // 90 if rotated_face in (0, 1) else 0)
            for i, point in enumerate(original):
                if "rotation" in part:
                    rot = part["rotation"]
                    if rot["axis"] not in "xyz" or rot["angle"] not in (-45, -22.5, 0, 22.5, 45):
                        raise AssetError("Unsupported 1.8.9 element rotation")
                    point = rotation(point, rot["axis"], rot["angle"], tuple(v / 16 for v in rot["origin"]), rot.get("rescale", False))
                target = (i + shift) % 4
                positions[target] = model_rotation(point, xrot, yrot)
                uvs[target] = tuple(v / 16 for v in raw_uv[(i + uvrot // 90) % 4])
            normal = normalized_cross(*positions[:3])
            final_face = closest_face(normal)
            if option.get("uvlock", False):
                for i, point in enumerate(positions):
                    uvs[(i + 4 - uvrot // 90) % 4] = locked_uv(point, final_face)
            # FaceBakery reorders axis-aligned elements to face-canonical winding.
            if "rotation" not in part:
                lo = [min(p[i] for p in positions) for i in range(3)]
                hi = [max(p[i] for p in positions) for i in range(3)]
                reordered = [tuple((lo, hi)[bits[i]][i] for i in range(3)) for bits in CORNERS[final_face]]
                reordered_uv = []
                for point in reordered:
                    nearest = min(range(4), key=lambda k: sum(abs(point[i] - positions[k][i]) for i in range(3)))
                    reordered_uv.append(uvs[nearest])
                positions, uvs = reordered, reordered_uv
            cull = face_data.get("cullface")
            cull_face = closest_face(model_rotation(NORMALS[FACES.index(cull)], xrot, yrot, (0, 0, 0))) if cull else -1
            vertices = []
            for point, pair in zip(positions, uvs):
                u, v = pair
                # Same small texel inset as TextureAtlasSprite.initSprite (0.01 px).
                au = tile["u0"] + (tile["u1"] - tile["u0"]) * u
                av = tile["v0"] + (tile["v1"] - tile["v0"]) * v
                vertices.append((*point, au, av))
            quads.append({"vertices": vertices, "normal": normal, "face": final_face, "cull_face": cull_face,
                          "tint_index": face_data.get("tintindex", -1), "shade": part.get("shade", True)})
    return {**flags, "ambient_occlusion": model.get("ambientocclusion", True), "quads": quads}


def build_atlas(resources, texture_names, out, padding=2):
    if not 1 <= padding <= 16: raise AssetError("Atlas padding must be between 1 and 16")
    textures, animations = {}, []
    missing = Image.new("RGBA", (16, 16), (0, 0, 0, 255))
    for y in range(16):
        for x in range(16):
            if (x < 8) == (y < 8): missing.putpixel((x, y), (248, 0, 248, 255))
    textures["native:missing"] = missing
    for name in sorted(set(texture_names)):
        ns, path = identifier(name)
        filename = f"assets/{ns}/textures/{path}.png"
        if filename not in resources: raise AssetError(f"Missing texture: {filename}")
        image = Image.open(io.BytesIO(resources[filename])).convert("RGBA")
        if image.width > 4096 or image.height > 65536:
            raise AssetError(f"Texture dimensions too large: {filename}")
        metadata_name = filename + ".mcmeta"
        animation = json.loads(resources[metadata_name]).get("animation") if metadata_name in resources else None
        if animation is not None:
            # 1.8.9 TextureAtlasSprite divides a vertical strip into width-square frames.
            if image.height % image.width:
                raise AssetError(f"Animation strip height must be a multiple of width: {filename}")
            frame_count = image.height // image.width
            sequence = animation.get("frames") or list(range(frame_count))
            frame_time = animation.get("frametime", 1)
            if not isinstance(frame_time, int) or frame_time <= 0:
                raise AssetError(f"Animation frame time must be positive: {filename}")
            for frame in sequence:
                index = frame if isinstance(frame, int) else frame["index"]
                duration = frame_time if isinstance(frame, int) else frame.get("time", frame_time)
                if not isinstance(index, int) or not 0 <= index < frame_count or not isinstance(duration, int) or duration <= 0:
                    raise AssetError(f"Invalid animation frame index/time: {filename}")
            first = sequence[0] if isinstance(sequence[0], int) else sequence[0]["index"]
            if first < 0 or first >= frame_count: raise AssetError(f"Invalid first animation frame: {filename}")
            animations.append({"texture": name, "frames": frame_count, "first_frame": first,
                               "metadata": animation, "runtime": "first_frame_only"})
            image = image.crop((0, first * image.width, image.width, (first + 1) * image.width))
        textures[name] = image
    area = sum((im.width + padding * 2) * (im.height + padding * 2) for im in textures.values())
    width = 1
    while width < max(max(im.width for im in textures.values()) + padding * 2, math.sqrt(area) * 1.08): width *= 2
    placements = {}
    while True:
        x, y, row = 0, 0, 0
        placements.clear()
        for name, im in sorted(textures.items(), key=lambda item: (-item[1].height, -item[1].width, item[0])):
            w, h = im.width + padding * 2, im.height + padding * 2
            if x + w > width:
                x, y, row = 0, y + row, 0
            placements[name] = (x + padding, y + padding)
            x, row = x + w, max(row, h)
        needed = y + row
        if needed <= 16384 and width <= 16384: break
        width *= 2
        if width > 16384: raise AssetError("Texture pack exceeds maximum 16384-square atlas")
    height = 1
    while height < needed: height *= 2
    atlas_image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    mapping = {}
    for name, im in sorted(textures.items()):
        x, y = placements[name]
        atlas_image.paste(im, (x, y))
        atlas_image.paste(im.crop((0, 0, 1, im.height)).resize((padding, im.height), Image.Resampling.NEAREST), (x - padding, y))
        atlas_image.paste(im.crop((im.width - 1, 0, im.width, im.height)).resize((padding, im.height), Image.Resampling.NEAREST), (x + im.width, y))
        atlas_image.paste(im.crop((0, 0, im.width, 1)).resize((im.width, padding), Image.Resampling.NEAREST), (x, y - padding))
        atlas_image.paste(im.crop((0, im.height - 1, im.width, im.height)).resize((im.width, padding), Image.Resampling.NEAREST), (x, y + im.height))
        for xx, yy, px, py in ((x-padding, y-padding, 0, 0), (x+im.width, y-padding, im.width-1, 0),
                               (x-padding, y+im.height, 0, im.height-1), (x+im.width, y+im.height, im.width-1, im.height-1)):
            atlas_image.paste(im.getpixel((px, py)), (xx, yy, xx+padding, yy+padding))
        mapping[name] = {"x": x, "y": y, "width": im.width, "height": im.height,
                         "u0": (x + .01) / width, "v0": (y + .01) / height,
                         "u1": (x + im.width - .01) / width, "v1": (y + im.height - .01) / height}
    write_bytes(out / "atlas.rgba", atlas_image.tobytes())
    preview = io.BytesIO(); atlas_image.save(preview, format="PNG"); write_bytes(out / "atlas.png", preview.getvalue())
    write_json(out / "atlas.json", {"width": width, "height": height, "format": "RGBA8", "origin": "top_left",
                                    "padding": padding, "safe_generated_mipmap_levels": int(math.log2(padding)),
                                    "textures": mapping, "animations": animations})
    return mapping, width, height, animations


def cube(texture, start=(0, 0, 0), end=(16, 16, 16), cull=True, tint=-1):
    return {"textures": {"all": texture}, "elements": [{"from": start, "to": end,
            "faces": {face: {"texture": "#all", "tintindex": tint, **({"cullface": face} if cull else {})} for face in FACES}}]}


def builtin_model(number, meta):
    # These are explicitly tagged approximations; tile entities have separate renderers in Java.
    flags = {"approximate": number not in (0, 166), "occludes": False, "full_cube": False, "layer": 0}
    if number in (0, 166, 36): return {"textures": {}, "elements": []}, {}, flags
    if number in (8, 9, 10, 11):
        water = number in (8, 9)
        height = 1 - ((0 if meta >= 8 else meta) + 1) / 9
        flags["layer"] = 2 if water else 0
        return cube("blocks/water_still" if water else "blocks/lava_still", end=(16, height*16, 16), tint=0 if water else -1), {}, flags
    if number in (54, 130, 146):
        # Single chest exterior from ModelChest box sizes; chest lid animation and paired
        # chests still require tile entity handling. Texture uses original atlas layout.
        tex = "entity/chest/ender" if number == 130 else "entity/chest/trapped" if number == 146 else "entity/chest/normal"
        elements = []
        def box(start, size, offset):
            sx, sy, sz = size; u, v = offset; w, h = 64, 64
            rectangles = {
                "east": (u+sz+sx, v+sz, u+sz+sx+sz, v+sz+sy),
                "west": (u, v+sz, u+sz, v+sz+sy),
                "down": (u+sz+sx, v, u+sz+sx+sx, v+sz),
                "up": (u+sz, v, u+sz+sx, v+sz),
                "north": (u+sz, v+sz, u+sz+sx, v+sz+sy),
                "south": (u+sz+sx+sz, v+sz, u+sz+sx+sz+sx, v+sz+sy)}
            elements.append({"from": start, "to": [start[i]+size[i] for i in range(3)],
                             "faces": {face: {"texture": "#all", "uv": [a*16/(w if i%2==0 else h) for i,a in enumerate(uv)]}
                                       for face,uv in rectangles.items()}})
        box((1, 0, 1), (14, 10, 14), (0, 19))
        box((1, 10, 1), (14, 4, 14), (0, 0))
        box((7, 7, 0), (2, 4, 1), (0, 0))
        facing = FACES[meta % 6]
        return {"textures": {"all": tex}, "elements": elements}, {"y": {"north":0,"south":180,"east":90,"west":270}.get(facing,0)}, flags
    if number in (63, 68):
        model = cube("blocks/planks_oak", (0, 7, 7), (16, 15, 9), cull=False)
        if number == 63:
            model["elements"].extend(cube("blocks/planks_oak", (7, 0, 7), (9, 7, 9), cull=False)["elements"])
        return model, {"y": (meta // 4 * 90) % 360 if number == 63 else {2:0,3:180,4:270,5:90}.get(meta,0)}, flags
    if number == 119:
        flags["layer"] = 2
        return cube("entity/end_portal", (0, 11.99, 0), (16, 12, 16), cull=False), {}, flags
    if number == 144:
        return cube("blocks/soul_sand", (4, 0, 4), (12, 8, 12), cull=False), {}, flags
    if number in (176, 177):
        return cube("blocks/wool_colored_white", (2, 2, 7), (14, 16, 8), cull=False), {}, flags
    return cube("native:missing"), {}, flags


def decode_runtime_images(resources, out):
    required = {"steve": "entity/steve", "gui/icons": "gui/icons", "gui/widgets": "gui/widgets",
                "gui/inventory": "gui/container/inventory"}
    for target, texture in required.items():
        im = Image.open(io.BytesIO(resources[f"assets/minecraft/textures/{texture}.png"])).convert("RGBA")
        write_bytes(out / "runtime" / (target + ".rgba"), b"NVTX" + struct.pack("<II", im.width, im.height) + im.tobytes())
    gui_images = {}
    for name, data in sorted(resources.items()):
        match = re.fullmatch(r"assets/minecraft/textures/gui/(.+)\.png", name)
        if match:
            gui_images[match[1]] = data
    gui_aliases = {"minecraft": "title/minecraft", "mojang": "title/mojang",
                   "crafting": "container/crafting_table", "chest": "container/generic_54",
                   "inventory": "container/inventory", "brewing": "container/brewing_stand",
                   "enchanting": "container/enchanting_table"}
    for name in ("furnace", "dispenser", "hopper", "anvil", "beacon", "horse", "villager"):
        gui_aliases[name] = "container/" + name
    for i in range(6):
        gui_aliases[f"panorama_{i}"] = f"title/background/panorama_{i}"
    for alias, source in gui_aliases.items():
        if source in gui_images:
            gui_images[alias] = gui_images[source]
    renderer_images = {"sun": "environment/sun", "moon_phases": "environment/moon_phases",
                       "particles": "particle/particles", "glint": "misc/enchanted_item_glint",
                       "unknown_server": "misc/unknown_server", "explosion": "entity/explosion",
                       "footprint": "particle/footprint"}
    renderer_images.update({f"destroy_stage_{i}": f"blocks/destroy_stage_{i}" for i in range(10)})
    for alias, source in renderer_images.items():
        filename = f"assets/minecraft/textures/{source}.png"
        if filename in resources:
            gui_images[alias] = resources[filename]
    gui_index = []
    for name, data in sorted(gui_images.items()):
        im = Image.open(io.BytesIO(data)).convert("RGBA")
        write_bytes(out / "runtime/gui" / (name + ".rgba"), b"NVTX" + struct.pack("<II", im.width, im.height) + im.tobytes())
        gui_index.append({"name": name, "path": name + ".rgba", "width": im.width, "height": im.height})
    write_json(out / "runtime/gui/index.json", gui_index)
    write_bytes(out / "runtime/gui/index.txt", ("".join(e["name"] + "\t" + e["path"] + "\n" for e in gui_index)).encode("utf-8"))
    font = Image.open(io.BytesIO(resources["assets/minecraft/textures/font/ascii.png"])).convert("RGBA")
    write_bytes(out / "font.rgba", font.tobytes())
    glyph_sizes = resources["assets/minecraft/font/glyph_sizes.bin"]
    if len(glyph_sizes) != 65536:
        raise AssetError("1.8.9 glyph_sizes.bin must have exactly 65,536 bytes")
    write_bytes(out / "runtime/font/glyph_sizes.bin", glyph_sizes)
    write_bytes(out / "runtime/font/ascii.rgba", b"NVTX" + struct.pack("<II", font.width, font.height) + font.tobytes())
    unicode_index = []
    for name, data in sorted(resources.items()):
        match = re.fullmatch(r"assets/minecraft/textures/font/(unicode_page_([0-9a-fA-F]{2}))\.png", name)
        if not match:
            continue
        im = Image.open(io.BytesIO(data)).convert("RGBA")
        path = match[1].lower() + ".rgba"
        write_bytes(out / "runtime/font" / path, b"NVTX" + struct.pack("<II", im.width, im.height) + im.tobytes())
        unicode_index.append({"page": int(match[2], 16), "path": path, "width": im.width, "height": im.height})
    write_json(out / "runtime/font/index.json", unicode_index)
    write_bytes(out / "runtime/font/index.txt", ("".join(str(e["page"]) + "\t" + e["path"] + "\n" for e in unicode_index)).encode("utf-8"))
    entity_index = []
    for path, data in sorted(resources.items()):
        match = re.fullmatch(r"assets/([^/]+)/textures/(entity/.+)\.png", path)
        if not match:
            continue
        namespace, name = match.groups()
        im = Image.open(io.BytesIO(data)).convert("RGBA")
        target = namespace + "/" + name[len("entity/"):] + ".rgba"
        write_bytes(out / "runtime/entities" / target, b"NVTX" + struct.pack("<II", im.width, im.height) + im.tobytes())
        entity_index.append({"resource": namespace + ":" + name, "path": target, "width": im.width, "height": im.height})
    write_json(out / "runtime/entities/index.json", entity_index)
    write_bytes(out / "runtime/entities/index.txt", ("".join(e["resource"] + "\t" + e["path"] + "\n" for e in entity_index)).encode("utf-8"))
    write_json(out / "entity-model-report.json", {
        "decoded_entity_textures": len(entity_index),
        "decoded_rgba_bytes": sum(e["width"] * e["height"] * 4 for e in entity_index),
        "native_module": "src/entity_models.cpp",
        "source_geometry": ["ModelBox", "TexturedQuad", "ModelRenderer", "ModelBiped", "ModelPlayer",
                            "ModelZombie", "ModelSkeleton", "ModelEnderman", "ModelCreeper", "ModelQuadruped",
                            "ModelCow", "ModelPig", "ModelSheep2", "ModelSpider", "ModelChicken", "ModelVillager",
                            "ModelSlime", "ModelMagmaCube", "ModelGhast", "ModelSquid", "ModelSilverfish",
                            "ModelEnderMite", "ModelBlaze", "ModelIronGolem", "ModelSnowMan", "ModelWolf",
                            "ModelOcelot", "ModelHorse", "ModelRabbit", "ModelWitch", "ModelGuardian",
                            "ModelWither", "ModelDragon", "ModelArmorStand", "ModelBat", "ModelBoat", "ModelMinecart"],
        "supported_mob_protocol_ids": [30, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68,
                                       90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 120],
        "supported_object_protocol_ids": [1, 10, 78],
        "player_skins": ["Steve", "Alex"],
        "unsupported_type_behavior": "supported=false, empty geometry",
        "limits": ["Not all object protocol models", "Not all mob special behaviors", "Not all equipment/cape/name layers",
                   "Sheep wool, mooshroom mushrooms, snowman pumpkin, emissive overlays incomplete",
                   "Some entity-dependent special animations approximate; see docs/ASSETS.md",
                   "This module does not implement entity gameplay or networking"]})
    return font.width, font.height


def collect_blockstates(resources):
    result = {}
    for path, data in sorted(resources.items()):
        match = re.fullmatch(r"assets/([^/]+)/blockstates/(.+)\.json", path)
        if match:
            raw = json.loads(data)
            if "multipart" in raw: raise AssetError(f"Multipart blockstates are not a 1.8.9 format: {path}")
            variants = raw.get("variants", {})
            if not variants: raise AssetError(f"No variants in blockstate: {path}")
            result[f"{match[1]}:{match[2]}"] = {canonical_properties(key): value if isinstance(value, list) else [value]
                                                              for key, value in variants.items()}
    return result


def serialize_cache(path, dimensions, models, variants, states):
    result = io.BytesIO()
    result.write(b"NVASSET1")
    result.write(struct.pack("<7I", *dimensions, len(models), len(variants), len(states)))
    def string(value):
        data = value.encode("utf-8"); result.write(struct.pack("<I", len(data))); result.write(data)
    for model in models:
        flags = (int(model["full_cube"]) | int(model["occludes"]) << 1 | int(model["ambient_occlusion"]) << 2 | int(model["approximate"]) << 3)
        result.write(struct.pack("<BBHI", model["layer"], flags, 0, len(model["quads"])))
        for quad in model["quads"]:
            result.write(struct.pack("<bbhB3x3f", quad["face"], quad["cull_face"], quad["tint_index"], quad["shade"], *quad["normal"]))
            for vertex in quad["vertices"]: result.write(struct.pack("<5f", *vertex))
    for variant in variants:
        string(variant["resource"]); string(variant["properties"])
        result.write(struct.pack("<I", len(variant["choices"])))
        for model, weight in variant["choices"]: result.write(struct.pack("<II", model, weight))
    for state in states:
        string(state["name"]); string(state["resource"]); string(state["properties"])
        result.write(struct.pack("<I", state["variant_index"]))
    write_bytes(path, result.getvalue())


def inventory(mcp, out, registry, classes):
    source = mcp / "src/minecraft"
    areas, files = Counter(), []
    for path in sorted(source.rglob("*.java")):
        rel = path.relative_to(source).as_posix()
        parts = rel.split("/")
        area = parts[2] if parts[:2] == ["net", "minecraft"] and len(parts) > 3 else "/".join(parts[:-1]) or "default"
        if area == "client" and len(parts) > 4: area += "/" + parts[3]
        data = path.read_bytes()
        areas[area] += 1
        files.append({"path": rel, "area": area, "sha1": sha1(data), "bytes": len(data), "lines": data.count(b"\n")})
    coverage = {
        "assets": {"native": "Verified resource import; JSON block models; GUI textures; Unicode metrics; source-derived item icons; native animation cache; see docs/RENDER_ASSETS.md",
                   "not_complete": ["All dynamic item/NBT render layers", "All tile-entity renderers", "All audio game triggers", "Complete complex-text layout", "Live resource-pack reload"]},
        "client/gui": {"native": "Original GUI PNGs, verified Unicode metrics, item icons, player/chest/creative inventory, anvil naming, merchant navigation, enchanting choices, brewing progress and beacon controls; see docs/GUI.md",
                       "not_complete": ["All original menu behavior and special GUI animations", "Arabic shaping and bidi layout", "All special inventory item effects"]},
        "client/audio": {"native": "SoundRegistry 1.8.9 event resolution, bounded Vorbis decoding, optional bundled OpenAL spatial playback and streaming; see docs/AUDIO.md",
                         "not_complete": ["MusicTicker automatic scheduling", "ITickableSound/entity-linked source updates", "Full repeat/delay behavior", "EAX/reverb extensions", "Audible hardware validation"]},
        "client/renderer": {"native": "See main project README; this inventory does not assert renderer parity"},
        "block": {"native": "Render state metadata mapping for registered IDs 0..197; world/physics module is separate", "not_complete": ["Full block gameplay logic"]},
        "network": {"native": "Separate protocol module; consult its tests and documented limits"},
        "world": {"native": "Separate world module; consult its tests and documented limits", "not_complete": ["Vanilla terrain generation", "Integrated server simulation"]},
        "entity": {"native": "EntityModels ports ModelBox UV/winding and 33 mob IDs plus Steve/Alex and three object types; see entity-model-report.json",
                   "not_complete": ["All mob/object families", "Equipment and special render layers", "Entity gameplay and AI", "Full animation/state parity"]},
        "other": {"native": "Unported unless specifically documented in module docs"}}
    write_json(out / "source-inventory.json", {"source_root": str(source.resolve()) if source.is_dir() else None,
                                               "registry_source": "MCP source" if classes else "tools/reference/asset_constants.json",
                                               "java_files": len(files),
                                               "java_lines": sum(x["lines"] for x in files), "areas": dict(sorted(areas.items())),
                                               "coverage": coverage, "files": files})
    write_json(out / "block-registry.json", [dict(block, **block_flags(block, classes)) for block in registry.values()])


def prepare(mcp, out, packs=(), allow_missing=False, padding=2):
    mcp, out = Path(mcp).resolve(), Path(out).resolve()
    if out == mcp or out.is_relative_to(mcp):
        raise AssetError("Asset output must be outside the original MCP source tree")
    out.mkdir(parents=True, exist_ok=True)
    resources, import_report = import_resources(mcp, out, list(packs), allow_missing)
    prepare_audio_runtime(mcp, out)
    registry, classes = read_registry(mcp / "src/minecraft")
    blockstates, resolver = collect_blockstates(resources), ModelResolver(resources)
    states, routes, resource_flags = [], {}, {}
    mapping_errors = []
    for number, block in registry.items():
        flags = block_flags(block, classes)
        for meta in range(16):
            resource, properties, invalid = state_mapping(number, meta, block["name"])
            state = {"state": number << 4 | meta, "block_id": number, "metadata": meta, "name": "minecraft:" + block["name"],
                     "resource": resource, "properties": properties, "invalid_metadata": invalid}
            states.append(state)
            if number not in BUILTINS:
                resource_flags[resource] = flags
                if resource not in blockstates or properties not in blockstates[resource]:
                    mapping_errors.append({**state, "error": "No matching JSON variant"})
            routes[number << 4 | meta] = (resource, properties)
    if mapping_errors:
        write_json(out / "model-errors.json", mapping_errors)
        raise AssetError(f"{len(mapping_errors)} unmapped state variants; see model-errors.json")
    write_json(out / "model-errors.json", [])
    # Every declared variant is baked, including neighbor-connected forms.
    options, texture_names = [], set()
    for resource, entries in sorted(blockstates.items()):
        flags = resource_flags.get(resource, {"layer": 0, "occludes": False, "full_cube": False, "approximate": False})
        for properties, alternatives in sorted(entries.items()):
            for alternative in alternatives:
                ns, modelpath = identifier(alternative["model"])
                # ModelBlockDefinition.Deserializer uses a block/ prefix for blockstates.
                if not modelpath.startswith("block/"): modelpath = "block/" + modelpath
                model = resolver.resolve(ns + ":" + modelpath)
                for part in model.get("elements", []):
                    for face in part.get("faces", {}).values(): texture_names.add(resolver.texture(model, face["texture"]))
                options.append((resource, properties, alternative, model, flags))
    for number in sorted(BUILTINS):
        for meta in range(16):
            model, option, flags = builtin_model(number, meta)
            for part in model.get("elements", []):
                for face in part.get("faces", {}).values(): texture_names.add(resolver.texture(model, face["texture"]))
            resource, properties = routes[number << 4 | meta]
            options.append((resource, properties, option, model, flags))
    # Include all block sprites, not only the subset reached by current JSON.
    for path in resources:
        match = re.fullmatch(r"assets/([^/]+)/textures/(blocks/.+)\.png", path)
        if match: texture_names.add(f"{match[1]}:{match[2]}")
    texture_names.discard("native:missing")
    atlas, width, height, animations = build_atlas(resources, texture_names, out, padding)
    font_width, font_height = decode_runtime_images(resources, out)
    models = [bake_model(cube("native:missing"), {}, atlas,
                         {"layer": 0, "occludes": True, "full_cube": True, "approximate": True})]
    variants = [{"resource": "native:missing", "properties": "normal", "choices": [(0, 1)]}]
    variant_indices, model_indices = {("native:missing", "normal"): 0}, {}
    for resource, properties, option, model, flags in options:
        key = json.dumps((model, option.get("x", 0), option.get("y", 0), option.get("uvlock", False), flags), sort_keys=True)
        if key not in model_indices:
            model_indices[key] = len(models)
            models.append(bake_model(model, option, atlas, flags))
        model_index = model_indices[key]
        variant_key = (resource, properties)
        if variant_key not in variant_indices:
            variant_indices[variant_key] = len(variants)
            variants.append({"resource": resource, "properties": properties, "choices": []})
        weight = option.get("weight", 1)
        if not isinstance(weight, int) or weight <= 0 or weight > 0x7fffffff:
            raise AssetError("Model weight must be a positive Java int")
        variants[variant_indices[variant_key]]["choices"].append((model_index, weight))
    for variant in variants:
        # WeightedBakedModel.MyWeighedRandomItem.compareTo uses descending weight,
        # then ascending total quad count. Python sort is stable like Java's.
        variant["choices"].sort(key=lambda pair: (-pair[1], len(models[pair[0]]["quads"])))
        if sum(pair[1] for pair in variant["choices"]) > 0x7fffffff:
            raise AssetError("Combined model weights overflow Java int")
    for state in states:
        state["variant_index"] = variant_indices[(state["resource"], state["properties"])]
        choices = variants[state["variant_index"]]["choices"]
        state["approximate"] = any(models[i]["approximate"] for i, _ in choices)
    serialize_cache(out / "assets.bin", (width, height, font_width, font_height), models, variants, states)
    write_json(out / "state-models.json", states)
    write_json(out / "model-variants.json", variants)
    inventory(mcp, out, registry, classes)
    report = {"block_ids": len(registry), "metadata_entries": len(states), "blockstate_resources": len(blockstates),
              "variants": len(variants), "baked_models": len(models), "quads": sum(len(m["quads"]) for m in models),
              "atlas": [width, height], "textures": len(atlas), "animated_textures_preserved": len(animations),
              "animation_playback": "first_frame_only", "unresolved_models": [], "unresolved_states": [],
              "invalid_metadata_fallbacks": sum(s["invalid_metadata"] for s in states),
              "built_in_renderers_approximated": [registry[i]["name"] for i in sorted(BUILTINS - {0, 166})],
              "actual_state": "All JSON variants baked. Caller must supply neighbor/tile-entity properties through modelWithProperties; base defaults alone are insufficient.",
              "terrain_cache_sha1": sha1((out / "assets.bin").read_bytes()), "atlas_sha1": sha1((out / "atlas.rgba").read_bytes())}
    write_json(out / "model-report.json", report)
    print(json.dumps({"import": {k:v for k,v in import_report.items() if k not in ("resource_packs",)}, "models": report}, ensure_ascii=False, indent=2))
    return report


def main():
    project = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcp", type=Path, default=project.parent / "MCP-919-main")
    parser.add_argument("--output", type=Path, default=project / "assets")
    parser.add_argument("--pack", action="append", type=Path, default=[], help="1.8.9 resource pack directory/ZIP; later packs have higher priority")
    parser.add_argument("--allow-missing", action="store_true", help="Preserve report and continue past missing/corrupt indexed sounds/languages")
    parser.add_argument("--padding", type=int, default=2)
    args = parser.parse_args()
    try:
        prepare(args.mcp, args.output, args.pack, args.allow_missing, args.padding)
    except (AssetError, ValueError, OSError, zipfile.BadZipFile) as error:
        print(f"Asset preparation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
