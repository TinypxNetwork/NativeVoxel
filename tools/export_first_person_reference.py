#!/usr/bin/env python3
"""Export matrices by invoking original ItemRenderer with a headless GL recorder."""
from pathlib import Path
import argparse,hashlib,json,os,random,subprocess


def main():
    root=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--mcp",type=Path,default=root.parent/"MCP-919-main");args=parser.parse_args();mcp=args.mcp.resolve()
    build=root/"build/first_person_reference";classes=build/"classes";classes.mkdir(parents=True,exist_ok=True)
    cp=os.pathsep.join(map(str,[classes,mcp/"bin/minecraft",mcp/"jars/versions/1.8.9/1.8.9.jar",*sorted((mcp/"jars/libraries").rglob("*.jar"))]))
    source=mcp/"src/minecraft/net/minecraft";names=["client/renderer/ItemRenderer.java","util/MathHelper.java",*["client/model/"+name+".java" for name in ("ModelPlayer","ModelBiped","ModelBase","ModelRenderer","ModelBox","PositionTextureVertex","TexturedQuad")]]
    # Compile the original source against its real dependencies first. Stubs only
    # replace graphics calls in the development test's own classpath afterwards.
    subprocess.run(["javac","-encoding","UTF-8","--release","8","-cp",cp,"-d",str(classes),*[str(source/name) for name in names]],cwd=build,check=True)
    stubs=sorted((root/"tests/java/first_person_stub").rglob("*.java"))
    subprocess.run(["javac","-encoding","UTF-8","--release","8","-cp",cp,"-d",str(classes),*map(str,stubs),str(root/"tests/java/FirstPersonReference.java")],cwd=build,check=True)
    lines=[]
    for swing in (0,.001,.125,.333,.5,.75,.999,1):
        for equip in (0,.2,1):
            for mode in (0,1,2):lines.append(f"{mode} {swing} {equip} 0 0 0 0 0.375 -1 0 0")
    for action in range(5):
        duration=72000 if action>=3 else 32
        for elapsed in (0,1,3,6,13,14,17,18,19,20,31):
            for partial in (0,.375,.999):lines.append(f"0 0.7 0.35 23 -72 21 -69 {partial} {action} {duration-elapsed} {duration}")
    rng=random.Random(189)
    for i in range(128):
        mode=i%3;values=[rng.random(),rng.random(),rng.uniform(-90,90),rng.uniform(-1080,1080),rng.uniform(-90,90),rng.uniform(-1080,1080),rng.random()]
        action=rng.randrange(-1,5);duration=72000 if action>=3 else 32;remaining=max(0,duration-rng.randrange(0,32))
        lines.append(" ".join(map(str,[mode,*values,action,remaining,duration])))
    poses=build/"poses.txt";poses.write_text("\n".join(lines)+"\n",encoding="ascii")
    target=root/"tools/reference/first_person_reference.bin";arms=root/"tools/reference/first_person_arms.bin"
    subprocess.run(["java","-Djava.awt.headless=true","-cp",cp,"FirstPersonReference",str(poses),str(target),str(arms)],cwd=build,check=True)
    report={"input_cases":len(lines),"source_sha256":{name:hashlib.sha256((source/name).read_bytes()).hexdigest() for name in names},"reference_sha256":hashlib.sha256(target.read_bytes()).hexdigest(),"arm_reference_sha256":hashlib.sha256(arms.read_bytes()).hexdigest(),"reference_method":"Unmodified ItemRenderer via reflection; only GL/texture/render calls replaced by a matrix recorder. Original ModelPlayer/ModelBox geometry and rotations."}
    (target.parent/"first_person_reference.json").write_text(json.dumps(report,indent=2)+"\n")


if __name__=="__main__":main()
