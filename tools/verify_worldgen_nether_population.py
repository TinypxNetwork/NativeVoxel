#!/usr/bin/env python3
"""Compare fortress-free Nether population with original provider control flow."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import sys
from export_registry import javac
from verify_physics import run

def main()->None:
    root=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mcp',type=Path,default=root.parent/'MCP-919-main');parser.add_argument('--cxx',default='D:/Strawberry/c/bin/g++.exe' if os.name=='nt' else 'g++');args=parser.parse_args();mcp=args.mcp.resolve();build=root/'build/worldgen_reference';build.mkdir(parents=True,exist_ok=True)
    rng=random.Random(189);seeds=[0,1,-1,189,123456789,-9223372036854775808,9223372036854775807]+[rng.randrange(-9223372036854775808,9223372036854775808) for _ in range(32)];cases=[]
    for seed in seeds:
        for pattern in [3,5,27]:
            for cx,cz in [(0,0),(-7,3)]:cases.append((seed,pattern,cx,cz,12 if seed&1 else 13))
    fixture=build/'nether_population.fixture';fixture.write_text('\n'.join(' '.join(map(str,c)) for c in cases)+'\n',encoding='ascii');cp=os.pathsep.join(str(p) for p in [build,mcp/'bin/minecraft',mcp/'jars/versions/1.8.9/1.8.9.jar',*sorted((mcp/'jars/libraries').rglob('*.jar'))])
    source=mcp/'src/minecraft/net/minecraft';java=[source/'world/gen/ChunkProviderHell.java',source/'world/gen/feature/WorldGenHellLava.java']
    print('Compiling original Nether population reference...',flush=True)
    javac(['-encoding','UTF-8','--release','8','-cp',cp,'-d',str(build),*map(str,java),str(root/'tests/java/WorldgenFeatureReference.java'),str(root/'tests/java/WorldgenNetherPopulationReference.java')],build)
    executable=build/('worldgen_nether_population_tests.exe' if os.name=='nt' else 'worldgen_nether_population_tests')
    run([args.cxx,'-std=c++17','-O2','-ffp-contract=off','-fno-fast-math','-I',str(root/'include'),*[str(root/'src'/name) for name in ['world.cpp','worldgen_noise.cpp','worldgen_features.cpp','worldgen_nether_population.cpp']],str(root/'tests/worldgen_nether_population_tests.cpp'),'-o',str(executable)],root)
    print(f'Comparing {len(cases)} complete Nether feature passes...',flush=True)
    run(['java','-Xmx1g','--add-opens','java.base/java.util=ALL-UNNAMED','-cp',cp,'WorldgenNetherPopulationReference',str(fixture),str(build/'java_nether_population.txt')],build)
    run([str(executable),str(fixture),str(build/'native_nether_population.txt')],root)
    reference=(build/'java_nether_population.txt').read_text().split('END\n');native=(build/'native_nether_population.txt').read_text().split('END\n');failures=[]
    for i,case in enumerate(cases):
        a=reference[i] if i<len(reference) else '';b=native[i] if i<len(native) else ''
        if a!=b:
            ar=a.splitlines();br=b.splitlines();first=next((j for j,(u,v) in enumerate(zip(ar,br)) if u!=v),min(len(ar),len(br)));failures.append({'case':case,'row':first,'java':ar[first] if first<len(ar) else None,'native':br[first] if first<len(br) else None,'java_rows':len(ar),'native_rows':len(br)})
    changes=sum(value.count('\nB ') for value in reference);ticks=sum(value.count('\nF ') for value in reference)
    report={'operations':len(cases),'changed_blocks':changes,'immediate_tick_callbacks':ticks,'scope':'Nether provider feature pass with no fortress starts; supplied light and captured immediate lava-tick boundaries; no simulated fluid approximation','mismatches':len(failures),'sha256':hashlib.sha256('END\n'.join(reference).encode()).hexdigest(),'failures':failures[:20],'source_sha256':{str(p.relative_to(mcp)):hashlib.sha256(p.read_bytes()).hexdigest() for p in java}}
    (build/'nether_population_report.json').write_text(json.dumps(report,indent=2))
    if failures or len(reference)!=len(native):
        for f in failures[:10]:print(json.dumps(f),file=sys.stderr)
        raise RuntimeError(f'{len(failures)} Nether feature passes differ')
    print(f'PASS: {len(cases)} Nether feature passes / {changes} block changes / {ticks} immediate lava-tick boundaries / remaining RNG match Java')

if __name__=='__main__':
    try:main()
    except Exception as error:print(f'Nether population verification failed: {error}',file=sys.stderr);sys.exit(1)
