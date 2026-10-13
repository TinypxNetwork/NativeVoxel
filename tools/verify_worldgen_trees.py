#!/usr/bin/env python3
"""Compare original tree/mushroom generators, including placement failure RNG."""
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
            for pattern in range(13):cases.append((seed,kind,pattern,63,0,0))
    for height in [0,239,244,248,250,253,255]:
        for kind in range(23):
            for pattern in [7,9]:cases.append((189,kind,pattern,height,0,0))
    for seed in range(128):
        for kind in [1,11]:cases.append((seed,kind,7,63,0,0))
    for seed in range(32):
        for kind in range(12,23):
            for pattern in [7,9]:cases.append((seed,kind,pattern,63,0,0))
    for seed in seeds:
        for kind in range(19,23):cases.append((seed,kind,7,63,-29999968,29999968))
    fixture=build/'trees.fixture';fixture.write_text('\n'.join(' '.join(map(str,case)) for case in cases)+'\n',encoding='ascii')
    cp=os.pathsep.join(str(p) for p in [build,mcp/'bin/minecraft',mcp/'jars/versions/1.8.9/1.8.9.jar',*sorted((mcp/'jars/libraries').rglob('*.jar'))])
    source=mcp/'src/minecraft/net/minecraft';names=['WorldGenAbstractTree','WorldGenTrees','WorldGenForest','WorldGenTaiga1','WorldGenTaiga2','WorldGenSwamp','WorldGenShrub','WorldGenBigMushroom','WorldGenSavannaTree','WorldGenCanopyTree','WorldGenHugeTrees','WorldGenMegaJungle','WorldGenMegaPineTree','WorldGenBigTree'];java=[source/'world/gen/feature'/(name+'.java') for name in names]
    print('Compiling original tree/mushroom generators...',flush=True)
    javac(['-encoding','UTF-8','--release','8','-cp',cp,'-d',str(build),*map(str,java),str(root/'tests/java/WorldgenFeatureReference.java'),str(root/'tests/java/WorldgenTreeReference.java')],build)
    executable=build/('worldgen_tree_tests.exe' if os.name=='nt' else 'worldgen_tree_tests')
    run([args.cxx,'-std=c++17','-O2','-ffp-contract=off','-fno-fast-math','-I',str(root/'include'),*[str(root/'src'/name) for name in ['world.cpp','worldgen_noise.cpp','worldgen_trees.cpp']],str(root/'tests/worldgen_tree_tests.cpp'),'-o',str(executable)],root)
    print(f'Comparing {len(cases)} tree/mushroom operations...',flush=True)
    run(['java','-Xmx1g','-cp',cp,'WorldgenTreeReference',str(fixture),str(build/'java_trees.txt')],build)
    run([str(executable),str(fixture),str(build/'native_trees.txt')],root)
    reference=(build/'java_trees.txt').read_text().split('END\n');native=(build/'native_trees.txt').read_text().split('END\n');failures=[]
    for i,case in enumerate(cases):
        a=reference[i] if i<len(reference) else '';b=native[i] if i<len(native) else ''
        if a!=b:
            ar=a.splitlines();br=b.splitlines();first=next((j for j,(u,v) in enumerate(zip(ar,br)) if u!=v),min(len(ar),len(br)))
            failures.append({'case':case,'row':first,'java':ar[first] if first<len(ar) else None,'native':br[first] if first<len(br) else None,'java_rows':len(ar),'native_rows':len(br)})
    rows=sum(len(value.splitlines()) for value in reference);changes=sum(value.count('\nB ') for value in reference)
    report={'generators':names,'operations':len(cases),'rows':rows,'changed_blocks':changes,'mismatches':len(failures),'sha256':hashlib.sha256('END\n'.join(reference).encode()).hexdigest(),'failures':failures[:20],'source_sha256':{str(p.relative_to(mcp)):hashlib.sha256(p.read_bytes()).hexdigest() for p in java}}
    (build/'trees_report.json').write_text(json.dumps(report,indent=2))
    if failures or len(reference)!=len(native):
        for f in failures[:10]:print(json.dumps(f),file=sys.stderr)
        raise RuntimeError(f'{len(failures)} tree operations differ')
    print(f'PASS: {len(cases)} tree/mushroom operations / {changes} block changes / all return values and remaining Random states match Java')

if __name__=='__main__':
    try:main()
    except Exception as error:print(f'Tree verification failed: {error}',file=sys.stderr);sys.exit(1)
