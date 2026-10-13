#!/usr/bin/env python3
"""Compare native joint poses with original Java model animation methods."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import sys
from export_registry import javac
from verify_physics import make_reference, run


def main() -> None:
    root=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcp",type=Path,default=root.parent/"MCP-919-main")
    parser.add_argument("--cxx",default="D:/Strawberry/c/bin/g++.exe" if os.name=="nt" else "g++")
    args=parser.parse_args();mcp=args.mcp.resolve();build=root/"build/entity_model_reference";classes=build/"pose_classes";classes.mkdir(parents=True,exist_ok=True)
    fixture=build/"poses.txt";lines=[]
    common=" headYaw=0.3 headPitch=-0.14 limbSwing=2.75 limbSwingAmount=0.65 ageTicks=40.375 partialTick=0.375 entityId=17"
    families=["wolf","ocelot","horse","rabbit","witch","guardian","wither","bat","armorstand"]
    for family in families:
        for basic in ["",common,common+" child=1",common+" limbSwing=11.2 limbSwingAmount=1"]:lines.append(family+basic)
    lines += ["wolf"+common+" flags=5 health=7 interestedAngle=0.2 headShake=0.1 maneShake=0.08 bodyShake=0.05 tailShake=-0.1",
              "wolf"+common+" flags=2","ocelot"+common+" flags=1","ocelot"+common+" flags=8","ocelot"+common+" sneaking=1"]
    for flag in [0,32,32|64,32|64|128,32|256]:
        for rearing,grazing in [(0,0),(.8,0),(0,.9),(.4,.2)]:
            lines.append("horse"+common+f" flags={flag} rearing={rearing} grazing={grazing} mouthOpen=0.4 horseType=1")
    for jump in [0,.1,.25,.5,.8,1]:lines.append("rabbit"+common+f" jumpProgress={jump}")
    lines += ["witch"+common+" flags=16384","guardian"+common+" spikeExtension=0 tailPhase=3.4","guardian"+common+" spikeExtension=0.6 tailPhase=7.4",
              "wither"+common+" sideHeadYaw0=0.4 sideHeadYaw1=-0.8 sideHeadPitch0=0.3 sideHeadPitch1=-0.2","bat"+common+" flags=16",
              "armorstand"+common+" flags=2048 standHead0=30 standHead1=25 standBody2=15 standLeftArm0=-110 standRightArm2=60 standLeftLeg2=-9 bodyYaw=0.4"]
    fixture.write_text("\n".join(lines)+"\n",encoding="ascii")
    make_reference(root,mcp,build)
    cp=os.pathsep.join(str(p) for p in [classes,build/"classes",mcp/"bin/minecraft",mcp/"jars/versions/1.8.9/1.8.9.jar",*sorted((mcp/"jars/libraries").rglob("*.jar"))])
    source=mcp/"src/minecraft/net/minecraft/client/model"
    names=["ModelBase","ModelBox","ModelRenderer","PositionTextureVertex","TexturedQuad","ModelBiped","ModelVillager","ModelArmorStandArmor",
           "ModelWolf","ModelOcelot","ModelHorse","ModelRabbit","ModelWitch","ModelGuardian","ModelWither","ModelDragon","ModelArmorStand","ModelBat","ModelBoat","ModelMinecart"]
    javac(["-encoding","UTF-8","--release","8","-cp",cp,"-d",str(classes),*[str(source/(name+".java")) for name in names],
           str(root/"tests/java/EntityModelExport.java"),str(root/"tests/java/EntityModelSnapshot.java"),str(build/"PhysicsReference.java"),
           str(root/"tests/java/model_stub/net/minecraft/client/Minecraft.java")],build)
    executable=build/("entity_models_tests.exe" if os.name=="nt" else "entity_models_tests")
    run([args.cxx,"-std=c++17","-O2","-ffp-contract=off","-I",str(root/"include"),str(root/"tests/entity_models_tests.cpp"),"-o",str(executable)],root)
    run(["java","-Djava.awt.headless=true","-cp",cp,"EntityModelSnapshot",str(fixture),str(build/"java_poses.txt")],build)
    native=run([str(executable),str(fixture)],root).stdout;(build/"native_poses.txt").write_text(native,encoding="ascii")
    def parse(text):
        result={}
        for line in text.splitlines():
            v=line.split();result[(int(v[0]),v[1])]=list(map(float,v[2:]))
        return result
    java=parse((build/"java_poses.txt").read_text(encoding="ascii"));cpp=parse(native)
    failures=[];maximum=0
    for key,a in java.items():
        # Hidden armor-stand headwear is controlled by ArmorStandRenderer, not
        # drawn by the native stand; its copied pose has no visible geometry.
        if key[1]=="bipedHeadwear":continue
        b=cpp.get(key)
        if b is None:failures.append({"key":key,"error":"missing native joint"});continue
        delta=max(abs(x-y) for x,y in zip(a,b));maximum=max(maximum,delta)
        if delta>2e-6:failures.append({"key":key,"fixture":lines[key[0]],"java":a,"native":b,"difference":delta})
    report={"cases":len(lines),"joint_snapshots":len(java),"maximum_absolute_difference":maximum,"tolerance":2e-6,"failures":failures}
    (build/"pose_report.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    if failures:
        for f in failures[:10]:print(json.dumps(f),file=sys.stderr)
        raise RuntimeError(f"{len(failures)} joint snapshots differ")
    print("PASS: "+json.dumps({k:v for k,v in report.items() if k!="failures"}))


if __name__=="__main__":
    try:main()
    except Exception as error:
        print(f"Entity pose verification failed: {error}",file=sys.stderr);sys.exit(1)
