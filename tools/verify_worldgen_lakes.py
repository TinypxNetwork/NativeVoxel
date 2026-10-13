#!/usr/bin/env python3
"""Compare original lake masks, geology, lighting boundaries and freezing."""
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
    args=parser.parse_args();mcp=args.mcp.resolve();build=root/'build/worldgen_reference';build.mkdir(parents=True,exist_ok=True);cases=[]
    seeds=[0,1,-1,189,123456789,-9223372036854775808,9223372036854775807]
    for seed in seeds:
        for pattern in [7,4,5,10,16,17,18]:
            for liquid in [9,11]:
                for biome,sky in [(1,15),(14,15),(12,15),(14,0)]:cases.append((seed,pattern,63,8 if seed&1 else 23,128,23 if seed&1 else 8,liquid,biome,sky))
    for ground in [0,3,4,5,6,63,252,255]:
        for liquid in [9,11]:
            for pattern in [5,7]:cases.append((189,pattern,ground,8,ground+10,8,liquid,12,15))
    for y in [0,3,4,5,6,32]:
        for liquid in [8,9,10,11]:cases.append((189,5,63,8,y,8,liquid,14,15))
    fixture=build/'lakes.fixture';fixture.write_text('\n'.join(' '.join(map(str,case)) for case in cases)+'\n',encoding='ascii')
    cp=os.pathsep.join(str(p) for p in [build,mcp/'bin/minecraft',mcp/'jars/versions/1.8.9/1.8.9.jar',*sorted((mcp/'jars/libraries').rglob('*.jar'))])
    source=mcp/'src/minecraft/net/minecraft';java=[source/'world/gen/feature/WorldGenLakes.java',source/'world/World.java']
    print('Compiling original lake generator...',flush=True)
    javac(['-encoding','UTF-8','--release','8','-cp',cp,'-d',str(build),*map(str,java),str(root/'tests/java/WorldgenFeatureReference.java'),str(root/'tests/java/WorldgenLakeReference.java')],build)
    executable=build/('worldgen_lake_tests.exe' if os.name=='nt' else 'worldgen_lake_tests')
    run([args.cxx,'-std=c++17','-O2','-ffp-contract=off','-fno-fast-math','-I',str(root/'include'),*[str(root/'src'/name) for name in ['world.cpp','worldgen_noise.cpp','worldgen_features.cpp']],str(root/'tests/worldgen_lake_tests.cpp'),'-o',str(executable)],root)
    print(f'Comparing {len(cases)} water/lava lake attempts...',flush=True)
    run(['java','-Xmx1g','-cp',cp,'WorldgenLakeReference',str(fixture),str(build/'java_lakes.txt')],build)
    run([str(executable),str(fixture),str(build/'native_lakes.txt')],root)
    reference=(build/'java_lakes.txt').read_text().split('END\n');native=(build/'native_lakes.txt').read_text().split('END\n');failures=[]
    for i,case in enumerate(cases):
        a=reference[i] if i<len(reference) else '';b=native[i] if i<len(native) else ''
        if a!=b:
            ar=a.splitlines();br=b.splitlines();first=next((j for j,(u,v) in enumerate(zip(ar,br)) if u!=v),min(len(ar),len(br)))
            failures.append({'case':case,'row':first,'java':ar[first] if first<len(ar) else None,'native':br[first] if first<len(br) else None,'java_rows':len(ar),'native_rows':len(br)})
    changes=sum(value.count('\nB ') for value in reference);placed={}
    for value in reference:
        for row in value.splitlines():
            if row.startswith('B '):id=int(row.split()[-1])>>4;placed[id]=placed.get(id,0)+1
    report={'attempts':len(cases),'changed_blocks':changes,'changed_block_ids':placed,'scope':'lake block generation with supplied sky-light/biome queries and original World.canBlockFreezeWater in cold/hot biomes; scheduled fluid/light simulation is separate','mismatches':len(failures),'sha256':hashlib.sha256('END\n'.join(reference).encode()).hexdigest(),'failures':failures[:20],'source_sha256':{str(p.relative_to(mcp)):hashlib.sha256(p.read_bytes()).hexdigest() for p in java}}
    (build/'lakes_report.json').write_text(json.dumps(report,indent=2))
    if failures or len(reference)!=len(native):
        for f in failures[:10]:print(json.dumps(f),file=sys.stderr)
        raise RuntimeError(f'{len(failures)} lake attempts differ')
    print(f'PASS: {len(cases)} lake attempts / {changes} changed blocks / light-freezing queries / remaining Random state match Java')

if __name__=='__main__':
    try:main()
    except Exception as error:print(f'Lake verification failed: {error}',file=sys.stderr);sys.exit(1)
