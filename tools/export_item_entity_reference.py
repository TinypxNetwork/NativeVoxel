#!/usr/bin/env python3
"""Run original RenderEntityItem/RenderXPOrb and verify GROUND/FIXED inheritance."""
from pathlib import Path
import argparse,hashlib,json,os,random,subprocess


def main():
    root=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--mcp",type=Path,default=root.parent/"MCP-919-main");args=parser.parse_args();mcp=args.mcp.resolve()
    build=root/"build/item_entity_reference";original=build/"original";classes=build/"classes";original.mkdir(parents=True,exist_ok=True);classes.mkdir(parents=True,exist_ok=True)
    tail=[mcp/"bin/minecraft",mcp/"jars/versions/1.8.9/1.8.9.jar",*sorted((mcp/"jars/libraries").rglob("*.jar"))]
    source=mcp/"src/minecraft/net/minecraft";names=["client/renderer/entity/RenderEntityItem.java","client/renderer/entity/RenderXPOrb.java","entity/item/EntityXPOrb.java","entity/item/EntityItem.java","util/MathHelper.java",*["client/renderer/block/model/"+name+".java" for name in ("ModelBlock","ItemCameraTransforms","ItemTransformVec3f")]]
    cp=os.pathsep.join(map(str,[original,*tail]));subprocess.run(["javac","-encoding","UTF-8","--release","8","-cp",cp,"-d",str(original),*[str(source/name) for name in names]],cwd=build,check=True)
    cp=os.pathsep.join(map(str,[classes,original,*tail]));stubs=sorted((root/"tests/java/item_entity_stub").rglob("*.java"))
    subprocess.run(["javac","-encoding","UTF-8","--release","8","-cp",cp,"-d",str(classes),*map(str,stubs),str(root/"tests/java/ItemEntityReference.java")],cwd=build,check=True)
    lines=[]
    for count in (-1,0,1,2,16,17,32,33,48,49,64,127):
        for gui3d in (0,1):
            for options in (0,1):
                for scale in ((1,1,1),(.5,.25,2),(-1,.75,-.5)):lines.append(f"D {count} 73 0.375 1.37 2.5 -0.75 11.25 {gui3d} {options} {' '.join(map(str,scale))}")
    rng=random.Random(189187)
    for i in range(128):lines.append(f"D {rng.randrange(1,128)} {rng.randrange(0,6000)} {rng.random()} {rng.random()*6.283} {rng.uniform(-100,100)} {rng.uniform(-100,100)} {rng.uniform(-100,100)} {i%2} 1 1 1 1")
    values=[-1,0,1,2,3,6,7,16,17,36,37,72,73,148,149,306,307,616,617,1236,1237,2476,2477,32767]
    for amount in values:
        for tick in (0,1,20,300,5999):
            for light in (0,240,0xf00000,0xf000f0,0x800030):lines.append(f"X {amount} {tick} 0.625 135 -37 2.5 -0.75 11.25 {light}")
    for i in range(128):
        light=rng.randrange(0,256)|(rng.randrange(0,256)<<16)
        lines.append(f"X {rng.randrange(0,32768)} {rng.randrange(0,6000)} {rng.random()} {rng.uniform(-1080,1080)} {rng.uniform(-90,90)} {rng.uniform(-100,100)} {rng.uniform(-100,100)} {rng.uniform(-100,100)} {light}")
    poses=build/"poses.txt";poses.write_text("\n".join(lines)+"\n",encoding="ascii");target=root/"tools/reference/item_entities_reference.bin";displays=build/"display_reference.json"
    subprocess.run(["java","-Djava.awt.headless=true","-cp",cp,"ItemEntityReference",str(poses),str(target),str(root/"assets/runtime/first_person/models.json"),str(root/"assets/resources"),str(displays)],cwd=build,check=True)
    native={entry["name"]:entry for entry in json.loads((root/"assets/runtime/first_person/models.json").read_text())};comparisons=0;maximum=0
    for item in json.loads(displays.read_text()):
        for kind in ("ground","fixed"):
            for a,b in zip(native[item["name"]][kind],item[kind]):
                delta=abs(a-b);maximum=max(maximum,delta);comparisons+=1
                if delta>2e-6:raise RuntimeError(f"{item['name']} {kind}: {a} != Java {b}")
    report={"drop_cases":sum(x.startswith("D ") for x in lines),"orb_cases":sum(x.startswith("X ") for x in lines),"ground_fixed_comparisons":comparisons,"maximum_display_error":maximum,"reference_sha256":hashlib.sha256(target.read_bytes()).hexdigest(),"source_sha256":{name:hashlib.sha256((source/name).read_bytes()).hexdigest() for name in names},"method":"Original entity renderers; GL calls recorded. Nested dropped model display is compared separately with original ModelBlock inheritance; XP superclass supplies controlled ordinary light."}
    (target.parent/"item_entities_reference.json").write_text(json.dumps(report,indent=2)+"\n");print(json.dumps({k:report[k] for k in ("drop_cases","orb_cases","ground_fixed_comparisons","maximum_display_error")}))


if __name__=="__main__":main()
