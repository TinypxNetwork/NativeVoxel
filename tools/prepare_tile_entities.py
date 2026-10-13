#!/usr/bin/env python3
"""Export vanilla banner IDs/MapColors and block-damage textures for native tiles."""
from pathlib import Path
import argparse,hashlib,io,json,re,struct
from PIL import Image
import prepare_assets as base
from prepare_render_assets import string,nvtx


def main():
    root=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--mcp",type=Path,default=root.parent/"MCP-919-main");parser.add_argument("--assets",type=Path,default=root/"assets");args=parser.parse_args();mcp=args.mcp.resolve();assets=args.assets.resolve()
    source=mcp/"src/minecraft/net/minecraft";banner=source/"tileentity/TileEntityBanner.java";dye=source/"item/EnumDyeColor.java";mapcolor=source/"block/material/MapColor.java"
    if banner.is_file() and dye.is_file() and mapcolor.is_file():
        patterns=re.findall(r'^\s*[A-Z_]+\("([a-z_]+)",\s*"([a-z]+)"',banner.read_text(encoding="utf-8"),re.M)
        colors={name:int(value) for name,value in re.findall(r'MapColor\s+(\w+)\s*=\s*new MapColor\(\d+,\s*(\d+)\)',mapcolor.read_text(encoding="utf-8"))}
        palette=[None]*16
        for damage,name in re.findall(r'^\s*[A-Z_]+\(\d+,\s*(\d+),[^\n]*?MapColor\.(\w+)',dye.read_text(encoding="utf-8"),re.M):palette[int(damage)]=colors[name]
    else:
        patterns=base.asset_constants()["banner_patterns"]
        palette=base.asset_constants()["dye_map_colors"]
    if len(patterns)!=39 or any(value is None for value in palette):raise RuntimeError("Original banner pattern/dye registry is incomplete")
    resources=assets/"resources/assets/minecraft/textures";basePng=(resources/"entity/banner_base.png").read_bytes()
    if basePng[24]!=8 or basePng[25] not in (2,6):raise RuntimeError("Native banner base currently requires 8-bit RGB or RGBA PNG")
    out=io.BytesIO();out.write(b"NVTILE01");out.write(struct.pack("<I16I",len(patterns),*palette));metadata=[]
    for name,id in patterns:
        encoded=(resources/f"entity/banner/{name}.png").read_bytes();type6=encoded[24]==8 and encoded[25]==6
        string(out,id);string(out,"banner/"+name);out.write(struct.pack("<I",type6));metadata.append({"id":id,"name":name,"java_type6":type6})
    target=assets/"runtime/tiles";base.write_bytes(target/"patterns.bin",out.getvalue())
    for stage in range(10):base.write_bytes(target/f"destroy_stage_{stage}.rgba",nvtx(Image.open(resources/f"blocks/destroy_stage_{stage}.png")))
    report={"patterns":metadata,"dye_damage_map_colors":palette,"source_sha256":{p.name:base.source_hash(mcp,p.relative_to(source).as_posix()) for p in (banner,dye,mapcolor)},"cache_sha256":hashlib.sha256(out.getvalue()).hexdigest()}
    base.write_json(target/"patterns.json",report);print(f"Prepared {len(patterns)} source banner patterns, 16 MapColors and 10 damage textures")


if __name__=="__main__":main()
