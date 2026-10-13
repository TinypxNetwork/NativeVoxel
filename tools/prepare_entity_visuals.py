#!/usr/bin/env python3
"""Stage original armor/glint RGBA and export source potion liquid colors."""
from pathlib import Path
import argparse,hashlib,json,re
from PIL import Image
import prepare_assets as base
from prepare_render_assets import nvtx

def main():
    root=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__);p.add_argument("--mcp",type=Path,default=root.parent/"MCP-919-main");p.add_argument("--assets",type=Path,default=root/"assets");args=p.parse_args();assets=args.assets.resolve();src=assets/"resources/assets/minecraft/textures";out=assets/"runtime/equipment";entries=[]
    for path in sorted((src/"models/armor").glob("*.png")):
        name="minecraft:models/armor/"+path.stem;filename=path.stem+".rgba";base.write_bytes(out/filename,nvtx(Image.open(path)));entries.append(name+"\t"+filename)
    base.write_bytes(out/"enchanted_item_glint.rgba",nvtx(Image.open(src/"misc/enchanted_item_glint.png")));entries.append("minecraft:misc/enchanted_item_glint\tenchanted_item_glint.rgba")
    base.write_bytes(out/"index.txt",("\n".join(entries)+"\n").encode())
    potion=args.mcp/"src/minecraft/net/minecraft/potion/Potion.java";colors=[-1]*32
    if potion.is_file():
        for id,color in re.findall(r'new Potion\w*\((\d+),\s*new ResourceLocation\("[^"]+"\),\s*(?:true|false),\s*(\d+)\)',potion.read_text(encoding="utf-8")):colors[int(id)]=int(color)
    else:
        colors=base.asset_constants()["potion_colors"]
    if sum(c>=0 for c in colors)!=23:raise RuntimeError("Incomplete source potion liquid color registry")
    base.write_bytes(root/"src/entity_visual_data.inc",("// Generated from original Potion.java by prepare_entity_visuals.py\nconstexpr int potionLiquidColors[32]={"+",".join(map(str,colors))+"};\n").encode())
    base.write_json(out/"report.json",{"textures":len(entries),"potion_colors":colors,"source_sha256":base.source_hash(args.mcp,"potion/Potion.java")})
    print(f"Prepared {len(entries)} armor/glint textures and 23 source potion colors")

if __name__=="__main__":main()
