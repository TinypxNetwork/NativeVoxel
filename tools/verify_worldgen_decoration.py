#!/usr/bin/env python3
"""Compare all 61 original biome decorators with exact immediate-tick boundaries."""
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
    data=json.loads((build/'biome_data.json').read_text())['biomes'];cp=os.pathsep.join(str(p) for p in [build,mcp/'bin/minecraft',mcp/'jars/versions/1.8.9/1.8.9.jar',*sorted((mcp/'jars/libraries').rglob('*.jar'))])
    source=mcp/'src/minecraft/net/minecraft';java=sorted((source/'world/biome').glob('*.java'))
    print('Compiling all original biome decorators...',flush=True)
    javac(['-encoding','UTF-8','--release','8','-cp',cp,'-d',str(build),*map(str,java),str(root/'tests/java/WorldgenFeatureReference.java'),str(root/'tests/java/WorldgenDecorationReference.java')],build)
    run(['java','-cp',cp,'WorldgenDecorationReference','--sites',str(build/'tulip_sites.txt')],build)
    cases=[]
    for b in data:
        id=b['id'];natural=6 if id==9 else 3 if id==8 else 13 if b['snow'] else 1 if id in [2,17,130] else 4 if id in [14,15] else 10 if id in [6,134] else 14 if b['mesa'] else 7
        for seed in [0,189,9223372036854775807]:
            for pattern in [natural,7 if natural!=7 else 9]:cases.append((id,seed,pattern,0,0,0))
        cases.append((id,1,17,0,0,1))
    for row in (build/'tulip_sites.txt').read_text().splitlines():
        x,z=map(int,row.split())
        for id in [1,129,132]:
            for seed in [0,1]:cases.append((id,seed,7,x,z,0))
    fixture=build/'decoration.fixture';fixture.write_text('\n'.join(' '.join(map(str,case)) for case in cases)+'\n',encoding='ascii')
    executable=build/('worldgen_decoration_tests.exe' if os.name=='nt' else 'worldgen_decoration_tests')
    run([args.cxx,'-std=c++17','-O2','-ffp-contract=off','-fno-fast-math','-I',str(root/'include'),*[str(root/'src'/name) for name in ['world.cpp','worldgen_noise.cpp','worldgen_features.cpp','worldgen_trees.cpp','worldgen_decoration.cpp']],str(root/'tests/worldgen_decoration_tests.cpp'),'-o',str(executable)],root)
    print(f'Comparing {len(cases)} complete biome-decoration calls over all {len(data)} biomes...',flush=True)
    run(['java','-Xmx1g','--add-opens','java.base/java.util=ALL-UNNAMED','-cp',cp,'WorldgenDecorationReference',str(fixture),str(build/'java_decoration.txt')],build)
    run([str(executable),str(fixture),str(build/'native_decoration.txt')],root)
    reference=(build/'java_decoration.txt').read_text().split('END\n');native=(build/'native_decoration.txt').read_text().split('END\n');failures=[]
    for i,case in enumerate(cases):
        a=reference[i] if i<len(reference) else '';b=native[i] if i<len(native) else ''
        if a!=b:
            ar=a.splitlines();br=b.splitlines();first=next((j for j,(u,v) in enumerate(zip(ar,br)) if u!=v),min(len(ar),len(br)))
            failures.append({'case':case,'row':first,'java':ar[first] if first<len(ar) else None,'native':br[first] if first<len(br) else None,'java_rows':len(ar),'native_rows':len(br)})
    changes=sum(value.count('\nB ') for value in reference);ticks=sum(value.count('\nF ') for value in reference);entities=sum(value.count('\nE ') for value in reference)
    report={'biomes':len(data),'operations':len(cases),'changed_blocks':changes,'immediate_tick_callbacks':ticks,'entity_requests':entities,'scope':'all original biome decoration control flow, blocks, fixed light queries, and captured spring forceBlockUpdateTick boundaries; actual server fluid updates are a separate dependency','mismatches':len(failures),'sha256':hashlib.sha256('END\n'.join(reference).encode()).hexdigest(),'failures':failures[:30],'source_sha256':{str(p.relative_to(mcp)):hashlib.sha256(p.read_bytes()).hexdigest() for p in java}}
    (build/'decoration_report.json').write_text(json.dumps(report,indent=2))
    if failures or len(reference)!=len(native):
        for f in failures[:10]:print(json.dumps(f),file=sys.stderr)
        raise RuntimeError(f'{len(failures)} biome decorations differ')
    print(f'PASS: {len(cases)} calls / {len(data)} biomes / {changes} block changes / {ticks} immediate-tick boundaries / {entities} entity requests match Java')

if __name__=='__main__':
    try:main()
    except Exception as error:print(f'Decoration verification failed: {error}',file=sys.stderr);sys.exit(1)
