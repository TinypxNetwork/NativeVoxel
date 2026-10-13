#!/usr/bin/env python3
"""Exercise surface rules of all61 registered biomes, including rare mutations."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
from export_registry import javac
from verify_physics import run


def main()->None:
    root=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcp",type=Path,default=root.parent/"MCP-919-main");parser.add_argument("--cxx",default="D:/Strawberry/c/bin/g++.exe" if os.name=="nt" else "g++")
    args=parser.parse_args();mcp=args.mcp.resolve();build=root/"build/worldgen_reference";biomes=json.loads((build/"biome_data.json").read_text())["biomes"]
    seeds=[0,1,-1,189,123456789,-9223372036854775808,9223372036854775807];cases=[]
    for seed in seeds:
        for b in biomes:
            for height in [55,62,65,95,130]:
                for noise in [-12,-1,-.8,.8,2.5]:
                    x=b["id"]*19+height-600;z=height*13-b["id"]*7-902
                    raw=(seed^(x*341873128712)^(z*132897987541)^height)&((1<<64)-1);randomSeed=raw-(1<<64) if raw>>63 else raw
                    cases.append((seed,randomSeed,b["id"],x,z,height,noise))
    fixture=build/"surfaces.fixture";fixture.write_text("\n".join(" ".join(map(str,c)) for c in cases)+"\n",encoding="ascii")
    cp=os.pathsep.join(str(p) for p in [build,mcp/"bin/minecraft",mcp/"jars/versions/1.8.9/1.8.9.jar",*sorted((mcp/"jars/libraries").rglob("*.jar"))]);source=mcp/"src/minecraft/net/minecraft"
    sources=sorted((source/"world/biome").glob("*.java"))
    javac(["-encoding","UTF-8","--release","8","-cp",cp,"-d",str(build),*map(str,sources),str(root/"tests/java/WorldgenChunkReference.java"),str(root/"tests/java/WorldgenSurfaceReference.java")],build)
    executable=build/("worldgen_surface_tests.exe" if os.name=="nt" else "worldgen_surface_tests")
    run([args.cxx,"-std=c++17","-O2","-ffp-contract=off","-fno-fast-math","-I",str(root/"include"),str(root/"src/worldgen_surface.cpp"),str(root/"src/worldgen_noise.cpp"),str(root/"tests/worldgen_surface_tests.cpp"),"-o",str(executable)],root)
    print(f"Comparing {len(cases)} columns across all61 biome surface methods...",flush=True)
    run(["java","-Xmx1g","-cp",cp,"WorldgenSurfaceReference",str(fixture),str(build/"java_surfaces.bin")],build)
    run([str(executable),str(fixture),str(build/"native_surfaces.bin")],root)
    a=(build/"java_surfaces.bin").read_bytes();b=(build/"native_surfaces.bin").read_bytes();length=524;failures=[]
    for i,c in enumerate(cases):
        ref=a[i*length:(i+1)*length];native=b[i*length:(i+1)*length]
        if ref!=native:
            first=next((j for j,(x,y) in enumerate(zip(ref,native)) if x!=y),min(len(ref),len(native)))
            failures.append({"case":c,"first_byte":first,"area":"blocks" if first<512 else "random" if first<520 else "temperature"})
    report={"biomes":len(biomes),"seeds":seeds,"columns":len(cases),"blocks":len(cases)*256,"mismatches":len(failures),"sha256":hashlib.sha256(a).hexdigest(),"failures":failures[:30],"source_sha256":{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}}
    (build/"surface_rules_report.json").write_text(json.dumps(report,indent=2))
    if len(a)!=len(b) or failures:
        for f in failures[:10]:print(json.dumps(f),file=sys.stderr)
        raise RuntimeError(f"{len(failures)} biome surface columns differ")
    print(f"PASS: {len(cases)} columns / {len(cases)*256} blocks + random consumption + temperature bits match Java")


if __name__=="__main__":
    try:main()
    except Exception as error:print(f"Worldgen surface verification failed: {error}",file=sys.stderr);sys.exit(1)
