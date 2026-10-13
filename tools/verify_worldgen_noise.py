#!/usr/bin/env python3
"""Exact bit/draw-consumption checks for native vanilla random and noise."""
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
    seeds=[0,1,-1,189,123456789,-9223372036854775808,9223372036854775807];commands=[]
    for seed in seeds:
        commands.extend([f"R {seed} 256",f"G {seed} 128",f"I {seed} -13.75 4.125 21.25 5 33 5 .025 .08 .125 .25",f"I {seed} 11.5 88 -7.125 7 1 9 .0625 1 .03125 .5"])
        for x,z in [(0,0),(-71,32),(1874999,-1874999),(16777217,-16777217)]:
            commands.extend([f"O {seed} 16 {x} 0 {z} 5 33 5 684.412 684.412 684.412",f"O {seed} 8 {x} 0 {z} 5 33 5 8.55515 4.277575 8.55515",f"O {seed} 10 {x} 10 {z} 7 1 9 200 1 200"])
        commands.extend([f"S {seed} 0 0 128",f"S {seed} -217.25 82.5 128",f"A {seed} -32 71 16 16 .0625 .0625 .55",f"A {seed} 0 0 5 9 .125 .2 1.7",f"P {seed} 4 -32 71 16 16 .0625 .0625 .5 .5",f"P {seed} 4 125.75 -821.5 17 9 .025 .15 2 .5",f"V {seed} 4 -218.25 4.5 128"])
    fixture=build/"noise.fixture";fixture.write_text("\n".join(commands)+"\n",encoding="ascii")
    cp=os.pathsep.join(str(p) for p in [build,mcp/"bin/minecraft",*sorted((mcp/"jars/libraries").rglob("*.jar"))])
    source=mcp/"src/minecraft/net/minecraft/world/gen";sources=[source/(name+".java") for name in ["NoiseGenerator","NoiseGeneratorImproved","NoiseGeneratorOctaves","NoiseGeneratorSimplex","NoiseGeneratorPerlin"]]
    javac(["-encoding","UTF-8","--release","8","-cp",cp,"-d",str(build),*map(str,sources),str(root/"tests/java/WorldgenNoiseReference.java")],build)
    exe=build/("worldgen_noise_tests.exe" if os.name=="nt" else "worldgen_noise_tests")
    run([args.cxx,"-std=c++17","-O2","-ffp-contract=off","-fno-fast-math","-I",str(root/"include"),str(root/"src/worldgen_noise.cpp"),str(root/"tests/worldgen_noise_tests.cpp"),"-o",str(exe)],root)
    run(["java","-cp",cp,"WorldgenNoiseReference",str(fixture),str(build/"java_noise.txt")],build)
    native=run([str(exe),str(fixture)],root).stdout;(build/"native_noise.txt").write_text(native,encoding="ascii")
    reference=(build/"java_noise.txt").read_text().splitlines();actual=native.splitlines()
    if len(reference)!=len(actual):raise RuntimeError("noise row counts differ")
    failures=[];gaussian=[]
    for i,(a,b) in enumerate(zip(reference,actual)):
        if a!=b:
            difference={"row":i,"command":commands[int(a.split()[1])],"java":a,"native":b}
            (gaussian if a.startswith("G ") else failures).append(difference)
    report={"seeds":seeds,"cases":len(commands),"rows":len(reference),"bitwise_noise_random_mismatches":len(failures),"gaussian_bitwise_mismatches":len(gaussian),"failures":failures[:20],"gaussian_differences":gaussian[:10],"source_sha256":{str(p.relative_to(mcp)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}}
    (build/"noise_report.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    if failures:
        for failure in failures[:10]:print(json.dumps(failure),file=sys.stderr)
        raise RuntimeError(f"{len(failures)} exact random/noise mismatches")
    print(f"PASS: {len(reference)} rows; all used Random draws and four noise generators are bit-identical; Gaussian libm-only bit differences={len(gaussian)}")


if __name__=="__main__":
    try:main()
    except Exception as error:print(f"Worldgen noise verification failed: {error}",file=sys.stderr);sys.exit(1)
