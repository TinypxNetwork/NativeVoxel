#!/usr/bin/env python3
"""Extract exact missing vanilla ModelBox geometry and UVs into native source."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import sys
from export_registry import javac, run, cpp_float, cpp_string
from verify_physics import java_block


def main() -> None:
    root=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcp",type=Path,default=root.parent/"MCP-919-main")
    args=parser.parse_args();mcp=args.mcp.resolve()
    build=root/"build/entity_model_reference";classes=build/"classes";classes.mkdir(parents=True,exist_ok=True)
    cp=os.pathsep.join(str(p) for p in [classes,mcp/"bin/minecraft",mcp/"jars/versions/1.8.9/1.8.9.jar",*sorted((mcp/"jars/libraries").rglob("*.jar"))])
    source=mcp/"src/minecraft/net/minecraft/client/model"
    names=["ModelBase","ModelBox","ModelRenderer","PositionTextureVertex","TexturedQuad","ModelBiped","ModelVillager","ModelArmorStandArmor",
           "ModelWolf","ModelOcelot","ModelHorse","ModelRabbit","ModelWitch","ModelGuardian","ModelWither","ModelDragon","ModelArmorStand","ModelBat","ModelBoat","ModelMinecart"]
    javac(["-encoding","UTF-8","--release","8","-cp",cp,"-d",str(classes),*[str(source/(name+".java")) for name in names],str(root/"tests/java/EntityModelExport.java")],build)
    raw=build/"model_parts.json"
    run(["java","-Djava.awt.headless=true","-cp",cp,"EntityModelExport",str(raw)],build)
    data=json.loads(raw.read_text(encoding="utf-8"));models=data["models"]
    lines=["// Generated from original Java Model constructors. Run tools/export_entity_models.py.","constexpr float sourceDyeColors[16][3]={"]
    lines += ["    {"+",".join(cpp_float(x) for x in dye)+"}," for dye in data["dyes"]]
    lines += ["};"]
    count=0
    for model in models:
        lines += [f"Geometry source{model['name']}() {{","    Geometry g;",f"    g.textureWidth={model['textureWidth']};g.textureHeight={model['textureHeight']};"]
        vec=lambda v:"{"+",".join(cpp_float(x) for x in v)+"}"
        for i,p in enumerate(model["parts"]):
            lines += [f"    g.part({vec(p['pivot'])},{vec(p['angle'])},{p['parent']});",
                      f"    g.parts[{i}].name={cpp_string(p['name'])};g.parts[{i}].offset={vec(p['offset'])};g.parts[{i}].visible={str(p['visible']).lower()};"]
            for box in p["boxes"]:
                lines += [f"    g.exactBox({i},{cpp_float(box['u'])},{cpp_float(box['v'])},{vec(box['pos'])},{vec(box['size'])},{vec(box['lo'])},{vec(box['hi'])},{str(box['mirror']).lower()});"]
                count+=1
        lines += ["    return g;","}"]
    output=root/"src/entity_model_data.inc";output.write_text("\n".join(lines)+"\n",encoding="ascii")
    # Preserve the long original horse joint equations verbatim apart from
    # mechanical field/getter substitutions into the native pose representation.
    horse=java_block((source/"ModelHorse.java").read_text(encoding="utf-8"),"public void setLivingAnimations(")
    horse=horse[horse.index("float f3 = f1 - f;"):horse.rfind("}")]
    horse=horse.replace("float f3 = f1 - f;","float f3 = p.headYaw * (180.0f / pi);")
    horse=re.sub(r"float f4 = .*?;","float f4 = p.headPitch;",horse,count=1)
    horse=re.sub(r"EntityHorse entityhorse = .*?;","",horse)
    replacements={"entityhorse.getGrassEatingAmount(partialTickTime)":"p.grazing","entityhorse.getRearingAmount(partialTickTime)":"p.rearing",
        "entityhorse.getMouthOpennessAngle(partialTickTime)":"p.mouthOpen","entityhorse.field_110278_bp != 0":"(p.flags & PoseTailWag) != 0",
        "entityhorse.isHorseSaddled()":"(p.flags & PoseSaddled) != 0","entityhorse.riddenByEntity != null":"(p.flags & PoseHasRider) != 0",
        "(float)entitylivingbaseIn.ticksExisted + partialTickTime":"p.ageTicks","p_78086_2_":"p.limbSwing","p_78086_3_":"p.limbSwingAmount",
        "MathHelper.cos":"animCos","MathHelper.sin":"animSin","Math.max":"std::max","(float)Math.PI":"pi","boolean":"bool"}
    for key,value in replacements.items():horse=horse.replace(key,value)
    for old,new in [("rotationPointX","pivot.x"),("rotationPointY","pivot.y"),("rotationPointZ","pivot.z"),
                    ("rotateAngleX","angle.x"),("rotateAngleY","angle.y"),("rotateAngleZ","angle.z")]:
        horse=re.sub(r"this\.(\w+)\."+old,lambda m:'g.named("'+m[1]+'").'+new,horse)
    horse=re.sub(r"float f1[34] = .*?;","",horse)
    horse=re.sub(r"(?<![\w.])(\d+)F\b",r"\1.0f",horse)
    horse=re.sub(r"(?<=\d)F\b","f",horse)
    (root/"src/entity_horse_animation.inc").write_text("// Generated from the original ModelHorse.setLivingAnimations equations.\nvoid animateHorse(Geometry& g,const EntityPose& p) {\n"+horse+"\n}\n",encoding="ascii")
    report={"models":len(models),"boxes":count,"source_sha256":{name:hashlib.sha256((source/(name+".java")).read_bytes()).hexdigest() for name in names},"header_sha256":hashlib.sha256(output.read_bytes()).hexdigest()}
    (build/"geometry_report.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(f"Exported {len(models)} original Java model families / {count} cuboids to {output}")


if __name__=="__main__":
    try:main()
    except Exception as error:
        print(f"Entity model export failed: {error}",file=sys.stderr);sys.exit(1)
