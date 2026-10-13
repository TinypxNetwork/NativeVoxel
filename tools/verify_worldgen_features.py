#!/usr/bin/env python3
"""Compare individual original decoration generators on controlled block fixtures."""
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
    seeds=[0,1,-1,189,123456789,-9223372036854775808,9223372036854775807];cases=[]
    for seed in seeds:
        for kind in range(23):
            for pattern in range(7):cases.append((seed,kind,pattern,63,0,0,pattern==3,13 if seed&1 else 12))
    for height in [0,3,253,254,255]:
        for kind in range(23):
            pattern=3 if kind in [14,15,16] else 6 if kind==21 else 5 if kind==22 else 2 if kind in [8,11,17,18,19] else 1 if kind in [4,7] else 0
            for noSky in [0,1]:cases.append((189,kind,pattern,height,0,0,noSky,13))
    for seed in seeds:
        for kind in [21,22]:cases.append((seed,kind,6 if kind==21 else 5,63,-29999968,29999968,1,0))
        # Center these attempts in water rather than the dry ridge at (0,0).
        for kind in [17,18,19]:cases.append((seed,kind,2,63,16,0,0,15))
        for kind in range(23,27):
            for pattern in [0,1,5,13,14,15,16]:cases.append((seed,kind,pattern,63,0,0,0,15))
    for height in [0,3,127,253,254,255]:
        for kind in range(23,27):
            for pattern in [1,13,15,16]:cases.append((189,kind,pattern,height,0,0,0,15))
    for seed in range(256):cases.append((seed,23,13,80,0,0,0,15))
    cases=[tuple(int(v) for v in case) for case in cases];fixture=build/'features.fixture';fixture.write_text('\n'.join(' '.join(map(str,case)) for case in cases)+'\n',encoding='ascii')
    cp=os.pathsep.join(str(p) for p in [build,mcp/'bin/minecraft',mcp/'jars/versions/1.8.9/1.8.9.jar',*sorted((mcp/'jars/libraries').rglob('*.jar'))])
    source=mcp/'src/minecraft/net/minecraft';names=['WorldGenFlowers','WorldGenTallGrass','WorldGenDeadBush','WorldGenDoublePlant','WorldGenCactus','WorldGenReed','WorldGenPumpkin','WorldGenMelon','WorldGenWaterlily','WorldGenFire','WorldGenGlowStone1','WorldGenGlowStone2','WorldGenClay','WorldGenSand','WorldGenBlockBlob','WorldGenSpikes','WorldGenIceSpike','WorldGenIcePath','WorldGenDesertWells','WorldGenVines'];java=[source/'world/gen/feature'/(name+'.java') for name in names]
    java+=[source/'world/gen/GeneratorBushFeature.java']+[source/'block'/(name+'.java') for name in ['BlockBush','BlockTallGrass','BlockDeadBush','BlockDoublePlant','BlockCactus','BlockReed','BlockPumpkin','BlockLilyPad','BlockMushroom','BlockVine']]
    print('Compiling original feature generators...',flush=True)
    javac(['-encoding','UTF-8','--release','8','-cp',cp,'-d',str(build),*map(str,java),str(root/'tests/java/WorldgenFeatureReference.java')],build)
    executable=build/('worldgen_feature_tests.exe' if os.name=='nt' else 'worldgen_feature_tests')
    run([args.cxx,'-std=c++17','-O2','-ffp-contract=off','-fno-fast-math','-I',str(root/'include'),*[str(root/'src'/name) for name in ['world.cpp','worldgen_noise.cpp','worldgen_features.cpp']],str(root/'tests/worldgen_feature_tests.cpp'),'-o',str(executable)],root)
    print(f'Comparing {len(cases)} feature operations, including height limits and placement failures...',flush=True)
    run(['java','-Xmx1g','-cp',cp,'WorldgenFeatureReference',str(fixture),str(build/'java_features.txt')],build)
    run([str(executable),str(fixture),str(build/'native_features.txt')],root)
    reference=(build/'java_features.txt').read_text().split('END\n');native=(build/'native_features.txt').read_text().split('END\n');failures=[]
    for i,case in enumerate(cases):
        a=reference[i] if i<len(reference) else '';b=native[i] if i<len(native) else ''
        if a!=b:
            ar=a.splitlines();br=b.splitlines();first=next((j for j,(u,v) in enumerate(zip(ar,br)) if u!=v),min(len(ar),len(br)))
            failures.append({'case':case,'row':first,'java':ar[first] if first<len(ar) else None,'native':br[first] if first<len(br) else None,'java_rows':len(ar),'native_rows':len(br)})
    rows=sum(len(value.splitlines()) for value in reference);changes=sum(value.count('\nB ') for value in reference)
    report={'features':names+['GeneratorBushFeature'],'operations':len(cases),'rows':rows,'changed_blocks':changes,'scope':'feature block geometry, placement predicates with supplied light, return values, spawned entity placement, Random consumption','mismatches':len(failures),'sha256':hashlib.sha256('END\n'.join(reference).encode()).hexdigest(),'failures':failures[:20],'source_sha256':{str(p.relative_to(mcp)):hashlib.sha256(p.read_bytes()).hexdigest() for p in java}}
    (build/'features_report.json').write_text(json.dumps(report,indent=2))
    if failures or len(reference)!=len(native):
        for f in failures[:10]:print(json.dumps(f),file=sys.stderr)
        raise RuntimeError(f'{len(failures)} feature operations differ')
    print(f'PASS: {len(cases)} feature operations / {changes} block changes / all return values, entity placements and remaining Random states match Java')

if __name__=='__main__':
    try:main()
    except Exception as error:print(f'Feature verification failed: {error}',file=sys.stderr);sys.exit(1)
