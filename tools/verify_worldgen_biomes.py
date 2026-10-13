#!/usr/bin/env python3
"""Compare complete original GenLayer maps with native biome maps."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
from export_registry import javac
from verify_physics import run


def main()->None:
    root=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcp",type=Path,default=root.parent/"MCP-919-main");parser.add_argument("--cxx",default="D:/Strawberry/c/bin/g++.exe" if os.name=="nt" else "g++")
    args=parser.parse_args();mcp=args.mcp.resolve();build=root/"build/worldgen_reference";build.mkdir(parents=True,exist_ok=True)
    commands=[];seeds=[0,1,-1,189,123456789,-9223372036854775808,9223372036854775807]
    for seed in seeds:
        for type in range(3):
            for x,z in [(0,0),(-16,-16),(-71,32),(12048,-6512),(1874999,-1874999),(-29999872,29999872)]:
                for level,width,height in [(0,10,10),(0,31,33),(0,64,64),(1,16,16),(1,32,16),(1,1,1)]:commands.append((seed,type,level,x,z,width,height))
    fixture=build/"biomes.fixture";fixture.write_text("\n".join(" ".join(map(str,c)) for c in commands)+"\n",encoding="ascii")
    cp=os.pathsep.join(str(p) for p in [build,mcp/"bin/minecraft",mcp/"jars/versions/1.8.9/1.8.9.jar",*sorted((mcp/"jars/libraries").rglob("*.jar"))])
    sources=sorted((mcp/"src/minecraft/net/minecraft/world/gen/layer").glob("*.java"))
    print("Compiling original GenLayer pipeline...",flush=True)
    javac(["-encoding","UTF-8","--release","8","-cp",cp,"-d",str(build),*map(str,sources),str(root/"tests/java/WorldgenBiomeReference.java")],build)
    executable=build/("worldgen_biome_tests.exe" if os.name=="nt" else "worldgen_biome_tests")
    run([args.cxx,"-std=c++17","-O2","-ffp-contract=off","-fno-fast-math","-I",str(root/"include"),str(root/"src/worldgen_biomes.cpp"),str(root/"src/worldgen_noise.cpp"),str(root/"tests/worldgen_biome_tests.cpp"),"-o",str(executable)],root)
    print(f"Comparing {len(commands)} original/default/large/amplified map regions...",flush=True)
    run(["java","-cp",cp,"WorldgenBiomeReference",str(fixture),str(build/"java_biomes.bin")],build)
    run([str(executable),str(fixture),str(build/"native_biomes.bin")],root)
    reference=(build/"java_biomes.bin").read_bytes();actual=(build/"native_biomes.bin").read_bytes();failures=[];offset=0;values=set()
    for case in commands:
        size=case[-2]*case[-1]*4;a=reference[offset:offset+size];b=actual[offset:offset+size]
        if a!=b:
            first=next((i for i in range(min(len(a),len(b))//4) if a[i*4:i*4+4]!=b[i*4:i*4+4]),-1)
            failures.append({"case":case,"first_cell":first,"java":struct.unpack_from('>i',a,first*4)[0] if first>=0 else None,"native":struct.unpack_from('>i',b,first*4)[0] if first>=0 else None})
        values.update(v[0] for v in struct.iter_unpack('>i',a));offset+=size
    report={"seeds":seeds,"regions":len(commands),"cells":len(reference)//4,"mismatching_regions":len(failures),"biomes_seen":sorted(values),"sha256":hashlib.sha256(reference).hexdigest(),"failures":failures[:30],"source_sha256":{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}}
    (build/"biome_report.json").write_text(json.dumps(report,indent=2))
    if len(reference)!=len(actual) or failures:
        for failure in failures[:10]:print(json.dumps(failure),file=sys.stderr)
        raise RuntimeError(f"{len(failures)} biome regions differ")
    print(f"PASS: {len(commands)} regions / {len(reference)//4} biome IDs exactly match original Java")


if __name__=="__main__":
    try:main()
    except Exception as error:print(f"Worldgen biome verification failed: {error}",file=sys.stderr);sys.exit(1)
