#!/usr/bin/env python3
"""Compare held sprite extrusion and baked face UV/winding with original Java."""
from pathlib import Path
import argparse,hashlib,io,json,os,subprocess,sys
import numpy as np
from PIL import Image
import prepare_assets as base
import prepare_render_assets as render
import prepare_first_person as first
import struct


def main():
    root=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--mcp",type=Path,default=root.parent/"MCP-919-main");args=parser.parse_args();mcp=args.mcp.resolve()
    build=root/"build/first_person_reference";classes=build/"geometry_classes";classes.mkdir(parents=True,exist_ok=True)
    cp=os.pathsep.join(map(str,[classes,mcp/"bin/minecraft",mcp/"jars/versions/1.8.9/1.8.9.jar",*sorted((mcp/"jars/libraries").rglob("*.jar"))]))
    names=["ItemModelGenerator","FaceBakery","BlockPart","BlockPartFace","BlockFaceUV"]
    source=mcp/"src/minecraft/net/minecraft/client/renderer/block/model"
    subprocess.run(["javac","-encoding","UTF-8","--release","8","-cp",cp,"-d",str(classes),*[str(source/(name+".java")) for name in names],str(root/"tests/java/GeneratedItemReference.java")],cwd=build,check=True)
    resources=render.ResourceFiles(root/"assets/resources");resolver=render.ItemModelResolver(resources)
    registrations=json.loads((root/"tools/reference/item_render_registry.json").read_text());textures=set()
    for name in {entry["model"] for entry in registrations["mappings"]}:
        ns,path=base.identifier(name);model=resolver.resolve(ns+":item/"+path)
        if model.get("builtin","") not in ("builtin/generated","builtin/clock","builtin/compass"):continue
        for layer in range(5):
            if "layer"+str(layer) not in model["textures"]:break
            textures.add(resolver.texture(model,"#layer"+str(layer)))
    samples={name:first.sprite_frames(resources,name) for name in sorted(textures)}
    # Deliberate corner cases absent from many vanilla sprites: disjoint runs
    # sharing a row, alpha=1, transparent holes, non-square images and changing
    # interior boundaries across animation frames.
    for width,height in ((4,4),(7,11),(32,16)):
        patterns=[]
        for mode in range(4):
            pixels=np.zeros((height,width,4),dtype=np.uint8);pixels[:,:,:3]=[23,47,131]
            for y in range(height):
                for x in range(width):
                    pixels[y,x,3]=255 if mode==0 or (x+y+mode)%3==0 else 1 if mode==1 and (x*y)%4==0 else 0
            patterns.append(Image.fromarray(pixels))
        for mode,image in enumerate(patterns):samples[f"native:test/{width}_{height}_{mode}"]=[image]
        samples[f"native:test/{width}_{height}_animation"]=patterns[1:]
    binary=io.BytesIO();binary.write(b"NVSPRIT1");binary.write(struct.pack(">I",len(samples)))
    for name,frames in samples.items():
        encoded=name.encode();binary.write(struct.pack(">I",len(encoded)));binary.write(encoded);binary.write(struct.pack(">III",*frames[0].size,len(frames)))
        for frame in frames:
            p=np.array(frame,dtype=np.uint32);pixels=(p[:,:,3]<<24)|(p[:,:,0]<<16)|(p[:,:,1]<<8)|p[:,:,2];binary.write(pixels.astype(">u4").tobytes())
    fixture=build/"sprites.bin";fixture.write_bytes(binary.getvalue());target=build/"java_sprites.json"
    subprocess.run(["java","-Djava.awt.headless=true","-cp",cp,"GeneratedItemReference",str(fixture),str(target)],cwd=build,check=True)
    references=json.loads(target.read_text());checks=0;maximum=0;faces_count=0;failures=[]
    def equal(a,b,label,tolerance=2e-6):
        nonlocal checks,maximum
        checks+=1;delta=float(np.max(np.abs(np.array(a,dtype=np.float64)-np.array(b,dtype=np.float64))));maximum=max(maximum,delta)
        if delta>tolerance:failures.append({"label":label,"native":a,"java":b,"difference":delta})
    for reference in references:
        name=reference["name"];frames=samples[name];elements=first.sprite_elements(frames,name,0)
        if len(elements)!=len(reference["elements"]):raise RuntimeError(f"{name}: {len(elements)} native elements vs {len(reference['elements'])} Java")
        width,height=frames[0].size;atlas={name:{"u0":(16+.01)/1024,"v0":(32+.01)/1024,"u1":(16+width-.01)/1024,"v1":(32+height-.01)/1024}}
        for index,(native,java) in enumerate(zip(elements,reference["elements"])):
            label=f"{name} element {index}";equal(native["from"],java["from"],label+" from");equal(native["to"],java["to"],label+" to")
            if set(native["faces"])!=set(java["faces"]):raise RuntimeError(label+" face direction mismatch")
            for direction,face in native["faces"].items():
                expected=java["faces"][direction];equal(face["uv"],expected["uv"],label+" raw UV")
                baked=base.bake_model({"elements":[{**native,"faces":{direction:face}}]}, {},atlas,{})["quads"][0]
                if baked["face"]!=expected["bakedFace"]:raise RuntimeError(label+" baked face direction mismatch")
                equal(baked["vertices"],expected["vertices"],label+" baked positions/UVs");faces_count+=1
    report={"sprites":len(references),"vanilla_textures":len(textures),"synthetic_cases":len(samples)-len(textures),"baked_faces":faces_count,"comparisons":checks,"maximum_absolute_difference":maximum,"tolerance":2e-6,"source_sha256":{name:hashlib.sha256((source/(name+".java")).read_bytes()).hexdigest() for name in names},"failures":failures}
    (build/"geometry_report.json").write_text(json.dumps(report,indent=2)+"\n")
    if failures:
        for item in failures[:8]:print(json.dumps(item),file=sys.stderr)
        raise RuntimeError(f"{len(failures)} generated item geometry differences")
    base.write_json(root/"tools/reference/first_person_geometry.json",{k:v for k,v in report.items() if k!="failures"})
    print("PASS: "+json.dumps({k:report[k] for k in ("sprites","baked_faces","comparisons","maximum_absolute_difference")}))


if __name__=="__main__":main()
