#!/usr/bin/env python3
"""Compare default ores and complete End decoration with original Java."""
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

def read_case(stream):
    states=stream.read(4*65536*2)
    if len(states)!=4*65536*2:raise RuntimeError('truncated population states')
    tail=stream.read(4)
    if len(tail)!=4:raise RuntimeError('missing entity count')
    count=struct.unpack('>I',tail)[0];entities=[]
    if count>100:raise RuntimeError('invalid population entity count')
    for _ in range(count):
        length=stream.read(2);size=struct.unpack('>H',length)[0];record=stream.read(size+32)
        if len(record)!=size+32:raise RuntimeError('truncated entity record')
        entities.append(record[:size].decode());tail+=length+record
    random=stream.read(8)
    if len(random)!=8:raise RuntimeError('missing random result')
    return states,tail+random,entities

def main()->None:
    root=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mcp',type=Path,default=root.parent/'MCP-919-main');parser.add_argument('--cxx',default='D:/Strawberry/c/bin/g++.exe' if os.name=='nt' else 'g++')
    parser.add_argument('--dimension',choices=['overworld','end','all'],default='all')
    args=parser.parse_args();mcp=args.mcp.resolve();build=root/'build/worldgen_reference';build.mkdir(parents=True,exist_ok=True)
    seeds=[0,1,-1,189,123456789,-9223372036854775808,9223372036854775807];cases=[]
    if args.dimension!='end':
        for seed in seeds:
            for type in range(3):
                for x,z in [(0,0),(-7,3),(831,-449)]:cases.append((seed,type,0,x,z,seed))
    if args.dimension!='overworld':
        for seed in seeds:
            for randomSeed in [0,189,-9223372036854775808,9223372036854775807]:
                for x,z in [(0,0),(1,-1),(-2,-2),(2,1),(3,0),(-4,2),(5,0),(831,-449)]:cases.append((seed,0,1,x,z,randomSeed))
    prefix=args.dimension+'_population';fixture=build/(prefix+'.fixture');fixture.write_text('\n'.join(' '.join(map(str,case)) for case in cases)+'\n',encoding='ascii')
    cp=os.pathsep.join(str(p) for p in [build,mcp/'bin/minecraft',mcp/'jars/versions/1.8.9/1.8.9.jar',*sorted((mcp/'jars/libraries').rglob('*.jar'))])
    source=mcp/'src/minecraft/net/minecraft';java=[source/'world/gen/ChunkProviderEnd.java',source/'world/biome/BiomeDecorator.java',source/'world/biome/BiomeEndDecorator.java',source/'world/gen/feature/WorldGenMinable.java',source/'world/gen/feature/WorldGenSpikes.java']
    print('Compiling original ore/End population reference...',flush=True)
    javac(['-encoding','UTF-8','--release','8','-cp',cp,'-d',str(build),*map(str,java),str(root/'tests/java/WorldgenPopulationReference.java')],build)
    executable=build/('worldgen_population_tests.exe' if os.name=='nt' else 'worldgen_population_tests')
    run([args.cxx,'-std=c++17','-O2','-ffp-contract=off','-fno-fast-math','-I',str(root/'include'),*[str(root/'src'/name) for name in ['world.cpp','worldgen.cpp','worldgen_noise.cpp','worldgen_biomes.cpp','worldgen_surface.cpp','worldgen_carvers.cpp','worldgen_dimensions.cpp','worldgen_features.cpp']],str(root/'tests/worldgen_population_tests.cpp'),'-o',str(executable)],root)
    print(f'Comparing {len(cases)} population operations over {len(cases)*4} generated columns...',flush=True)
    referencePath=build/('java_'+prefix+'.bin');nativePath=build/('native_'+prefix+'.bin')
    run(['java','-Xmx1g','-cp',cp,'WorldgenPopulationReference',str(fixture),str(referencePath)],build)
    run([str(executable),str(fixture),str(nativePath)],root)
    failures=[];spawned={};sha=hashlib.sha256()
    with referencePath.open('rb') as ref,nativePath.open('rb') as actual:
        for case in cases:
            a,ae,entities=read_case(ref);b,be,_=read_case(actual);sha.update(a);sha.update(ae)
            for entity in entities:spawned[entity]=spawned.get(entity,0)+1
            if a!=b or ae!=be:
                first=next((i for i,(u,v) in enumerate(zip(a,b)) if u!=v),-1)
                failures.append({'case':case,'first_state_byte':first,'java':a[first] if first>=0 else None,'native':b[first] if first>=0 else None,'entity_or_random_mismatch':ae!=be})
        if ref.read(1) or actual.read(1):raise RuntimeError('trailing population bytes')
    report={'operations':len(cases),'columns':len(cases)*4,'blocks':len(cases)*4*65536,'scope':'default ore pass in Overworld; complete End decoration blocks and entity creation requests; explicit world Random','spawned':spawned,'mismatches':len(failures),'sha256':sha.hexdigest(),'failures':failures[:20],'source_sha256':{str(p.relative_to(mcp)):hashlib.sha256(p.read_bytes()).hexdigest() for p in java}}
    (build/(prefix+'_report.json')).write_text(json.dumps(report,indent=2))
    if failures:
        for f in failures[:10]:print(json.dumps(f),file=sys.stderr)
        raise RuntimeError(f'{len(failures)} population operations differ')
    print(f'PASS: {len(cases)} population operations / {report["blocks"]} blocks / entity requests {spawned} / remaining Random state match Java exactly')

if __name__=='__main__':
    try:main()
    except Exception as error:print(f'Population verification failed: {error}',file=sys.stderr);sys.exit(1)
