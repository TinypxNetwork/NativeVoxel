#!/usr/bin/env python3
"""Record original tile/model rendering using CPU display-list and matrix stubs."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess

def main():
    root=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--mcp",type=Path,default=root.parent/"MCP-919-main");args=parser.parse_args();mcp=args.mcp.resolve();build=root/"build/tile_entity_reference";original=build/"geometry_original";classes=build/"geometry_classes";original.mkdir(parents=True,exist_ok=True);classes.mkdir(parents=True,exist_ok=True)
    tail=[mcp/"bin/minecraft",mcp/"jars/versions/1.8.9/1.8.9.jar",*sorted((mcp/"jars/libraries").rglob("*.jar"))];source=mcp/"src/minecraft/net/minecraft"
    names=[*["client/model/"+name+".java" for name in ("ModelBase","ModelRenderer","ModelBox","TexturedQuad","PositionTextureVertex","ModelChest","ModelLargeChest","ModelSkeletonHead","ModelHumanoidHead","ModelBanner")],*["client/renderer/tileentity/"+name+".java" for name in ("TileEntityChestRenderer","TileEntityEnderChestRenderer","TileEntitySkullRenderer","TileEntityBannerRenderer")],"client/renderer/GLAllocation.java","tileentity/TileEntityChest.java","util/MathHelper.java"]
    cp=os.pathsep.join(map(str,[original,*tail]));subprocess.run(["javac","-encoding","UTF-8","--release","8","-cp",cp,"-d",str(original),*[str(source/name) for name in names]],cwd=build,check=True)
    cp=os.pathsep.join(map(str,[classes,original,*tail]));subprocess.run(["javac","-encoding","UTF-8","--release","8","-cp",cp,"-d",str(classes),*map(str,sorted((root/"tests/java/tile_stub").rglob("*.java"))),str(root/"tests/java/TileGeometryReference.java")],cwd=build,check=True)
    lines=[]
    def add(id,meta,neighbors=0,holiday=0,stage=-1,type=0,rotation=0,position=(-3,64,5),time=0,previous=.25,current=.5,partial=.375):lines.append(" ".join(map(str,[id,meta,neighbors,holiday,stage,type,rotation,*position,time,previous,current,partial])))
    for id in (54,146,130):
        for meta in (2,3,4,5):
            for neighbors in ((0,) if id==130 else (0,1,2,4,8)):
                for previous,current,partial in ((0,0,0),(.25,.5,.375),(1,1,1)):add(id,meta,neighbors,previous=previous,current=current,partial=partial)
            for stage in (-1,0,9):add(id,meta,neighbors=1 if id!=130 else 0,holiday=1,stage=stage)
    for meta in range(6):
        for type in range(6):
            for rotation in (0,7,-1,15):
                for stage in (-1,4):add(144,meta,type=type,rotation=rotation,stage=stage)
    for id in (176,177):
        for meta in (range(16) if id==176 else (2,3,4,5)):
            for time in (0,12345,10000000):add(id,meta,time=time)
    add(176,7,position=(29999000,64,-29999000),time=9223372036854775807);add(177,5,position=(-29999000,64,29999000),time=-9223372036854775808,stage=5)
    poses=build/"tile_poses.txt";poses.write_text("\n".join(lines)+"\n",encoding="ascii");target=root/"tools/reference/tile_geometry_reference.bin"
    subprocess.run(["java","-Djava.awt.headless=true","-cp",cp,"TileGeometryReference",str(poses),str(target)],cwd=build,check=True)
    report={"cases":len(lines),"source_sha256":{name:hashlib.sha256((source/name).read_bytes()).hexdigest() for name in names},"fixture_sha256":hashlib.sha256(target.read_bytes()).hexdigest(),"method":"Original ModelRenderer/ModelBox/TexturedQuad and tile renderers; OpenGL display lists/texture binding captured. Original chest update executes against a silent client World stub."}
    (target.parent/"tile_geometry_reference.json").write_text(json.dumps(report,indent=2)+"\n")

if __name__=="__main__":main()
