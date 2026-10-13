#!/usr/bin/env python3
"""Bake native GUI item icons, exact font metrics and terrain animation frames.

Consumes checked-in Java reference registrations; normal builds need Python,
Pillow and NumPy, not Java. Run after prepare_assets.py.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
from pathlib import Path
import re
import struct
import sys

import numpy as np
from PIL import Image

import prepare_assets as base


class ResourceFiles:
    def __init__(self, root):
        self.root=Path(root)
        self.cache={}
    def __contains__(self, name):
        return (self.root/name).is_file()
    def __getitem__(self, name):
        if name not in self.cache:
            self.cache[name]=(self.root/name).read_bytes()
        return self.cache[name]


class ItemModelResolver(base.ModelResolver):
    """ModelBlock inherits each non-default camera transform independently."""
    def __init__(self, resources):
        super().__init__(resources)
        self.display_resolved=set()
    def resolve(self, name, stack=()):
        model=super().resolve(name,stack)
        ns,path=base.identifier(name);key=ns+":"+path
        if key in self.display_resolved or path.startswith("builtin/"):
            return model
        raw=json.loads(self.resources[f"assets/{ns}/models/{path}.json"])
        inherited={}
        if raw.get("parent"):
            inherited=dict(self.resolve(base.texture_name(raw["parent"])).get("display",{}))
        for kind,transform in raw.get("display",{}).items():
            if (transform.get("rotation",[0,0,0])!=[0,0,0] or transform.get("translation",[0,0,0])!=[0,0,0]
                    or transform.get("scale",[1,1,1])!=[1,1,1]):
                inherited[kind]=transform
        model["display"]=inherited
        self.display_resolved.add(key)
        return model


def string(out, value):
    data=value.encode("utf-8");out.write(struct.pack("<I",len(data)));out.write(data)


def nvtx(image):
    return b"NVTX"+struct.pack("<II",image.width,image.height)+image.convert("RGBA").tobytes()


def prepare_font(mcp, assets):
    source_path=mcp/"src/minecraft/net/minecraft/client/gui/FontRenderer.java"
    if source_path.is_file():
        source=source_path.read_text(encoding="utf-8")
        found=re.search(r'"((?:\\.|[^"\\])*)"\.indexOf\(ch\)',source)
        if not found:
            raise base.AssetError("FontRenderer's original character map was not found")
        alphabet=found[1].encode("ascii").decode("unicode_escape")
    else:
        alphabet=base.asset_constants()["ascii_alphabet"]
    if len(alphabet)!=256:
        raise base.AssetError("Original font character map must contain 256 UTF-16 code units")
    image=Image.open(assets/"resources/assets/minecraft/textures/font/ascii.png").convert("RGBA")
    if image.width%16 or image.height%16 or image.width!=image.height:
        raise base.AssetError("Legacy ascii.png must be square and divisible into 16x16 glyphs")
    pixels=np.array(image);cell_w=image.width//16;cell_h=image.height//16
    widths=[]
    for char in range(256):
        tile=pixels[(char//16)*cell_w:(char//16)*cell_w+cell_h,(char%16)*cell_w:(char%16+1)*cell_w,3]
        used=np.flatnonzero(np.any(tile!=0,axis=0))
        right=int(used[-1])+1 if len(used) else 0
        widths.append(int(.5+right*8/cell_w)+1)
    base.write_bytes(assets/"runtime/font/ascii-map.bin",struct.pack("<256H",*map(ord,alphabet)))
    base.write_bytes(assets/"runtime/font/ascii-widths.bin",bytes(widths))
    galactic_path=assets/"resources/assets/minecraft/textures/font/ascii_sga.png"
    if galactic_path.is_file():
        galactic=Image.open(galactic_path).convert("RGBA");gp=np.array(galactic);cw=galactic.width//16;ch=galactic.height//16;gw=[]
        for char in range(256):
            tile=gp[(char//16)*cw:(char//16)*cw+ch,(char%16)*cw:(char%16+1)*cw,3]
            used=np.flatnonzero(np.any(tile!=0,axis=0));right=int(used[-1])+1 if len(used) else 0
            gw.append(int(.5+right*8/cw)+1)
        base.write_bytes(assets/"runtime/gui/enchant_font.rgba",nvtx(galactic))
        base.write_bytes(assets/"runtime/font/sga-widths.bin",bytes(gw))
    report={"source":"FontRenderer.java::readFontTexture/getCharWidth/renderUnicodeChar",
            "source_sha256":base.source_hash(mcp,"client/gui/FontRenderer.java"),"ascii_glyphs":256,
            "unicode_pages":len(json.loads((assets/"runtime/font/index.json").read_text())),
            "glyph_sizes_bytes":(assets/"runtime/font/glyph_sizes.bin").stat().st_size,
            "signed_byte_render_bounds_corrected":sum(b>=128 for b in (assets/"runtime/font/glyph_sizes.bin").read_bytes())}
    base.write_json(assets/"runtime/font/metrics-report.json",report)


def padded(image, padding):
    if padding==0:return image.copy()
    return Image.fromarray(np.pad(np.array(image),((padding,padding),(padding,padding),(0,0)),mode="edge"))


def first_frame(resources, texture, frame=None):
    ns,path=base.identifier(texture)
    name=f"assets/{ns}/textures/{path}.png"
    image=Image.open(io.BytesIO(resources[name])).convert("RGBA")
    metadata=json.loads(resources[name+".mcmeta"]).get("animation") if name+".mcmeta" in resources else None
    if metadata is not None or (frame is not None and image.height>image.width):
        if image.height%image.width:
            raise base.AssetError("Invalid animated texture aspect ratio: "+texture)
        sequence=(metadata or {}).get("frames") or [0]
        first=sequence[0] if isinstance(sequence[0],int) else sequence[0]["index"]
        use=first if frame is None else frame%(image.height//image.width)
        return image.crop((0,use*image.width,image.width,(use+1)*image.width))
    return image


def prepare_animations(resources, assets):
    atlas=json.loads((assets/"atlas.json").read_text(encoding="utf-8"))
    out=io.BytesIO();out.write(b"NVANIM01")
    animated=atlas["animations"]
    out.write(struct.pack("<III",atlas["width"],atlas["height"],len(animated)))
    report=[]
    for animation in animated:
        texture=animation["texture"];tile=atlas["textures"][texture];meta=animation["metadata"]
        ns,path=base.identifier(texture)
        image=Image.open(io.BytesIO(resources[f"assets/{ns}/textures/{path}.png"])).convert("RGBA")
        count=image.height//image.width;pad=atlas["padding"]
        sequence=meta.get("frames") or list(range(count))
        sequence=[(frame,meta.get("frametime",1)) if isinstance(frame,int) else (frame["index"],frame.get("time",meta.get("frametime",1))) for frame in sequence]
        frames=[padded(image.crop((0,i*image.width,image.width,(i+1)*image.width)),pad).tobytes() for i in range(count)]
        string(out,texture)
        out.write(struct.pack("<6IB3x",tile["x"]-pad,tile["y"]-pad,tile["width"]+pad*2,tile["height"]+pad*2,count,len(sequence),bool(meta.get("interpolate",False))))
        for index,duration in sequence:out.write(struct.pack("<II",index,duration))
        for frame in frames:out.write(frame)
        report.append({"texture":texture,"frame_count":count,"sequence":sequence,"interpolate":bool(meta.get("interpolate",False)),"cycle_ticks":sum(t for _,t in sequence)})
    base.write_bytes(assets/"runtime/animations/terrain.bin",out.getvalue())
    base.write_json(assets/"runtime/animations/report.json",{"tick_rate":20,"alpha_interpolated":False,"animations":report})
    return len(animated)


def matrix_rotation(axis, degrees):
    c,s=math.cos(math.radians(degrees)),math.sin(math.radians(degrees))
    if axis=="x":return np.array([[1,0,0],[0,c,-s],[0,s,c]],dtype=float)
    if axis=="y":return np.array([[c,0,s],[0,1,0],[-s,0,c]],dtype=float)
    return np.array([[c,-s,0],[s,c,0],[0,0,1]],dtype=float)


def raster_item(model, resources, colors, pixels=32, gui3d=True):
    """Source GUI matrices + orthographic software rasterizer for static 3-D icons."""
    rgba=np.zeros((pixels,pixels,4),dtype=np.float64);depth=np.full((pixels,pixels),-np.inf)
    transform=model.get("display",{}).get("gui",{})
    scale=np.clip(np.array(transform.get("scale",[1,1,1]),dtype=float),-4,4)
    translate=np.clip(np.array(transform.get("translation",[0,0,0]),dtype=float)/16,-1.5,1.5)
    rx,ry,rz=transform.get("rotation",[0,0,0])
    gui=matrix_rotation("y",ry)@matrix_rotation("x",rx)@matrix_rotation("z",rz)@np.diag(scale)
    outer=matrix_rotation("x",210 if gui3d else 180)@matrix_rotation("y",-135 if gui3d else 0)
    outer=np.diag([1,1,-1])@outer
    normal_matrix=outer@np.linalg.pinv(gui).T
    texcoords={}
    for part in model.get("elements",[]):
        for face in part.get("faces",{}).values():
            texture=base.ModelResolver.texture(model,face["texture"])
            texcoords[texture]={"u0":0.,"v0":0.,"u1":1.,"v1":1.}
    light_matrix=matrix_rotation("y",-30)@matrix_rotation("x",165)
    lights=[light_matrix@(np.array(vector)/np.linalg.norm(vector)) for vector in ((.2,1,-.7),(-.2,1,.7))]
    baked=base.bake_model(model,{},texcoords,{"layer":0,"occludes":False,"full_cube":False,"approximate":False})
    # bake_model preserves element/face order; associate each output quad's texture.
    textures=[base.ModelResolver.texture(model,face["texture"]) for part in model.get("elements",[]) for face in part.get("faces",{}).values()]
    texture_pixels={texture:np.array(first_frame(resources,texture)) for texture in texcoords}
    for quad,texture in zip(baked["quads"],textures):
        vertices=np.array(quad["vertices"],dtype=float)
        positions=((vertices[:,:3]-.5)*.5)@gui.T+translate
        positions=positions@outer.T
        positions*=20 if gui3d else 32
        positions[:,:2]+=8
        positions[:,:2]*=pixels/16
        # RenderItem.putColor4 replaces the FaceBakery shade; standard GUI lights
        # illuminate the snapped quad face normal instead.
        normal=normal_matrix@np.array(base.NORMALS[quad["face"]]);normal/=max(np.linalg.norm(normal),1e-10)
        brightness=min(1,.4+.6*sum(max(0,float(normal@light)) for light in lights)) if gui3d else 1.
        tint=colors[quad["tint_index"]] if 0<=quad["tint_index"]<len(colors) else 0xffffff
        color=np.array([(tint>>16)&255,(tint>>8)&255,tint&255],dtype=float)/255*brightness
        tex=texture_pixels[texture]
        for indices in ((0,1,2),(0,2,3)):
            p=positions[list(indices)];uv=vertices[list(indices),3:]
            x0=max(0,int(np.floor(p[:,0].min())));x1=min(pixels-1,int(np.ceil(p[:,0].max())))
            y0=max(0,int(np.floor(p[:,1].min())));y1=min(pixels-1,int(np.ceil(p[:,1].max())))
            if x1<x0 or y1<y0:continue
            xx,yy=np.meshgrid(np.arange(x0,x1+1)+.5,np.arange(y0,y1+1)+.5)
            den=(p[1,1]-p[2,1])*(p[0,0]-p[2,0])+(p[2,0]-p[1,0])*(p[0,1]-p[2,1])
            if abs(den)<1e-10:continue
            a=((p[1,1]-p[2,1])*(xx-p[2,0])+(p[2,0]-p[1,0])*(yy-p[2,1]))/den
            b=((p[2,1]-p[0,1])*(xx-p[2,0])+(p[0,0]-p[2,0])*(yy-p[2,1]))/den;c=1-a-b
            z=a*p[0,2]+b*p[1,2]+c*p[2,2]
            u=a*uv[0,0]+b*uv[1,0]+c*uv[2,0];v=a*uv[0,1]+b*uv[1,1]+c*uv[2,1]
            tx=np.clip((u*tex.shape[1]).astype(int),0,tex.shape[1]-1);ty=np.clip((v*tex.shape[0]).astype(int),0,tex.shape[0]-1)
            sample=tex[ty,tx].astype(float);sample[:,:,:3]*=color
            region=rgba[y0:y1+1,x0:x1+1];zregion=depth[y0:y1+1,x0:x1+1]
            mask=(a>=-1e-6)&(b>=-1e-6)&(c>=-1e-6)&(z>=zregion-1e-7)&(sample[:,:,3]>25.5)
            alpha=sample[:,:,3:4]/255
            destination_alpha=region[:,:,3:4]/255
            merged_alpha=alpha+destination_alpha*(1-alpha)
            merged_rgb=(sample[:,:,:3]*alpha+region[:,:,:3]*destination_alpha*(1-alpha))/np.maximum(merged_alpha,1e-10)
            combined=np.concatenate([merged_rgb,merged_alpha*255],axis=2)
            region[mask]=combined[mask];zregion[mask]=z[mask]
    return Image.fromarray(np.clip(rgba,0,255).astype(np.uint8))


def entity_box(texture, start, size, offset=(0,0), texture_size=(64,32), inflate=0):
    """Single ModelBox unfolded to JSON faces for builtin skull/banner previews."""
    x,y,z=size;u,v=offset;tw,th=texture_size
    rectangles={"east":(u+z+x,v+z,u+z+x+z,v+z+y),"west":(u,v+z,u+z,v+z+y),
                "up":(u+z,v,u+z+x,v+z),"down":(u+z+x,v+z,u+z+x+x,v),
                "north":(u+z,v+z,u+z+x,v+z+y),"south":(u+z+x+z,v+z,u+z+x+z+x,v+z+y)}
    return {"from":[a-inflate for a in start],"to":[start[i]+size[i]+inflate for i in range(3)],
            "faces":{face:{"texture":texture,"uv":[value*16/(tw if i%2==0 else th) for i,value in enumerate(rect)]} for face,rect in rectangles.items()}}


def builtin_item(model_name, model):
    name=model_name.split(":",1)[1]
    if name in ("chest","trapped_chest","ender_chest"):
        result,_,_=base.builtin_model({"chest":54,"trapped_chest":146,"ender_chest":130}[name],2)
        return result
    if name.startswith("skull_"):
        texture={"skull_skeleton":"entity/skeleton/skeleton","skull_wither":"entity/skeleton/wither_skeleton",
                 "skull_zombie":"entity/zombie/zombie","skull_char":"entity/steve","skull_creeper":"entity/creeper/creeper"}[name]
        height=64 if name in ("skull_char","skull_zombie") else 32
        return {"elements":[entity_box(texture,(4,4,4),(8,8,8),texture_size=(64,height))],
                "display":{"gui":{"scale":[1.4,1.4,1.4]}}}
    if name=="banner":
        # Base banner only; dynamic pattern composition remains with the special renderer.
        cloth=entity_box("entity/banner_base",(0,0,0),(20,40,1),texture_size=(64,64))
        cloth["from"],cloth["to"]=[4,3,7],[12,17,8]
        for face in cloth["faces"].values():face["tintindex"]=1
        pole=entity_box("entity/banner_base",(0,0,0),(2,42,2),(44,0),(64,64))
        pole["from"],pole["to"]=[7.5,0,7.5],[8.5,17,8.5]
        return {"elements":[pole,cloth],"display":{"gui":{"scale":[.85,.85,.85],"rotation":[0,-65,0]}}}
    raise base.AssetError("Unsupported builtin inventory model: "+model_name)


def pack_images(images, assets):
    padding=2;area=sum((im.width+4)*(im.height+4) for im in images.values())
    width=1
    while width<math.sqrt(area)*1.1:width*=2
    width=max(width,max(im.width+4 for im in images.values()))
    x=y=row=0;positions={}
    for name,im in sorted(images.items(),key=lambda e:(-e[1].height,-e[1].width,e[0])):
        if x+im.width+4>width:x=0;y+=row;row=0
        positions[name]=(x+2,y+2);x+=im.width+4;row=max(row,im.height+4)
    height=1
    while height<y+row:height*=2
    if width>16384 or height>16384:raise base.AssetError("Item atlas exceeds 16384-pixel limit")
    atlas=Image.new("RGBA",(width,height));regions={}
    for name,im in images.items():
        x,y=positions[name];atlas.paste(padded(im,2),(x-2,y-2))
        regions[name]=[(x+.01)/width,(y+.01)/height,(x+im.width-.01)/width,(y+im.height-.01)/height]
    base.write_bytes(assets/"runtime/items/atlas.rgba",atlas.tobytes())
    base.write_bytes(assets/"runtime/gui/items.rgba",nvtx(atlas))
    preview=io.BytesIO();atlas.save(preview,format="PNG");base.write_bytes(assets/"runtime/items/atlas.png",preview.getvalue())
    return atlas,regions


def prepare_items(resources, assets, reference):
    data=json.loads(reference.read_text(encoding="utf-8"));resolver=ItemModelResolver(resources)
    names=[]
    for item in data["items"]:
        if item.get("translationKey"):
            names.append((item["id"],-1,item["translationKey"],item.get("displayName","")))
    for item in data["mappings"]:
        if item.get("translationKey"):
            names.append((item["id"],item["meta"],item["translationKey"],item.get("displayName","")))
    def safe_name(value):return str(value).replace("\t"," ").replace("\r"," ").replace("\n"," ")
    base.write_bytes(assets/"runtime/items/names.txt",("".join("\t".join(map(safe_name,row))+"\n" for row in names)).encode("utf-8"))
    mappings=list(data["mappings"]);defaults=[]
    for dynamic in data["dynamic"]:
        defaults.append(dynamic["id"])
        if dynamic["kind"]=="potion":
            for meta,color in enumerate(dynamic["palette"]):
                mappings.append({"id":dynamic["id"],"meta":meta,"model":"minecraft:"+(dynamic["splash"] if meta&16384 else dynamic["normal"]),
                                 "colors":[color,0xffffff,0xffffff,0xffffff,0xffffff]})
    images={};icons=[];lookup={};entries=[];frame_entries=[];issues=[]
    missing=Image.new("RGBA",(16,16),(0,0,0,255))
    for y in range(16):
        for x in range(16):
            if (x<8)==(y<8):missing.putpixel((x,y),(248,0,248,255))
    images["missing"]=missing
    icons.append({"model":"native:missing","layers":[{"image":"missing","tint":-1}],"valid":False,"approximate":False})
    def add_icon(name,colors,frame=None):
        ns,path=base.identifier(name);model=resolver.resolve(ns+":item/"+path)
        builtin=model.get("builtin","")
        flat=builtin in ("builtin/generated","builtin/clock","builtin/compass")
        key=(name,() if flat else tuple(colors),frame)
        if key in lookup:return lookup[key]
        layers=[];approximate=False
        if flat:
            for layer in range(5):
                reference="#layer"+str(layer)
                if reference[1:] not in model.get("textures",{}):break
                texture=resolver.texture(model,reference);image_key=texture+("@"+str(frame) if frame is not None else "")
                if image_key not in images:images[image_key]=first_frame(resources,texture,frame)
                layers.append({"image":image_key,"tint":layer})
            approximate=builtin in ("builtin/clock","builtin/compass") and frame is None
        else:
            geometry=model
            if builtin=="builtin/entity":geometry=builtin_item(name,model);approximate=True
            image_key="model:"+name+":"+','.join(map(str,colors))
            images[image_key]=raster_item(geometry,resources,colors)
            layers.append({"image":image_key,"tint":-1})
        index=len(icons);lookup[key]=index
        icons.append({"model":name,"layers":layers,"valid":bool(layers),"approximate":approximate})
        return index
    for mapping in mappings:
        try:index=add_icon(mapping["model"],mapping["colors"])
        except (base.AssetError,KeyError,OSError) as error:
            issues.append({"id":mapping["id"],"meta":mapping["meta"],"model":mapping["model"],"error":str(error)})
            index=0
        entries.append({**mapping,"icon":index})
    # Original clock/compass strip frames and bow/cast models are selectable by
    # the renderer through ItemAppearance.frame without abusing damage metadata.
    item_names={item["name"]:item["id"] for item in data["items"]}
    for name in ("clock","compass"):
        texture="minecraft:items/"+name
        im=Image.open(io.BytesIO(resources["assets/minecraft/textures/items/"+name+".png"]))
        for frame in range(im.height//im.width):
            index=add_icon("minecraft:"+name,[0xffffff]*5,frame)
            frame_entries.append((item_names["minecraft:"+name]<<16|frame,index))
    for entry in entries:
        if entry["id"] in (item_names["minecraft:bow"],item_names["minecraft:fishing_rod"]):
            frame_entries.append((entry["id"]<<16|entry["meta"],entry["icon"]))
    atlas,regions=pack_images(images,assets)
    out=io.BytesIO();out.write(b"NVITEM01")
    out.write(struct.pack("<6I",atlas.width,atlas.height,len(icons),len(entries),len(defaults),len(frame_entries)))
    for icon in icons:
        string(out,icon["model"]);out.write(struct.pack("<BBH",int(icon["valid"])|int(icon["approximate"])<<1,len(icon["layers"]),0))
        for layer in icon["layers"]:
            out.write(struct.pack("<4fIi",*regions[layer["image"]],0xffffffff,layer["tint"]))
    for entry in entries:out.write(struct.pack("<HHI5I",entry["id"],entry["meta"],entry["icon"],*[c|0xff000000 for c in entry["colors"]]))
    for item_id in defaults:
        default=next(e for e in entries if e["id"]==item_id and e["meta"]==0)
        out.write(struct.pack("<II",item_id,default["icon"]))
    for key,index in frame_entries:out.write(struct.pack("<II",key,index))
    base.write_bytes(assets/"runtime/items/icons.bin",out.getvalue())
    summary={"source":"tools/reference/item_render_registry.json (original RenderItem and Item getters)",
             "registry_items":len(data["items"]),"mapped_item_ids":len({e["id"] for e in entries if e["icon"]}),
             "metadata_mappings":len(entries),"icon_models":len(icons),"atlas":[atlas.width,atlas.height],
             "sprite_images":len(images),"selectable_clock_compass_bow_rod_frames":len(frame_entries),
             "unmapped_registry_ids":[item for item in data["items"] if item["id"] not in {e["id"] for e in entries}],
             "errors":issues,"builtin_icons_approximate":sorted({i["model"] for i in icons if i["approximate"]})}
    base.write_json(assets/"runtime/items/report.json",summary)
    base.write_json(assets/"runtime/items/regions.json",regions)
    # Small reviewable list without repeating the 65,536-entry potion palette.
    base.write_json(assets/"runtime/items/mappings.json",[e for e in entries if e["id"]!=373 or e["meta"] in (0,16,8193,8194,8197,16384,16385)])
    return summary


def prepare(mcp, assets, reference):
    resources=ResourceFiles(assets/"resources")
    prepare_font(mcp,assets)
    item_report=prepare_items(resources,assets,reference)
    animations=prepare_animations(resources,assets)
    creative=Path(__file__).resolve().parent/"reference/creative_catalog.bin"
    if creative.is_file():base.write_bytes(assets/"runtime/gui/creative_catalog.bin",creative.read_bytes())
    model_report=assets/"model-report.json"
    if model_report.is_file():
        report=json.loads(model_report.read_text(encoding="utf-8"))
        report["animation_playback"]="Native 20 Hz AnimationAtlasCache available; renderer must upload its changed rectangles"
        base.write_json(model_report,report)
    print(json.dumps({"items":item_report,"terrain_animations":animations},ensure_ascii=False,indent=2))
    if item_report["errors"]:raise base.AssetError("Unresolved inventory models; see runtime/items/report.json")


def main():
    project=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcp",type=Path,default=project.parent/"MCP-919-main")
    parser.add_argument("--assets",type=Path,default=project/"assets")
    parser.add_argument("--output",type=Path,help="Alias for --assets")
    parser.add_argument("--reference",type=Path,default=project/"tools/reference/item_render_registry.json")
    args=parser.parse_args()
    try:prepare(args.mcp.resolve(),(args.output or args.assets).resolve(),args.reference.resolve())
    except (base.AssetError,OSError,ValueError) as error:print("Render asset preparation failed: "+str(error),file=sys.stderr);return 1
    return 0


if __name__=="__main__":raise SystemExit(main())
