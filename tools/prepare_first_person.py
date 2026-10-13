#!/usr/bin/env python3
"""Bake native held-item meshes from original 1.8.9 JSON models and sprite edges.

Development/import only. Native runtime consumes models.bin and atlas.rgba;
neither Java, Python nor image libraries are needed by the running client.
"""
from __future__ import annotations
import argparse
import copy
import hashlib
import io
import json
from pathlib import Path
import struct
import sys

import numpy as np
from PIL import Image
import prepare_assets as base
import prepare_render_assets as render


class Resources(render.ResourceFiles):
    def __init__(self, root):
        super().__init__(root)
        self.extra={}
    def __contains__(self, name):
        return name in self.extra or super().__contains__(name)
    def __getitem__(self, name):
        return self.extra[name] if name in self.extra else super().__getitem__(name)
    def image(self, name, image):
        ns,path=base.identifier(name);out=io.BytesIO();image.save(out,format="PNG")
        self.extra[f"assets/{ns}/textures/{path}.png"]=out.getvalue()


def sprite_frames(resources, texture):
    ns,path=base.identifier(texture);filename=f"assets/{ns}/textures/{path}.png"
    image=Image.open(io.BytesIO(resources[filename])).convert("RGBA")
    animated=filename+".mcmeta" in resources and "animation" in json.loads(resources[filename+".mcmeta"])
    if not animated:return [image]
    if image.height%image.width:raise base.AssetError("Invalid item animation strip: "+texture)
    return [image.crop((0,y,image.width,y+image.width)) for y in range(0,image.height,image.width)]


def sprite_elements(frames, texture, tint):
    """ItemModelGenerator, including spans merged across gaps/animation frames."""
    width,height=frames[0].size
    if width<2 or height<2:raise base.AssetError("Generated item sprite must be at least 2x2")
    elements=[{"from":[0,0,7.5],"to":[16,16,8.5],"faces":{
        "south":{"texture":texture,"uv":[0,0,16,16],"tintindex":tint},
        "north":{"texture":texture,"uv":[16,0,0,16],"tintindex":tint}}}]
    spans={};directions=(("up",0,-1),("down",0,1),("left",-1,0),("right",1,0))
    for frame in frames:
        if frame.size!=(width,height):raise base.AssetError("Item animation dimensions changed")
        opaque=np.array(frame)[:,:,3]!=0
        for y in range(height):
            for x in range(width):
                if not opaque[y,x]:continue
                for direction,dx,dy in directions:
                    nx,ny=x+dx,y+dy
                    if 0<=nx<width and 0<=ny<height and opaque[ny,nx]:continue
                    horizontal=direction in ("up","down");anchor=y if horizontal else x;value=x if horizontal else y
                    key=(direction,anchor)
                    if key in spans:spans[key][0]=min(spans[key][0],value);spans[key][1]=max(spans[key][1],value)
                    else:spans[key]=[value,value]
    f=np.float32;gx=f(16)/f(width);gy=f(16)/f(height)
    for (direction,anchor),(low,high) in spans.items():
        low,high,anchor=f(low),f(high),f(anchor)
        if direction in ("up","down"):
            x0=low;x1=f(high+f(1));y0=y1=f(anchor+(1 if direction=="down" else 0))
            u0=low;u1=x1;v0=v1=anchor;ux=f(16)/f(width);vy=f(16)/f(height-1)
        else:
            x0=x1=f(anchor+(1 if direction=="right" else 0));y0=low;y1=f(high+f(1))
            u0=u1=anchor;v0=y1;v1=low;ux=f(16)/f(width-1);vy=f(16)/f(height)
        start=[float(f(x0*gx)),float(f(f(16)-f(y0*gy))),7.5]
        end=[float(f(x1*gx)),float(f(f(16)-f(y1*gy))),8.5]
        uv=[float(f(u0*ux)),float(f(v0*vy)),float(f(u1*ux)),float(f(v1*vy))]
        face={"up":"up","down":"down","left":"east","right":"west"}[direction]
        elements.append({"from":start,"to":end,"faces":{face:{"texture":texture,"uv":uv,"tintindex":tint}}})
    return elements


def item_geometry(model, resources):
    result=copy.deepcopy(model);result["elements"]=[]
    for layer in range(5):
        if "layer"+str(layer) not in model.get("textures",{}):break
        texture=base.ModelResolver.texture(model,"#layer"+str(layer))
        result["elements"].extend(sprite_elements(sprite_frames(resources,texture),texture,layer))
    return result


def camera_transform(model, kind="firstperson"):
    transform=model.get("display",{}).get(kind,{})
    # JSON numbers first become floats; translation and clamps then use floats.
    t=np.clip(np.array(transform.get("translation",[0,0,0]),dtype=np.float32)*np.float32(.0625),-1.5,1.5)
    r=np.array(transform.get("rotation",[0,0,0]),dtype=np.float32)
    s=np.clip(np.array(transform.get("scale",[1,1,1]),dtype=np.float32),-4,4)
    if any(len(v)!=3 or not np.all(np.isfinite(v)) for v in (t,r,s)):raise base.AssetError("Invalid item camera transform")
    return [float(x) for v in (t,r,s) for x in v]


def write_animations(resources, assets, atlas, animations):
    animated=[a for a in animations if a["texture"] not in ("minecraft:items/clock","minecraft:items/compass")]
    out=io.BytesIO();out.write(b"NVANIM01");out.write(struct.pack("<III",atlas["width"],atlas["height"],len(animated)))
    for animation in animated:
        name=animation["texture"];tile=atlas["textures"][name];metadata=animation["metadata"];padding=atlas["padding"]
        frames=sprite_frames(resources,name);sequence=metadata.get("frames") or list(range(len(frames)))
        sequence=[(frame,metadata.get("frametime",1)) if isinstance(frame,int) else (frame["index"],frame.get("time",metadata.get("frametime",1))) for frame in sequence]
        render.string(out,name);out.write(struct.pack("<6IB3x",tile["x"]-padding,tile["y"]-padding,tile["width"]+2*padding,tile["height"]+2*padding,len(frames),len(sequence),bool(metadata.get("interpolate",False))))
        for index,duration in sequence:out.write(struct.pack("<II",index,duration))
        for frame in frames:out.write(render.padded(frame,padding).tobytes())
    base.write_bytes(assets/"runtime/animations/first_person.bin",out.getvalue())
    return len(animated)


def prepare(assets, reference, mcp):
    resources=Resources(assets/"resources");resolver=render.ItemModelResolver(resources)
    registrations=json.loads(reference.read_text(encoding="utf-8"));mappings=list(registrations["mappings"])
    for dynamic in registrations["dynamic"]:
        if dynamic["kind"]=="potion":
            mappings.extend({"id":dynamic["id"],"meta":meta,"model":"minecraft:"+(dynamic["splash"] if meta&16384 else dynamic["normal"]),"colors":[color,0xffffff,0xffffff,0xffffff,0xffffff]} for meta,color in enumerate(dynamic["palette"]))
    missing=base.cube("native:missing");models=[{"name":"native:missing","geometry":missing,"transform":[0,0,0,0,0,0,1,1,1],"ground":[0,0,0,0,0,0,1,1,1],"fixed":[0,0,0,0,0,0,1,1,1],"gui3d":True,"flags":0,"overrides":{}}]
    lookup={};textures={"minecraft:map/map_background"};frame_entries=[]
    def add_model(name, frame=None):
        key=(name,frame)
        if key in lookup:return lookup[key]
        ns,path=base.identifier(name);resolved=resolver.resolve(ns+":item/"+path);builtin=resolved.get("builtin","")
        flat=builtin in ("builtin/generated","builtin/clock","builtin/compass");overrides={};flags=1
        if flat:geometry=item_geometry(resolved,resources)
        elif builtin=="builtin/entity":geometry=render.builtin_item(name,resolved);flags|=2|4
        else:geometry=resolved
        if frame is not None:
            texture=resolver.texture(resolved,"#layer0");image=sprite_frames(resources,texture)[frame]
            derived="native:held/"+path+"_"+str(frame);resources.image(derived,image);overrides[texture]=derived;textures.add(derived)
        if builtin in ("builtin/clock","builtin/compass") and frame is None:flags|=2
        for part in geometry.get("elements",[]):
            for face in part.get("faces",{}).values():textures.add(base.ModelResolver.texture(geometry,face["texture"]))
        index=len(models);lookup[key]=index;models.append({"name":name+("@"+str(frame) if frame is not None else ""),"geometry":geometry,"transform":camera_transform(resolved),"ground":camera_transform(resolved,"ground"),"fixed":camera_transform(resolved,"fixed"),"thirdperson":camera_transform(resolved,"thirdperson"),"gui":camera_transform(resolved,"gui"),"head":camera_transform(resolved,"head"),"gui3d":not flat,"flags":flags,"overrides":overrides})
        return index
    entries=[]
    for mapping in mappings:entries.append({**mapping,"index":add_model(mapping["model"])})
    for name,id in (("clock",347),("compass",345)):
        for frame in range(len(sprite_frames(resources,"minecraft:items/"+name))):frame_entries.append((id<<16|frame,add_model("minecraft:"+name,frame)))
    for entry in entries:
        if entry["id"] in (261,346):frame_entries.append((entry["id"]<<16|entry["meta"],entry["index"]))
    defaults=[]
    for dynamic in registrations["dynamic"]:
        default=next(entry for entry in entries if entry["id"]==dynamic["id"] and entry["meta"]==0);defaults.append((dynamic["id"],default["index"]))
    output=assets/"runtime/first_person";atlas,width,height,animations=base.build_atlas(resources,textures-{"native:missing"},output)
    atlas_data=json.loads((output/"atlas.json").read_text());animation_count=write_animations(resources,assets,atlas_data,animations)
    out=io.BytesIO();out.write(b"NVFP0001");out.write(struct.pack("<6I",width,height,len(models),len(entries),len(defaults),len(frame_entries)))
    # ItemRenderer binds map_background as an ordinary texture (UV 0..1), so
    # preserve its full pixel interval instead of the block-sprite 0.01px inset.
    bg=atlas["minecraft:map/map_background"];out.write(struct.pack("<4f",bg["x"]/width,bg["y"]/height,(bg["x"]+bg["width"])/width,(bg["y"]+bg["height"])/height))
    vertices=0;model_report=[]
    for model in models:
        selected_atlas={**atlas,**{original:atlas[derived] for original,derived in model["overrides"].items()}}
        baked=base.bake_model(model["geometry"],{},selected_atlas,{})
        groups={}
        for quad in baked["quads"]:
            tint=quad["tint_index"];group=groups.setdefault(tint,[]);normal=base.NORMALS[quad["face"]]
            for index in (0,1,2,0,2,3):group.append((*quad["vertices"][index],*normal))
        render.string(out,model["name"]);out.write(struct.pack("<I9fI",model["flags"],*model["transform"],len(groups)))
        for tint,group in groups.items():
            out.write(struct.pack("<iI",tint,len(group)));vertices+=len(group)
            for vertex in group:out.write(struct.pack("<8f",*vertex))
        model_report.append({"name":model["name"],"quads":len(baked["quads"]),"groups":len(groups),"approximate":bool(model["flags"]&2),"firstperson":model["transform"],"ground":model["ground"],"fixed":model["fixed"],"gui3d":model["gui3d"]})
    for entry in entries:out.write(struct.pack("<HHI5I",entry["id"],entry["meta"],entry["index"],*[int(c)|0xff000000 for c in entry["colors"]]))
    for pair in defaults+frame_entries:out.write(struct.pack("<II",*pair))
    base.write_bytes(output/"models.bin",out.getvalue())
    transforms=io.BytesIO();transforms.write(b"NVITRF01");transforms.write(struct.pack("<I",len(models)))
    for model in models:render.string(transforms,model["name"]);transforms.write(struct.pack("<I18f",model["gui3d"],*model["ground"],*model["fixed"]))
    base.write_bytes(output/"transforms.bin",transforms.getvalue())
    equipment=io.BytesIO();equipment.write(b"NVEQP001");equipment.write(struct.pack("<I",len(models)))
    for model in models:
        render.string(equipment,model["name"])
        for kind in ("thirdperson","gui","head"):equipment.write(struct.pack("<9f",*model.get(kind,[0,0,0,0,0,0,1,1,1])))
    base.write_bytes(output/"equipment.bin",equipment.getvalue())
    source_paths=["client/renderer/ItemRenderer.java","client/renderer/entity/RenderItem.java","client/renderer/block/model/ItemModelGenerator.java","client/renderer/block/model/ItemCameraTransforms.java","client/renderer/block/model/ItemTransformVec3f.java","client/renderer/block/model/ModelBlock.java"]
    report={"models":len(models),"vertices":vertices,"mapped_item_ids":len({e["id"] for e in entries}),"metadata_mappings":len(entries),"texture_images":len(atlas),"atlas":[width,height],"animated_sprites":animation_count,"selectable_frames":len(frame_entries),"cache_sha256":hashlib.sha256(out.getvalue()).hexdigest(),"source_sha256":{name:base.source_hash(mcp,name) for name in source_paths},"approximate_models":[m["name"] for m in model_report if m["approximate"]]}
    report["world_transform_models"]=len(models);report["world_transform_sha256"]=hashlib.sha256(transforms.getvalue()).hexdigest()
    base.write_json(output/"report.json",report);base.write_json(output/"models.json",model_report)
    print(json.dumps({k:report[k] for k in ("models","vertices","mapped_item_ids","metadata_mappings","texture_images","atlas","animated_sprites","selectable_frames")},indent=2))


def main():
    root=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets",type=Path,default=root/"assets");parser.add_argument("--mcp",type=Path,default=root.parent/"MCP-919-main")
    parser.add_argument("--reference",type=Path,default=root/"tools/reference/item_render_registry.json")
    args=parser.parse_args();prepare(args.assets.resolve(),args.reference.resolve(),args.mcp.resolve())


if __name__=="__main__":
    try:main()
    except Exception as error:print("First-person asset preparation failed: "+str(error),file=sys.stderr);sys.exit(1)
