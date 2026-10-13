#!/usr/bin/env python3
"""Compare spawn selection by invoking the original private WorldServer method."""
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
    parser.add_argument('--mcp',type=Path,default=root.parent/'MCP-919-main');parser.add_argument('--cxx',default='D:/Strawberry/c/bin/g++.exe' if os.name=='nt' else 'g++')
    args=parser.parse_args();mcp=args.mcp.resolve();build=root/'build/worldgen_reference';build.mkdir(parents=True,exist_ok=True)
    seeds=[0,1,-1,189,123456789,-9223372036854775808,9223372036854775807];commands=[];biomeCases=0;spawnCases=0
    for seed in seeds:
        for type in range(3):
            for x,z,radius in [(0,0,256),(-31,19,112),(139817,-98228,512),(0,0,0),(-1,-1,1)]:
                for allowed in [[4,1,5,19,18,21,22],[2,17],[],[0,24,10,7,11]]:
                    commands.append(' '.join(map(str,['B',seed,type,x,z,radius,seed^1234567,len(allowed),*allowed])));biomeCases+=1
            for mode in range(5):commands.append(f'S {seed} {type} 0 {mode}');spawnCases+=1
        for dimension in [-1,1]:commands.append(f'S {seed} 0 {dimension} 1');spawnCases+=1
    fixture=build/'spawn.fixture';fixture.write_text('\n'.join(commands)+'\n',encoding='ascii')
    cp=os.pathsep.join(str(p) for p in [build,mcp/'bin/minecraft',mcp/'jars/versions/1.8.9/1.8.9.jar',*sorted((mcp/'jars/libraries').rglob('*.jar'))])
    source=mcp/'src/minecraft/net/minecraft';java=[source/'world/WorldServer.java',source/'world/World.java',source/'world/WorldProvider.java',source/'world/biome/WorldChunkManager.java',source/'world/gen/ChunkProviderGenerate.java',source/'world/chunk/Chunk.java',source/'world/chunk/ChunkPrimer.java']
    print('Compiling original spawn selection reference...',flush=True)
    javac(['-encoding','UTF-8','--release','8','-cp',cp,'-d',str(build),*map(str,java),str(root/'tests/java/WorldgenSpawnReference.java')],build)
    executable=build/('worldgen_spawn_tests.exe' if os.name=='nt' else 'worldgen_spawn_tests')
    run([args.cxx,'-std=c++17','-O2','-ffp-contract=off','-fno-fast-math','-I',str(root/'include'),*[str(root/'src'/name) for name in ['world.cpp','worldgen.cpp','worldgen_noise.cpp','worldgen_biomes.cpp','worldgen_surface.cpp','worldgen_carvers.cpp','worldgen_dimensions.cpp','worldgen_features.cpp']],str(root/'tests/worldgen_spawn_tests.cpp'),'-o',str(executable)],root)
    print(f'Comparing {biomeCases} biome searches and {spawnCases} spawn selections...',flush=True)
    run(['java','-Xmx1g','-cp',cp,'WorldgenSpawnReference',str(fixture),str(build/'java_spawn.txt')],build)
    run([str(executable),str(fixture),str(build/'native_spawn.txt')],root)
    reference=(build/'java_spawn.txt').read_text().splitlines();native=(build/'native_spawn.txt').read_text().splitlines();failures=[]
    for i,(a,b) in enumerate(zip(reference,native)):
        if a!=b:failures.append({'row':i,'java':a,'native':b})
    report={'biome_searches':biomeCases,'spawn_selections':spawnCases,'rows':len(reference),'ground_queries':sum(s.startswith('Q ') for s in reference),'mismatches':len(failures),'native_rows':len(native),'stage':'unpopulated carved terrain; predicate path also tested','sha256':hashlib.sha256('\n'.join(reference).encode()).hexdigest(),'failures':failures[:20],'source_sha256':{str(p.relative_to(mcp)):hashlib.sha256(p.read_bytes()).hexdigest() for p in java}}
    (build/'spawn_report.json').write_text(json.dumps(report,indent=2))
    if len(reference)!=len(native) or failures:
        for f in failures[:10]:print(json.dumps(f),file=sys.stderr)
        raise RuntimeError('spawn selection differs')
    print(f'PASS: {biomeCases} biome searches / {spawnCases} spawn selections / {report["ground_queries"]} ground queries match Java exactly')

if __name__=='__main__':
    try:main()
    except Exception as error:print(f'Spawn verification failed: {error}',file=sys.stderr);sys.exit(1)
