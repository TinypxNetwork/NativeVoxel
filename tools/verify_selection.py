#!/usr/bin/env python3
"""Focused original-Java differential for block outlines and ray picking."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import sys
from export_registry import javac
from verify_physics import make_reference,run


def fixtures(path:Path, records:list[dict]) -> list[str]:
    commands=[];labels=[];label="initial";rng=random.Random(189)
    def emit(line):
        commands.append(line)
        if line[0] in "QR":labels.append(label)
    def block(x,y,z,state):emit(f"B {x} {y} {z} {state}")
    def ray(start,end,liquid=False,ignore=False,miss=False):
        emit("R "+" ".join(format(float(v),".17g") for v in (*start,*end))+f" {int(liquid)} {int(ignore)} {int(miss)}")
    axes=[((-.25,64.5,.5),(1.25,64.5,.5)),((.5,63.75,.5),(.5,65.25,.5)),((.5,64.5,-.25),(.5,64.5,1.25)),((.5,64.5,1.25),(.5,64.5,-.25))]
    for record in records:
        if not record["valid"]:continue
        state=record["state"];label=f"state {state>>4}:{state&15}"
        emit("W");block(0,64,0,state);emit("Q 0 64 0")
        for start,end in axes:ray(start,end)
        if state>>4 in (8,9,10,11,50,63,68,75,76,106):
            ray(*axes[2],liquid=True);ray(*axes[2],liquid=True,ignore=True,miss=True)
    for id in [85,113,139,101,102,160]:
        for mask in range(16):
            label=f"connected shape {id} neighbor mask {mask}";emit("W");block(0,64,0,id<<4)
            for bit,(x,z) in enumerate([(0,-1),(1,0),(0,1),(-1,0)]):
                if mask&(1<<bit):block(x,64,z,16)
            emit("Q 0 64 0")
            for side in [.1,.49,.51,.9]:ray((side,64.7,-.25),(side,64.7,1.25));ray((-.25,64.7,side),(1.25,64.7,side))
    for id in [64,71,193,194,195,196,197]:
        for lower in range(8):
            for hinge in range(2):
                label=f"door {id} lower {lower} hinge {hinge}";emit("W");block(0,64,0,(id<<4)|lower);block(0,65,0,(id<<4)|8|hinge)
                emit("Q 0 64 0");emit("Q 0 65 0")
                for start,end in axes:ray(start,end);ray(tuple(v+(1 if i==1 else 0) for i,v in enumerate(start)),tuple(v+(1 if i==1 else 0) for i,v in enumerate(end)))
    for upper in [0,1,20,18,44]:
        label=f"unattached vine ceiling {upper}";emit("W");block(0,64,0,106<<4);block(0,65,0,upper<<4);emit("Q 0 64 0")
        for start,end in axes:ray(start,end)
    for meta in range(8):
        label=f"stair corner {meta}";emit("W");block(0,64,0,(53<<4)|meta);block(1,64,0,(67<<4)|((meta+2)%8));block(0,64,-1,(53<<4)|((meta+3)%8));emit("Q 0 64 0")
        for y in [64.1,64.49,64.5,64.51,64.9]:
            for x in [.1,.49,.5,.51,.9]:ray((x,y,-.1),(x,y,1.1));ray((-.1,y,x),(1.1,y,x))
    for x in [-17,-1,16777217]:
        label=f"cactus selected float coordinates {x}";emit("W");block(x,64,-1,81<<4);emit(f"Q {x} 64 -1");ray((x-.2,64.5,-.5),(x+1.2,64.5,-.5))
    label="DDA boundary/tie/miss cases";emit("W");block(0,64,0,16);block(2,64,2,16)
    endpoints=[((0,64.5,.5),(1,64.5,.5)),((1,64.5,.5),(0,64.5,.5)),((0,64,0),(3,67,3)),((3,67,3),(0,64,0)),
        ((-.5,63.5,-.5),(2.5,66.5,2.5)),((1,64,1),(1,65,1)),((.5,64.5,.5),(.5,64.5,.5)),
        ((.49999,64.5,-1),(.50001,64.5,2)),((0,63,-1),(0,66,2))]
    for start,end in endpoints:
        for liquid,ignore,miss in [(False,False,False),(False,False,True),(True,True,True)]:ray(start,end,liquid,ignore,miss)
    for distance in [200,201,202]:
        label=f"DDA iteration limit {distance}";emit("W");block(distance,64,0,16);ray((.5,64.5,.5),(250,64.5,.5),miss=True)
    label="mixed-block deterministic rays";emit("W")
    candidates=[16,44<<4,(44<<4)|8,53<<4,(53<<4)|2,50<<4,68<<4|2,68<<4|3,65<<4|2,81<<4,106<<4|1,139<<4,102<<4,9<<4]
    for x in range(-4,5):
        for z in range(-4,5):
            if rng.random()<.55:block(x,64+rng.randrange(3),z,rng.choice(candidates))
    for i in range(2000):
        label=f"mixed ray {i}"
        start=[rng.uniform(-6,6),rng.uniform(63,69),rng.uniform(-6,6)];end=[rng.uniform(-6,6),rng.uniform(63,69),rng.uniform(-6,6)]
        ray(start,end,i%3==0,i%4==0,i%2==0)
    path.write_text("\n".join(commands)+"\n",encoding="ascii");return labels


def main()->None:
    root=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcp",type=Path,default=root.parent/"MCP-919-main");parser.add_argument("--cxx",default="D:/Strawberry/c/bin/g++.exe" if os.name=="nt" else "g++")
    args=parser.parse_args();mcp=args.mcp.resolve();build=root/"build/selection_reference";build.mkdir(parents=True,exist_ok=True)
    raw=build/"selection.json"
    if not raw.is_file():run([sys.executable,str(root/"tools/export_selection.py"),"--mcp",str(mcp)],root)
    fixture=build/"picking.fixture";labels=fixtures(fixture,json.loads(raw.read_text()));make_reference(root,mcp,build)
    cp=os.pathsep.join(str(p) for p in [build,root/"build/physics_reference/oracle_classes",mcp/"bin/minecraft",mcp/"jars/versions/1.8.9/1.8.9.jar",*sorted((mcp/"jars/libraries").rglob("*.jar"))])
    source=mcp/"src/minecraft/net/minecraft";sources=sorted((source/"block").rglob("*.java"))+[source/"world/World.java",source/"util/Vec3.java"]
    print("Compiling original block picking and World.rayTraceBlocks sources...",flush=True)
    javac(["-encoding","UTF-8","--release","8","-cp",cp,"-d",str(build),*map(str,sources),str(build/"PhysicsReference.java"),str(root/"tests/java/SelectionReference.java")],build)
    native=build/("selection_tests.exe" if os.name=="nt" else "selection_tests")
    run([args.cxx,"-std=c++17","-O2","-ffp-contract=off","-I",str(root/"include"),str(root/"src/world.cpp"),str(root/"tests/selection_tests.cpp"),"-o",str(native)],root)
    print(f"Comparing {len(labels)} outline/ray results...",flush=True)
    run(["java","-Djava.awt.headless=true","-cp",cp,"SelectionReference",str(fixture),str(build/"java_picking.txt")],build)
    result=run([str(native),str(fixture)],root);(build/"native_picking.txt").write_text(result.stdout,encoding="ascii")
    reference=(build/"java_picking.txt").read_text().splitlines();actual=result.stdout.splitlines()
    if len(reference)!=len(actual):raise RuntimeError(f"row count differs: {len(reference)} vs {len(actual)}")
    failures=[];maximum=0.0
    for i,(java,cpp) in enumerate(zip(reference,actual)):
        a,b=java.split(),cpp.split()
        if len(a)!=len(b) or a[0]!=b[0]:delta=None
        else:delta=max((abs(float(x)-float(y)) for x,y in zip(a[1:],b[1:])),default=0);maximum=max(maximum,delta)
        if delta is None or delta!=0:failures.append({"row":i,"case":labels[i],"java":java,"native":cpp,"delta":delta})
    report={"rows":len(reference),"selected_bounds":sum(line.startswith('Q') for line in reference),"rays":sum(line.startswith('R') for line in reference),
        "mismatches":len(failures),"maximum_absolute_difference":maximum,"failures":failures[:100],"source_sha256":{str(p.relative_to(mcp)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}}
    (build/"picking_report.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    if failures:
        for f in failures[:12]:print(json.dumps(f),file=sys.stderr)
        raise RuntimeError(f"{len(failures)} ray/selection differences; see {build/'picking_report.json'}")
    print(f"PASS: {len(reference)} original Java/C++ outline and ray results match exactly")


if __name__=="__main__":
    try:main()
    except Exception as error:print(f"Selection verification failed: {error}",file=sys.stderr);sys.exit(1)
