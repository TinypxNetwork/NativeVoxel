#!/usr/bin/env python3
"""Compare complete chunk states/light/biomes against original Java generation."""
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
    parser.add_argument("--stage",choices=["terrain","surface","carved"],default="terrain")
    parser.add_argument("--dimension",choices=["overworld","nether","end","all"],default="overworld")
    args=parser.parse_args();mcp=args.mcp.resolve();build=root/"build/worldgen_reference";build.mkdir(parents=True,exist_ok=True)
    stage=["terrain","surface","carved"].index(args.stage);seeds=[0,1,-1,189,123456789,-9223372036854775808,9223372036854775807]
    dimensions=[0,-1,1] if args.dimension=="all" else [{"overworld":0,"nether":-1,"end":1}[args.dimension]]
    cases=[]
    for dimension in dimensions:
        positions=[(0,0),(1,-1),(-7,3),(831,-449),(1874999,-1874999)] if dimension==0 else [(0,0),(1,-1),(-7,3),(3,3),(5,0),(-4,1),(-2,-3),(831,-449),(1874999,-1874999)]
        for seed in seeds:
            for type in range(3) if dimension==0 else [0]:
                for x,z in positions:cases.append((seed,type,stage,x,z,dimension))
    prefix=("" if args.dimension=="overworld" else args.dimension+"_")+args.stage
    fixture=build/(prefix+"_chunks.fixture");fixture.write_text("\n".join(" ".join(map(str,c)) for c in cases)+"\n",encoding="ascii")
    cp=os.pathsep.join(str(p) for p in [build,mcp/"bin/minecraft",mcp/"jars/versions/1.8.9/1.8.9.jar",*sorted((mcp/"jars/libraries").rglob("*.jar"))])
    source=mcp/"src/minecraft/net/minecraft"
    java=[source/"world/gen/ChunkProviderGenerate.java",source/"world/chunk/ChunkPrimer.java",source/"world/chunk/Chunk.java",source/"world/biome/WorldChunkManager.java",
          source/"world/gen/MapGenBase.java",source/"world/gen/MapGenCaves.java",source/"world/gen/MapGenRavine.java",
          source/"world/gen/MapGenCavesHell.java",source/"world/gen/ChunkProviderHell.java",source/"world/gen/ChunkProviderEnd.java"]
    print(f"Compiling original Java {args.stage} generation reference...",flush=True)
    javac(["-encoding","UTF-8","--release","8","-cp",cp,"-d",str(build),*map(str,java),str(root/"tests/java/WorldgenChunkReference.java")],build)
    executable=build/("worldgen_chunk_tests.exe" if os.name=="nt" else "worldgen_chunk_tests")
    run([args.cxx,"-std=c++17","-O2","-ffp-contract=off","-fno-fast-math","-I",str(root/"include"),*[str(root/"src"/name) for name in ["world.cpp","worldgen.cpp","worldgen_noise.cpp","worldgen_biomes.cpp","worldgen_surface.cpp","worldgen_carvers.cpp","worldgen_dimensions.cpp","worldgen_features.cpp"]],str(root/"tests/worldgen_chunk_tests.cpp"),"-o",str(executable)],root)
    referencePath=build/("java_"+prefix+"_chunks.bin");nativePath=build/("native_"+prefix+"_chunks.bin")
    print(f"Comparing {len(cases)} complete {args.stage} chunks...",flush=True)
    run(["java","-Xmx1g","-cp",cp,"WorldgenChunkReference",str(fixture),str(referencePath)],build)
    run([str(executable),str(fixture),str(nativePath)],root)
    reference=referencePath.read_bytes();native=nativePath.read_bytes();length=65536*3+256;failures=[]
    for i,case in enumerate(cases):
        a=reference[i*length:(i+1)*length];b=native[i*length:(i+1)*length]
        if a!=b:
            pos=next((j for j,(u,v) in enumerate(zip(a,b)) if u!=v),min(len(a),len(b)))
            failures.append({"case":case,"first_byte":pos,"region":"states" if pos<131072 else "light" if pos<196608 else "biomes",
                "java":a[pos] if pos<len(a) else None,"native":b[pos] if pos<len(b) else None})
    report={"stage":args.stage,"dimensions":dimensions,"chunks":len(cases),"blocks":len(cases)*65536,"bytes":len(reference),"no_sky_light_normalized":True,"mismatches":len(failures),"sha256":hashlib.sha256(reference).hexdigest(),"failures":failures[:20],"source_sha256":{str(p.relative_to(mcp)):hashlib.sha256(p.read_bytes()).hexdigest() for p in java}}
    (build/(prefix+"_chunk_report.json")).write_text(json.dumps(report,indent=2))
    if len(reference)!=len(native) or failures:
        for f in failures[:10]:print(json.dumps(f),file=sys.stderr)
        raise RuntimeError(f"{len(failures)} chunk outputs differ")
    print(f"PASS: {len(cases)} {args.stage} chunks / {len(cases)*65536} blocks + all light/biome bytes match Java exactly")


if __name__=="__main__":
    try:main()
    except Exception as error:print(f"Worldgen chunk verification failed: {error}",file=sys.stderr);sys.exit(1)
