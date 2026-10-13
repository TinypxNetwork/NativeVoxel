#!/usr/bin/env python3
"""Exercise the C++ runtime pack loader; Python only constructs test fixtures."""
from __future__ import annotations
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import time
import zipfile
from PIL import Image

def png(color,size=(16,16)):
    output=io.BytesIO();Image.new('RGBA',size,color).save(output,format='PNG');return output.getvalue()

def block_signatures(path):
    """Compare source-derived meshes independently of atlas placement/model IDs."""
    data=(path/'assets.bin').read_bytes();rects=json.loads((path/'atlas.json').read_text())['textures'];at=36;models=[]
    def string():
        nonlocal at
        size,=struct.unpack_from('<I',data,at);at+=4;value=data[at:at+size].decode();at+=size;return value
    for _ in range(struct.unpack_from('<I',data,24)[0]):
        flags=data[at:at+4];count,=struct.unpack_from('<I',data,at+4);at+=8;quads=[]
        for _ in range(count):
            header=struct.unpack_from('<bbhB3x3f',data,at);vertices=[struct.unpack_from('<5f',data,at+20+i*20) for i in range(4)];at+=100
            u0=min(v[3] for v in vertices);u1=max(v[3] for v in vertices);v0=min(v[4] for v in vertices);v1=max(v[4] for v in vertices)
            texture,tile=next((name,t) for name,t in rects.items() if u0>=t['u0']-2e-6 and u1<=t['u1']+2e-6 and v0>=t['v0']-2e-6 and v1<=t['v1']+2e-6)
            normalized=[tuple(round(n,5) for n in (*v[:3],(v[3]-tile['u0'])/(tile['u1']-tile['u0']),(v[4]-tile['v0'])/(tile['v1']-tile['v0']))) for v in vertices]
            # Cyclic changes preserve winding; reversing a quad does not.
            winding=min(tuple(normalized[i:]+normalized[:i]) for i in range(4))
            quads.append((tuple(round(v,5) for v in header),texture,winding))
        models.append((flags,tuple(sorted(quads))))
    variants={}
    for _ in range(struct.unpack_from('<I',data,28)[0]):
        resource=string();properties=string();count,=struct.unpack_from('<I',data,at);at+=4;choices=[]
        for _ in range(count):
            model,weight=struct.unpack_from('<II',data,at);at+=8;choices.append((models[model],weight))
        variants[(resource,properties)]=choices
    return variants

def main():
    root=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--build-dir',type=Path,default=root/'build/resource_pack_reference');args=parser.parse_args();build=args.build_dir.resolve();build.mkdir(parents=True,exist_ok=True);work=build/str(time.time_ns());work.mkdir();cache=work/'cache';base=root/'assets'
    executable=build/'resource_packs_tests.exe'
    cmd=['D:/Strawberry/c/bin/g++.exe','-std=c++17','-O2','-ffp-contract=off','-fno-fast-math','-I',str(root/'include'),*[str(root/'src'/p) for p in ['assets.cpp','render_assets.cpp','resource_packs.cpp','first_person.cpp','skins.cpp']],str(root/'tests/resource_packs_tests.cpp'),'-lstdc++fs','-lz','-lole32','-lwindowscodecs','-lshell32','-lwinhttp','-lcrypto','-o',str(executable)]
    subprocess.run(cmd,cwd=root,check=True)
    # The tested program has no Python/JVM on PATH and invokes no subprocesses.
    env=dict(os.environ);env['PATH']='D:/Strawberry/c/bin;C:/Windows/system32;C:/Windows'
    def run(*args,ok=True):
        result=subprocess.run([str(executable),*map(str,args)],cwd=root,env=env,encoding='utf-8',capture_output=True)
        if (result.returncode==0)!=ok:raise RuntimeError(result.stdout+'\n'+result.stderr)
        return result.stdout.strip().splitlines()
    low=work/'folder_pack';low.mkdir();(low/'pack.mcmeta').write_text(json.dumps({'pack':{'pack_format':1,'description':'Low priority'}}))
    low_files={'assets/minecraft/textures/blocks/stone.png':png((220,0,0,255)),
               'assets/minecraft/textures/blocks/stone.png.mcmeta':b'{"animation":{"frametime":4}}',
               'assets/minecraft/textures/gui/widgets.png':png((0,0,200,255),(256,256)),
               'assets/minecraft/sounds.json':b'{"native.test":{"sounds":["random/click"]}}'}
    for name,data in low_files.items():p=low/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
    font=Image.open(base/'resources/assets/minecraft/textures/font/ascii.png').convert('RGBA');mapping=struct.unpack('<256H',(base/'runtime/font/ascii-map.bin').read_bytes());index=mapping.index(65);x=index%16*8;y=index//16*8;font.paste((0,0,0,0),(x,y,x+8,y+8));font.paste((255,255,255,255),(x,y,x+2,y+7));font_bytes=io.BytesIO();font.save(font_bytes,format='PNG')
    high=work/'高优先级.zip';high_files={'pack.mcmeta':json.dumps({'pack':{'pack_format':1,'description':'High priority'}}).encode(),
        'assets/minecraft/textures/blocks/stone.png':png((0,230,0,255),(32,32)),
        'assets/minecraft/textures/items/apple.png':png((240,200,0,255),(32,32)),
        'assets/minecraft/textures/models/armor/diamond_layer_1.png':png((12,34,56,255),(64,32)),
        'assets/minecraft/textures/environment/sun.png':png((220,180,20,255)),
        'assets/minecraft/textures/environment/clouds.png':png((90,100,110,180)),
        'assets/minecraft/textures/environment/end_sky.png':png((20,10,50,255)),
        'assets/minecraft/mcpatcher/sky/world0/sky1.properties':b'source=./day.png\nstartFadeIn=6:00\nendFadeIn=7:00\nstartFadeOut=18:00\nendFadeOut=19:00\n',
        'assets/minecraft/mcpatcher/sky/world0/day.png':png((60,120,180,255),(48,32)),
        'assets/minecraft/textures/entity/steve.png':png((100,110,120,255),(64,32)),
        'assets/minecraft/textures/font/ascii.png':font_bytes.getvalue(),
        'assets/minecraft/textures/gui/title/background/panorama_0.png':png((50,100,150,255),(512,512)),
        'assets/minecraft/textures/misc/unknown_server.png':png((10,20,30,255),(64,64)),
        'assets/minecraft/sounds.json':b'{"native.test":{"replace":true,"sounds":["random/click"]}}'}
    parent={'textures':{'surface':'blocks/stone'},'elements':[{'from':[0,0,0],'to':[16,8,16],'faces':{face:{'texture':'#surface','cullface':face} for face in ('down','up','north','south','west','east')}}]}
    high_files.update({
        'assets/minecraft/models/block/native_pack_parent.json':json.dumps(parent).encode(),
        'assets/minecraft/models/block/native_pack_slab.json':b'{"parent":"block/native_pack_parent"}',
        'assets/minecraft/blockstates/stone.json':b'{"variants":{"normal":{"model":"native_pack_slab"}}}',
        'assets/minecraft/models/item/stone.json':b'{"parent":"block/native_pack_slab","display":{"firstperson":{"translation":[1,2,3]},"ground":{"scale":[0.5,0.5,0.5]}}}',
        'assets/minecraft/models/item/apple.json':b'{"parent":"builtin/generated","textures":{"layer0":"packtest:items/apple"}}',
        'assets/packtest/textures/items/apple.png':png((230,20,200,255),(32,32)),
        'assets/minecraft/textures/blocks/destroy_stage_0.png':png((20,40,60,180)),
    })
    with zipfile.ZipFile(high,'w',compression=zipfile.ZIP_DEFLATED) as archive:
        for name,data in high_files.items():archive.writestr(name,data)
    observed=run('inspect',high);assert observed[0].startswith('1 ')
    watched=[base/'assets.bin',base/'atlas.rgba',base/'runtime/items/icons.bin',base/'resources/assets/minecraft/textures/blocks/stone.png']
    before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in watched}
    print('Preparing native folder + ZIP overrides...',flush=True)
    lines=run('prepare',base,cache,low,high);selected=Path(lines[0]);assert lines[1].split()[-1]=='3'
    assert int(lines[2].split()[0])>0
    atlas=json.loads((selected/'atlas.json').read_text());stone=atlas['textures']['minecraft:blocks/stone'];pixels=(selected/'atlas.rgba').read_bytes();offset=(stone['y']*atlas['width']+stone['x'])*4;assert pixels[offset:offset+4]==bytes([0,230,0,255]);assert stone['width']==32
    for name,size,color in [('widgets',(256,256),(0,0,200,255)),('panorama_0',(512,512),(50,100,150,255)),('unknown_server',(64,64),(10,20,30,255))]:
        data=(selected/'runtime/gui'/f'{name}.rgba').read_bytes();assert data[:4]==b'NVTX';assert struct.unpack_from('<II',data,4)==size;assert data[12:16]==bytes(color)
    assert (selected/'runtime/gui/explosion.rgba').is_file() and (selected/'runtime/gui/footprint.rgba').is_file()
    assert (selected/'runtime/equipment/diamond_layer_1.rgba').read_bytes()[12:16]==bytes([12,34,56,255])
    for name,color in [('sun',(220,180,20,255)),('clouds',(90,100,110,180)),('end_sky',(20,10,50,255))]:
        assert (selected/'runtime/gui'/f'{name}.rgba').read_bytes()[12:16]==bytes(color)
    assert (selected/'runtime/sky/sky_0.rgba').read_bytes()[12:16]==bytes([60,120,180,255])
    assert len((selected/'runtime/sky/index.txt').read_text().splitlines())==1
    assert struct.unpack_from('<II',(selected/'runtime/entities/minecraft/steve.rgba').read_bytes(),4)==(64,64)
    assert Path(run('prepare',base,cache,low,high)[0])==selected, 'Unchanged selection must reuse its native cache'
    assert not (selected/'resources/assets/minecraft/textures/blocks/stone.png.mcmeta').exists(), 'A lower-priority metadata file cannot animate a higher-priority replacement'
    assert (selected/'runtime/tiles/destroy_stage_0.rgba').read_bytes()[12:16]==bytes([20,40,60,180])
    # Verify the pack's inherited geometry in both native caches.
    def read_string(data,offset):
        length,=struct.unpack_from('<I',data,offset);offset+=4
        return data[offset:offset+length].decode(),offset+length
    data=(selected/'assets.bin').read_bytes();model_count,variant_count,state_count=struct.unpack_from('<III',data,24);offset=36;models=[]
    for _ in range(model_count):
        quads,=struct.unpack_from('<I',data,offset+4);offset+=8;vertices=[]
        for _ in range(quads):vertices.extend(struct.unpack_from('<5f',data,offset+20+i*20) for i in range(4));offset+=100
        models.append(vertices)
    variants=[]
    for _ in range(variant_count):
        resource,offset=read_string(data,offset);properties,offset=read_string(data,offset);choices,=struct.unpack_from('<I',data,offset);offset+=4
        variants.append((resource,properties,[struct.unpack_from('<II',data,offset+i*8) for i in range(choices)]));offset+=choices*8
    stone_variant=next(v for v in variants if v[:2]==('minecraft:stone','normal'));stone_model=models[stone_variant[2][0][0]];assert len(stone_model)==24 and max(v[1] for v in stone_model)==.5
    data=(selected/'runtime/first_person/models.bin').read_bytes();count,=struct.unpack_from('<I',data,16);offset=48;held_stone=None
    for _ in range(count):
        name,offset=read_string(data,offset);transform=struct.unpack_from('<9f',data,offset+4);groups,=struct.unpack_from('<I',data,offset+40);offset+=44;vertices=[]
        for _ in range(groups):
            tint,number=struct.unpack_from('<iI',data,offset);offset+=8;vertices.extend(struct.unpack_from('<8f',data,offset+i*32) for i in range(number));offset+=number*32
        if name=='minecraft:stone':held_stone=(transform,vertices)
    assert held_stone and held_stone[0][:3]==(1/16,2/16,3/16) and max(v[1] for v in held_stone[1])==.5
    held_atlas=json.loads((selected/'runtime/first_person/atlas.json').read_text());apple=held_atlas['textures']['packtest:items/apple'];held_pixels=(selected/'runtime/first_person/atlas.rgba').read_bytes();offset=(apple['y']*held_atlas['width']+apple['x'])*4;assert held_pixels[offset:offset+4]==bytes([230,20,200,255])
    item_regions=json.loads((selected/'runtime/items/regions.json').read_text());assert 'packtest:items/apple' in item_regions
    stone_key=next(key for key in item_regions if key.startswith('model:minecraft:stone:'));item_pixels=(selected/'runtime/items/atlas.rgba').read_bytes();item_data=(selected/'runtime/items/icons.bin').read_bytes();iw,ih=struct.unpack_from('<II',item_data,8);u0,v0,u1,v1=item_regions[stone_key];rect=Image.frombytes('RGBA',(iw,ih),item_pixels).crop((round(u0*iw),round(v0*ih),round(u1*iw),round(v1*ih)));assert any(g>0 and r==0 and b==0 and a>0 for r,g,b,a in struct.iter_unpack('4B',rect.tobytes())), '3-D inventory icon must use the selected stone texture'
    layers=(selected/'runtime/audio/sounds-layers.txt').read_text().splitlines();assert len(layers)>=3
    for layer in layers:assert (selected/layer.split('\t')[1]).is_file()
    assert run('prepare',base,cache)[0]==str(base)
    after={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in watched};assert before==after
    malicious=['../escape.png','/absolute.png','C:/drive.png','assets/../escape.png','assets\\escape.png','assets/minecraft/CON.txt']
    for i,name in enumerate(malicious):
        path=work/f'bad_path_{i}.zip'
        with zipfile.ZipFile(path,'w') as archive:archive.writestr('pack.mcmeta',high_files['pack.mcmeta']);archive.writestr(name,b'bad')
        # ZipInfo normalizes backslashes on Windows; restore the hostile raw
        # filename in both headers so the fixture tests the C++ path check.
        if '\\' in name:path.write_bytes(path.read_bytes().replace(name.replace('\\','/').encode(),name.encode()))
        run('inspect',path,ok=False)
    corrupt=work/'bad_crc.zip'
    with zipfile.ZipFile(corrupt,'w',compression=zipfile.ZIP_STORED) as archive:
        archive.writestr('pack.mcmeta',high_files['pack.mcmeta']);archive.writestr('assets/minecraft/textures/blocks/stone.png',png((1,2,3,255)))
    data=bytearray(corrupt.read_bytes());loc=data.find(b'PK\x03\x04',4);name_size,extra=struct.unpack_from('<HH',data,loc+26);data[loc+30+name_size+extra+12]^=1;corrupt.write_bytes(data)
    run('prepare',base,cache,corrupt,ok=False)
    broken=work/'broken_model';broken.mkdir();(broken/'pack.mcmeta').write_bytes(high_files['pack.mcmeta']);model=broken/'assets/minecraft/models/block/stone.json';model.parent.mkdir(parents=True);model.write_bytes(b'{"parent":"block/stone"}')
    run('rollback',base,cache,selected,broken)
    assert not list(cache.glob('.staging_*'))
    base_variants=block_signatures(base);selected_variants=block_signatures(selected);unchanged={key:value for key,value in base_variants.items() if key[0]!='minecraft:stone'}
    differences=[key for key,value in unchanged.items() if selected_variants[key]!=value];assert not differences, f'Native rebake changed unmodified source models: {differences[:10]}'
    report={'native_runtime':True,'folder_and_deflated_zip':True,'hd_terrain_uv_repacked':True,'inherited_block_and_held_models_rebaked':True,'flat_and_3d_item_models_rebuilt':True,'held_atlas_and_transforms_reloaded':True,'tile_textures_reloaded':True,'failed_reload_preserves_selection':True,'unmodified_model_variants':len(unchanged),'gui_panorama_font_overrides':True,'audio_layers_preserved':len(layers),'unsafe_paths_rejected':len(malicious),'crc_rejected':True,'base_unchanged':True,'selected_cache':str(selected)}
    (build/'report.json').write_text(json.dumps(report,indent=2));print('PASS: '+json.dumps(report),flush=True)

if __name__=='__main__':main()
