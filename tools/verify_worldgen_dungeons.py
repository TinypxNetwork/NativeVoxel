#!/usr/bin/env python3
"""Compare original dungeon geometry and actual chest/spawner tile NBT."""
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
    cases=[]
    for seed in [0,1,-1,189,123456789,-9223372036854775808,9223372036854775807]:
        for pattern in range(19,27):
            for y in [1,32,64,250,252]:cases.append((seed,pattern,y,1))
    rng=random.Random(189)
    for _ in range(128):
        seed=rng.randrange(-9223372036854775808,9223372036854775808)
        for pattern in [20,21,26]:cases.append((seed,pattern,64,2))
    fixture=build/'dungeons.fixture';fixture.write_text('\n'.join(' '.join(map(str,c)) for c in cases)+'\n',encoding='ascii');cp=os.pathsep.join(str(p) for p in [build,mcp/'bin/minecraft',mcp/'jars/versions/1.8.9/1.8.9.jar',*sorted((mcp/'jars/libraries').rglob('*.jar'))])
    source=mcp/'src/minecraft/net/minecraft';java=[source/'world/gen/feature/WorldGenDungeons.java',source/'block/BlockChest.java',source/'world/World.java',source/'world/chunk/Chunk.java',source/'tileentity/TileEntityChest.java',source/'tileentity/TileEntityMobSpawner.java']
    print('Compiling original dungeon + server chest callbacks...',flush=True)
    javac(['-encoding','UTF-8','--release','8','-cp',cp,'-d',str(build),*map(str,java),str(root/'tests/java/WorldgenFeatureReference.java'),str(root/'tests/java/WorldgenLootReference.java'),str(root/'tests/java/WorldgenDungeonReference.java')],build)
    executable=build/('worldgen_dungeon_tests.exe' if os.name=='nt' else 'worldgen_dungeon_tests')
    run([args.cxx,'-std=c++17','-O2','-ffp-contract=off','-fno-fast-math','-I',str(root/'include'),*[str(root/'src'/name) for name in ['world.cpp','worldgen_noise.cpp','worldgen_loot.cpp','worldgen_dungeons.cpp']],str(root/'tests/worldgen_dungeon_tests.cpp'),'-o',str(executable)],root)
    print(f'Comparing {len(cases)} dungeon fixtures ({sum(c[-1] for c in cases)} generation attempts)...',flush=True)
    run(['java','-Xmx1g','-cp',cp,'WorldgenDungeonReference',str(fixture),str(build/'java_dungeons.txt')],build)
    run([str(executable),str(fixture),str(build/'native_dungeons.txt')],root)
    reference=(build/'java_dungeons.txt').read_text().split('END\n');native=(build/'native_dungeons.txt').read_text().split('END\n');failures=[]
    for i,case in enumerate(cases):
        a=reference[i] if i<len(reference) else '';b=native[i] if i<len(native) else ''
        if a!=b:
            ar=a.splitlines();br=b.splitlines();first=next((j for j,(u,v) in enumerate(zip(ar,br)) if u!=v),min(len(ar),len(br)))
            failures.append({'case':case,'row':first,'java':ar[first] if first<len(ar) else None,'native':br[first] if first<len(br) else None,'java_rows':len(ar),'native_rows':len(br)})
    changes=sum(value.count('\nB ') for value in reference);tiles=sum(value.count('\nT ') for value in reference)
    report={'fixtures':len(cases),'attempts':sum(c[-1] for c in cases),'changed_blocks':changes,'tile_snapshots':tiles,'scope':'original dungeon blocks, actual chest onBlockAdded facing, complete chest/spawner NBT and RNG; runtime inventory-drop entities when replacing old chests remain external','mismatches':len(failures),'sha256':hashlib.sha256('END\n'.join(reference).encode()).hexdigest(),'failures':failures[:20],'source_sha256':{str(p.relative_to(mcp)):hashlib.sha256(p.read_bytes()).hexdigest() for p in java}}
    (build/'dungeons_report.json').write_text(json.dumps(report,indent=2))
    if failures or len(reference)!=len(native):
        for f in failures[:10]:print(json.dumps(f),file=sys.stderr)
        raise RuntimeError(f'{len(failures)} dungeon fixtures differ')
    print(f'PASS: {len(cases)} fixtures / {report["attempts"]} attempts / {changes} block changes / {tiles} complete tile snapshots / remaining RNG match Java')

if __name__=='__main__':
    try:main()
    except Exception as error:print(f'Dungeon verification failed: {error}',file=sys.stderr);sys.exit(1)
