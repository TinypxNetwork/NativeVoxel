#!/usr/bin/env python3
"""Compare enchanted books, dungeon chests and spawner typed NBT against Java."""
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
    mixed=random.Random(189);seeds=[mixed.randrange(-9223372036854775808,9223372036854775808) for _ in range(4096)];cases=[f'B {seed} 30' for seed in [*range(4096),*seeds]]
    for power in [0,1,5,15,29,31,50,100,300,32767]:
        for seed in [0,1,-1,189,123456789,-9223372036854775808,9223372036854775807]:cases.append(f'B {seed} {power}')
    for i,seed in enumerate(seeds[:2048]):cases.append(f'C {seed} {i%32-16} {i%256} {-i%32}')
    for i,seed in enumerate(seeds[:256]):cases.append(f'S {seed} {i%32-16} {i%256} {-i%32}')
    fixture=build/'loot.fixture';fixture.write_text('\n'.join(cases)+'\n',encoding='ascii');cp=os.pathsep.join(str(p) for p in [build,mcp/'bin/minecraft',mcp/'jars/versions/1.8.9/1.8.9.jar',*sorted((mcp/'jars/libraries').rglob('*.jar'))])
    source=mcp/'src/minecraft/net/minecraft';java=[*sorted((source/'enchantment').glob('*.java')),source/'world/gen/feature/WorldGenDungeons.java',source/'item/ItemEnchantedBook.java',source/'util/WeightedRandomChestContent.java',source/'tileentity/TileEntityChest.java',source/'tileentity/MobSpawnerBaseLogic.java']
    print('Compiling original loot/enchantment reference...',flush=True)
    javac(['-encoding','UTF-8','--release','8','-cp',cp,'-d',str(build),*map(str,java),str(root/'tests/java/WorldgenLootReference.java')],build)
    executable=build/('worldgen_loot_tests.exe' if os.name=='nt' else 'worldgen_loot_tests')
    run([args.cxx,'-std=c++17','-O2','-ffp-contract=off','-fno-fast-math','-I',str(root/'include'),str(root/'src/worldgen_noise.cpp'),str(root/'src/worldgen_loot.cpp'),str(root/'tests/worldgen_loot_tests.cpp'),'-o',str(executable)],root)
    print(f'Comparing {len(cases)} complete typed-NBT loot results and remaining Random states...',flush=True)
    run(['java','-Xmx1g','-cp',cp,'WorldgenLootReference',str(fixture),str(build/'java_loot.txt')],build)
    run([str(executable),str(fixture),str(build/'native_loot.txt')],root)
    reference=(build/'java_loot.txt').read_text().splitlines();native=(build/'native_loot.txt').read_text().splitlines();failures=[]
    for i,(a,b) in enumerate(zip(reference,native)):
        if a!=b:
            first=next((j for j,(u,v) in enumerate(zip(a,b)) if u!=v),min(len(a),len(b)));failures.append({'case':cases[i],'first_character':first,'java':a[max(0,first-40):first+100],'native':b[max(0,first-40):first+100]})
    report={'results':len(cases),'books':sum(c.startswith('B') for c in cases),'chests':sum(c.startswith('C') for c in cases),'spawners':sum(c.startswith('S') for c in cases),'mismatches':len(failures),'sha256':hashlib.sha256('\n'.join(reference).encode()).hexdigest(),'failures':failures[:30],'source_sha256':{str(p.relative_to(mcp)):hashlib.sha256(p.read_bytes()).hexdigest() for p in java}}
    (build/'loot_report.json').write_text(json.dumps(report,indent=2))
    if failures or len(reference)!=len(native):
        for f in failures[:10]:print(json.dumps(f),file=sys.stderr)
        raise RuntimeError(f'{len(failures)} loot results differ')
    print(f'PASS: {len(cases)} books/chests/spawners have exact typed NBT, enchantment/slot order and remaining Random state')

if __name__=='__main__':
    try:main()
    except Exception as error:print(f'Loot verification failed: {error}',file=sys.stderr);sys.exit(1)
