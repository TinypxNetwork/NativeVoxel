#!/usr/bin/env python3
"""Capture original Java LayeredColorMaskTexture composites without OpenGL."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess

def main():
    root=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--mcp",type=Path,default=root.parent/"MCP-919-main");args=parser.parse_args();mcp=args.mcp.resolve()
    build=root/"build/tile_entity_reference";original=build/"banner_original";classes=build/"banner_classes";original.mkdir(parents=True,exist_ok=True);classes.mkdir(parents=True,exist_ok=True)
    tail=[mcp/"bin/minecraft",mcp/"jars/versions/1.8.9/1.8.9.jar",*sorted((mcp/"jars/libraries").rglob("*.jar"))];source=mcp/"src/minecraft/net/minecraft"
    names=["client/renderer/texture/LayeredColorMaskTexture.java","util/MathHelper.java","item/EnumDyeColor.java","block/material/MapColor.java"]
    cp=os.pathsep.join(map(str,[original,*tail]));subprocess.run(["javac","-encoding","UTF-8","--release","8","-cp",cp,"-d",str(original),*[str(source/name) for name in names]],cwd=build,check=True)
    cp=os.pathsep.join(map(str,[classes,original,*tail]));subprocess.run(["javac","-encoding","UTF-8","--release","8","-cp",cp,"-d",str(classes),*map(str,sorted((root/"tests/java/banner_stub").rglob("*.java"))),str(root/"tests/java/BannerReference.java")],cwd=build,check=True)
    target=root/"tools/reference/banner_reference.bin";subprocess.run(["java","-Djava.awt.headless=true","-cp",cp,"BannerReference",str(root/"assets"),str(target)],cwd=build,check=True)
    report={"source_sha256":{name:hashlib.sha256((source/name).read_bytes()).hexdigest() for name in names},"fixture_sha256":hashlib.sha256(target.read_bytes()).hexdigest(),"method":"Unmodified LayeredColorMaskTexture.loadTexture with ImageIO/AWT; only texture GPU upload is replaced. Vanilla patterns, all MapColors, layered ordering/cap, partial alpha and ignored masks."}
    (target.parent/"banner_reference.json").write_text(json.dumps(report,indent=2)+"\n")

if __name__=="__main__":main()
