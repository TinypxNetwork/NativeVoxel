#!/usr/bin/env python3
"""Compare native digging hardness and inventory predictions with MCP Java."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import struct
import subprocess


def run(arguments: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    environment = os.environ.copy()
    environment["PATH"] = r"D:\Strawberry\c\bin" + os.pathsep + environment.get("PATH", "")
    result = subprocess.run(arguments, cwd=cwd, text=True, capture_output=True, env=environment)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result


def fixtures(path: Path) -> dict:
    lines = []
    for block in range(1, 198):
        for tool in (-1, 257, 270, 274, 278, 285, 256, 258, 267, 276, 359):
            for setting in ((0, -1, -1, 0, 1, 0), (3, 1, -1, 0, 1, 0),
                            (5, -1, 0, 1, 0, 0), (4, 2, 1, 1, 1, 1), (0, 0, 3, 0, 0, 0)):
                lines.append("H " + " ".join(map(str, (block, 0, tool, *setting))))
    hardness = len(lines)
    root=path.parents[2]
    exported=json.loads((root/"build/crafting_export/crafting.json").read_text(encoding="utf-8"))
    recipe_cases=0
    def recipe_case(grid: list[dict], size: int) -> None:
        nonlocal recipe_cases
        lines.append("R "+str(size)+" "+str(size)+" "+" ".join(str(value) for stack in grid for value in
            (stack["id"],0 if stack["id"]<0 else 1,0 if stack["damage"]==32767 else stack["damage"])))
        recipe_cases+=1
    empty={"id":-1,"damage":0}
    for recipe in exported["recipes"]:
        if recipe["kind"]=="ShapedRecipes":
            width,height=recipe["width"],recipe["height"]
            for size in (2,3):
                if width>size or height>size:continue
                for ox in range(size-width+1):
                    for oy in range(size-height+1):
                        for mirror in (False,True):
                            grid=[empty]*(size*size)
                            for y in range(height):
                                for x in range(width):grid[ox+x+(oy+y)*size]=recipe["inputs"][(width-1-x if mirror else x)+y*width]
                            recipe_case(grid,size)
        elif recipe["kind"]=="ShapelessRecipes":
            for size in (2,3):
                if len(recipe["inputs"])>size*size:continue
                for variant in range(2):
                    grid=recipe["inputs"]+[empty]*(size*size-len(recipe["inputs"]))
                    random.Random(recipe["index"]+variant).shuffle(grid)
                    recipe_case(grid,size)
    # These streams call the original Container.slotClick and compare its return
    # value, cursor, and every slot after every click. Crafting slots are excluded.
    for container in (0, 9, 27, 54,"workbench","furnace","brewing","enchant","beacon","horse","donkey"):
        lines += ["W", f"C {container}"]
        counts={"workbench":10,"furnace":3,"brewing":4,"enchant":2,"beacon":1,"horse":2,"donkey":17}
        size = 45 if container == 0 else counts.get(container,container) + 36
        first = 9 if container == 0 else (1 if container=="workbench" else 0)
        lines += [f"S {first} 1 31 0", f"I {first} 1 0", f"I {first+1} 1 0", f"I {first+2} 0 0",
                  f"I {first} 0 1", "K 5 10 2", "I -999 0 5", f"I {first+3} 1 5", f"I {first+4} 1 5", "I -999 2 5", "K -1 0 0"]
        rng = random.Random("controller:"+str(container))
        for _ in range(150):
            slot = rng.randrange(first, size)
            if rng.random() < .3:
                item = rng.choice((1, 3, 5, 264, 267, 310, 332,263,265,351,372,373,374,329,417))
                count = 1 if item in (267, 310,373,329,417) else rng.randrange(1, 17)
                lines.append(f"S {slot} {item} {count} 0")
            mode = rng.choice((0, 0, 0, 1, 2, 4, 6))
            button = rng.randrange(9) if mode == 2 else rng.randrange(2)
            lines.append(f"I {slot} {button} {mode}")
    lines += ["W","C 0","S 1 17 10 0","I 0 1 0","I 0 0 0","K -1 0 0","I 0 0 1",
              "W","C 0","S 1 5 4 2","S 2 5 4 0","S 3 5 4 1","S 4 5 4 3","I 0 1 0","K -1 0 0","I 0 0 1",
              "W","C workbench"]
    for index,item in enumerate((335,335,335,353,344,353,296,296,296),1):lines.append(f"S {index} {item} 1 0")
    lines += ["I 0 1 0","K -1 0 0","I 1 0 0","I 3 1 0"]
    def mutf(value: str) -> bytes:
        units=value.encode("utf-16-be");out=bytearray()
        for at in range(0,len(units),2):
            unit=int.from_bytes(units[at:at+2],"big")
            if 0<unit<128:out.append(unit)
            elif unit<2048:out.extend((0xc0|(unit>>6),0x80|(unit&63)))
            else:out.extend((0xe0|(unit>>12),0x80|((unit>>6)&63),0x80|(unit&63)))
        return struct.pack(">H",len(out))+out
    def payload(kind: int,value) -> bytes:
        if kind in (1,2,3,4):return struct.pack({1:">b",2:">h",3:">i",4:">q"}[kind],value)
        if kind==8:return mutf(value)
        if kind==9:
            child,values=value;return bytes([child])+struct.pack(">i",len(values))+b"".join(payload(child,v) for v in values)
        if kind==10:return b"".join(bytes([tag[0]])+mutf(key)+payload(*tag) for key,tag in value.items())+b"\0"
        raise ValueError(kind)
    def tag(value: dict | None) -> str:return "-" if value is None else (b"\x0a\0\0"+payload(10,value)).hex()
    def stack(id: int,count=1,damage=0,nbt=None)->str:return f"{id} {count if id>=0 else 0} {damage} {tag(nbt)}"
    def ench(id: int,level: int,book=False)->dict:return {"StoredEnchantments" if book else "ench":(9,(10,[{"id":(2,id),"lvl":(2,level)}]))}
    none=stack(-1)
    cases=[
        (stack(257,1,200),stack(265,8),"",0),
        (stack(257,1,200),stack(257,1,180),"",0),
        (stack(257),stack(265,4),"",0),
        (stack(278,1,100),stack(265),"",0),
        (stack(339),none,"Test",0),
        (stack(339,nbt={"display":(10,{"Name":(8,"Old")}),"RepairCost":(3,3)}),none,"",0),
        (stack(339,nbt={"display":(10,{"Name":(8,"Old")}),"RepairCost":(3,3)}),none,"Old",0),
        (stack(339,nbt={"RepairCost":(3,100)}),none,"Renamed",0),
        (stack(257,1,200,{"RepairCost":(3,39)}),stack(265),"",0),
        (stack(257,1,200,{"RepairCost":(3,39)}),stack(265),"",1),
        (stack(278,1,100,ench(32,2)),stack(403,nbt=ench(32,2,True)),"",0),
        (stack(278,1,100,ench(33,1)),stack(403,nbt=ench(35,3,True)),"",0),
        (stack(403,nbt=ench(16,2,True)),stack(403,nbt=ench(16,2,True)),"",0),
        (stack(339),none,"\u5de5\u5177\U0001f600",0),
        (stack(257,1,200,{"Unbreakable":(1,1)}),stack(257,1,100),"",0),
        (stack(310,1,300,ench(0,2)),stack(403,nbt=ench(0,2,True)),"Helmet",0),
    ]
    for first,second,name,creative in cases:lines.append(f"A {first} {second} {name.encode('utf-8').hex() or '-'} {creative}")
    advanced_start=len(lines)
    lines += ["W","C anvil","X 30","S 0 257 1 200","S 1 265 8 0","P","I 2 1 0","P",
              "W","C anvil","X 0","S 0 257 1 200","S 1 265 4 0","P","I 2 0 0","P","X 30","I 2 0 1","P",
              "W","C anvil","X 30","S 3 257 1 200","I 3 0 1","S 4 265 3 0","I 4 0 1","P","N 4e616d65","P","I 2 0 0","P",
              "W","C merchant",f"O {stack(388,2)} {none} {stack(264)} 0 3",f"O {stack(388)} {stack(340)} {stack(403)} 0 2",
              "S 0 388 8 0","P","I 2 1 0","P","K -1 0 0","I 2 0 1","P","J 1","S 0 388 2 0","S 1 340 2 0","P","I 2 0 1","P",
              "W","C merchant",f"O {stack(388,2)} {stack(340)} {stack(264,3)} 0 7","S 0 340 3 0","S 1 388 6 0","P","I 2 1 0","P",
              "W","C merchant",f"O {stack(340,1,0,{'test':(3,7)})} {none} {stack(388)} 0 1",
              f"S 0 340 1 0 {tag({'test':(3,7),'extra':(8,'ok')})}","P","I 2 0 0","P",
              "W","C enchant","X 30","S 0 278 1 0","S 1 351 10 4","V 0 5","V 1 15","V 2 30","E 0","E 2","X 1","E 2","G 1","E 2"]
    path.write_text("\n".join(lines) + "\n", encoding="ascii")
    return {"hardness_cases": hardness,"recipe_cases":recipe_cases, "inventory_clicks": sum(line.startswith("I ") for line in lines),
            "anvil_nbt_cases":len(cases),"advanced_snapshots":sum(line.startswith(("P","E")) for line in lines[advanced_start:])}


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcp", type=Path, default=root.parent / "MCP-919-main")
    parser.add_argument("--native", type=Path, default=root / "tests/controller_tests.exe")
    args = parser.parse_args()
    mcp = args.mcp.resolve()
    build = root / "build/controller_reference"
    classes = build / "classes"
    classes.mkdir(parents=True, exist_ok=True)
    physics = root / "build/physics_reference"
    if not (physics / "PhysicsReference.class").exists():
        raise RuntimeError("Run tools/verify_physics.py first to compile its reusable headless world/player adapter")
    classpath = os.pathsep.join(map(str, [classes, physics, physics / "oracle_classes", root / "build/registry_export/classes",
        mcp / "bin/minecraft", mcp / "jars/versions/1.8.9/1.8.9.jar", *sorted((mcp / "jars/libraries").rglob("*.jar"))]))
    source = mcp / "src/minecraft/net/minecraft"
    originals = [source / ("inventory/"+name+".java") for name in ("Container","ContainerPlayer","ContainerChest","ContainerWorkbench","ContainerFurnace",
        "ContainerBrewingStand","ContainerEnchantment","ContainerBeacon","ContainerHorseInventory","ContainerRepair","ContainerMerchant","InventoryMerchant","SlotMerchantResult","Slot","SlotCrafting","SlotFurnaceFuel","SlotFurnaceOutput",
        "InventoryCrafting","InventoryCraftResult")]+[source/"entity/player/InventoryPlayer.java",source/"item/ItemStack.java",
            source/"tileentity/TileEntityBrewingStand.java",source/"tileentity/TileEntityFurnace.java",source/"tileentity/TileEntityBeacon.java",
            source/"village/MerchantRecipe.java",source/"village/MerchantRecipeList.java",source/"entity/NpcMerchant.java",source/"enchantment/EnchantmentHelper.java"]
    run(["javac", "-encoding", "UTF-8", "--release", "8", "-cp", classpath, "-d", str(classes), *map(str, originals),
         str(root / "tests/java/ControllerReference.java")], build)
    fixture = build / "controller.fixture"
    counts = fixtures(fixture)
    run(["java", "-Djava.awt.headless=true", "-cp", classpath, "ControllerReference", str(fixture), str(build / "java.trace")], build)
    native = run([str(args.native.resolve()), "--trace", str(fixture)], root).stdout
    (build / "native.trace").write_text(native, encoding="ascii")
    expected = (build / "java.trace").read_text(encoding="ascii").splitlines()
    actual = native.splitlines()
    if len(expected) != len(actual):
        raise RuntimeError(f"Trace row count differs: Java={len(expected)}, native={len(actual)}")
    differences = [{"row": index, "java": java, "native": cpp} for index, (java, cpp) in enumerate(zip(expected, actual)) if java != cpp]
    report = {"oracle": "original MCP Block/ItemStack/InventoryPlayer/Container/Slot methods", "counts": counts,
              "rows": len(expected), "mismatches": len(differences), "failures": differences[:30],
              "source_sha256": {str(path.relative_to(mcp)): hashlib.sha256(path.read_bytes()).hexdigest() for path in originals}}
    (build / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    if differences:
        for difference in differences[:5]:
            print(json.dumps(difference))
        raise RuntimeError(f"{len(differences)} Java/native mismatches; see {build / 'report.json'}")
    print(f"PASS {len(expected)} exact Java/native rows; {counts}")


if __name__ == "__main__":
    main()
