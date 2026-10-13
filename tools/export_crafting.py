#!/usr/bin/env python3
"""Export the supplied 1.8.9 Java CraftingManager into checked-in native data."""
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


def prototype(stack: dict) -> str:
    return "{" + ",".join((str(stack["id"]), str(stack["count"]), str(stack["damage"]), json.dumps(stack["nbt"]))) + "}"


def header(data: dict, output: Path) -> dict:
    kinds = {"ShapedRecipes": 0, "ShapelessRecipes": 1, "RecipeRepairItem": 2}
    lines = ["#pragma once", "// Generated from original MCP crafting/item/furnace getters by tools/export_crafting.py.",
             "namespace mc::crafting::data {", "struct Stack { int id,count,damage; const char* nbt; };",
             "struct Ingredient { int id,damage; };",
             "struct Recipe { int kind,width,height,count; bool copyNbt; const char* name; std::array<Ingredient,9> ingredients; Stack output; };",
             "struct Smelting { Ingredient input; Stack output; };",
             f"inline constexpr std::array<Recipe,{len(data['recipes'])}> recipes{{{{"]
    ordinary = 0
    special = []
    for recipe in data["recipes"]:
        kind = kinds.get(recipe["kind"], 3)
        if kind < 2:
            ordinary += 1
        elif kind == 3:
            special.append(recipe["kind"])
        inputs = ["{" + str(stack["id"]) + "," + str(stack["damage"]) + "}" for stack in recipe["inputs"]]
        inputs += ["{-1,0}"] * (9 - len(inputs))
        lines.append("    {" + ",".join((str(kind), str(recipe["width"]), str(recipe["height"]), str(len(recipe["inputs"])),
            "true" if recipe["copy_nbt"] else "false", json.dumps(recipe["kind"]), "{{" + ",".join(inputs) + "}}", prototype(recipe["output"]))) + "},")
    lines += ["}};", f"inline constexpr std::array<Smelting,{len(data['smelting'])}> smelting{{{{"]
    for recipe in data["smelting"]:
        ingredient = recipe["input"]
        lines.append("    {{" + str(ingredient["id"]) + "," + str(ingredient["damage"]) + "}," + prototype(recipe["output"]) + "},")
    size = max(value["id"] for value in data["items"]) + 1
    lines += ["}};", "struct ItemRule { int container{-1}; std::array<int,16> fuel{}; unsigned potionMask{}; std::array<int,8> repairMaterials{}; int repairCount{},enchantability{}; };",
              "inline constexpr auto items=[]() constexpr {", f"    std::array<ItemRule,{size}> result{{}};"]
    for value in data["items"]:
        if len(value["repairs"]) > 8:
            raise RuntimeError("native repair material capacity exceeded")
        repairs = value["repairs"] + [-1] * (8 - len(value["repairs"]))
        lines.append(f"    result[{value['id']}]={{" + str(value["container"]) + ",{{" + ",".join(map(str,value["fuel"])) + "}}," + str(value["potion_mask"]) + ",{{" +
                     ",".join(map(str, repairs)) + "}}," + str(len(value["repairs"])) + "," + str(value["enchantability"]) + "};")
    lines += ["    return result;", "}();", "struct EnchantmentRule { bool valid{}; int maxLevel{},weight{}; std::array<std::uint64_t,36> items{}; std::array<std::uint64_t,4> compatible{}; };",
              "inline constexpr auto enchantments=[]() constexpr {", "    std::array<EnchantmentRule,256> result{};"]
    for value in data["enchantments"]:
        applies=[0]*36; compatible=[0]*4
        for item in value["applies"]:applies[item//64]|=1<<(item%64)
        for other in value["compatible"]:compatible[other//64]|=1<<(other%64)
        lines.append(f"    result[{value['id']}]={{true,{value['max_level']},{value['weight']},{{{{"+",".join(str(x)+"ull" for x in applies)+"}},{{"+",".join(str(x)+"ull" for x in compatible)+"}}};")
    lines += ["    return result;", "}();", f"inline constexpr std::size_t ordinaryCount={ordinary};", f"inline constexpr std::size_t specialCount={len(special)};",
              "} // namespace mc::crafting::data", ""]
    output.write_text("\n".join(lines), encoding="ascii")
    return {"ordinary_recipes": ordinary, "repair_recipe": 1, "special_recipes_pending": special, "smelting_recipes": len(data["smelting"])}


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcp", type=Path, default=root.parent / "MCP-919-main")
    args = parser.parse_args()
    mcp = args.mcp.resolve()
    build = root / "build/crafting_export"
    classes = build / "classes"
    classes.mkdir(parents=True, exist_ok=True)
    classpath = os.pathsep.join(map(str, [classes, root / "build/registry_export/classes", mcp / "bin/minecraft",
        mcp / "jars/versions/1.8.9/1.8.9.jar", *sorted((mcp / "jars/libraries").rglob("*.jar"))]))
    source = mcp / "src/minecraft/net/minecraft"
    originals = [*sorted((source / "item/crafting").glob("*.java")), *sorted((source / "enchantment").glob("*.java")), source / "tileentity/TileEntityFurnace.java"]
    arguments = ["-encoding", "UTF-8", "--release", "8", "-cp", classpath, "-d", str(classes), *map(str, originals), str(root / "tests/java/CraftingExport.java")]
    argument_file = build / "javac.args"
    argument_file.write_text("\n".join('"' + argument.replace("\\", "/").replace('"', '\\"') + '"' for argument in arguments), encoding="utf-8")
    run(["javac", "@" + str(argument_file)], build)
    raw = build / "crafting.json"
    run(["java", "-Djava.awt.headless=true", "-cp", classpath, "CraftingExport", str(raw)], build)
    output = root / "include/mc/crafting_data.hpp"
    report = header(json.loads(raw.read_text(encoding="utf-8")), output)
    report["source_sha256"] = {str(path.relative_to(mcp)): hashlib.sha256(path.read_bytes()).hexdigest() for path in originals}
    report["header_sha256"] = hashlib.sha256(output.read_bytes()).hexdigest()
    (build / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key != "source_sha256"}, indent=2))


if __name__ == "__main__":
    main()
