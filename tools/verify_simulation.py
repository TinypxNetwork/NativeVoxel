#!/usr/bin/env python3
"""Compare native singleplayer subsystems with actual MCP server-side methods."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

def run(arguments: list[str],cwd: Path)->str:
    env=os.environ.copy();env["PATH"]=r"D:\Strawberry\c\bin"+os.pathsep+env.get("PATH","")
    result=subprocess.run(arguments,cwd=cwd,env=env,capture_output=True,text=True)
    if result.returncode:raise RuntimeError(f"Command failed ({result.returncode}): {arguments[0]}\n"+result.stdout[-2000:]+result.stderr[-4000:])
    return result.stdout

def main()->None:
    root=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--mcp",type=Path,default=root.parent/"MCP-919-main");parser.add_argument("--native",type=Path,default=root/"tests/simulation_tests.exe");args=parser.parse_args();mcp=args.mcp.resolve()
    build=root/"build/simulation_reference";classes=build/"classes";classes.mkdir(parents=True,exist_ok=True)
    classpath=os.pathsep.join(map(str,[classes,root/"build/physics_reference/oracle_classes",root/"build/crafting_export/classes",root/"build/registry_export/classes",mcp/"bin/minecraft",mcp/"jars/versions/1.8.9/1.8.9.jar",*sorted((mcp/"jars/libraries").rglob("*.jar"))]))
    source=mcp/"src/minecraft/net/minecraft";originals=[source/name for name in ("util/FoodStats.java","world/World.java","tileentity/TileEntityFurnace.java","tileentity/TileEntityBrewingStand.java","potion/PotionHelper.java","potion/PotionEffect.java","item/ItemPotion.java","entity/player/EntityPlayer.java","inventory/Container.java","inventory/ContainerPlayer.java","inventory/ContainerWorkbench.java","inventory/ContainerFurnace.java","inventory/ContainerBrewingStand.java")]
    run(["javac","-encoding","UTF-8","--release","8","-cp",classpath,"-d",str(classes),*map(str,originals),str(root/"tests/java/SimulationReference.java")],build)
    lines=[];labels=[];counts={"food_ticks":0,"weather_ticks":0,"furnace_ticks":0,"brewing_ticks":0,"potion_cases":0,"container_close_cases":0}
    def case(label: str,setup: str,ticks: int,kind: str,extra: list[str]|None=None):
        lines.extend([setup,*(extra or []),f"T {ticks}"]);labels.extend([label]*ticks);counts[kind]+=ticks
    for difficulty in range(4):
        for health,food,saturation,exhaustion,timer in ((20,20,5,4,0),(19.5,20,5,0,0),(20,0,0,0,0),(2,0,0,0,0),(15,17,0,9,79),(1,18,.5,4.0001,79)):
            case(f"food difficulty {difficulty} health {health} food {food}",f"F {difficulty} 1 0 {health} 20 {food} {saturation} {exhaustion} {timer}",320,"food_ticks")
    case("creative starvation", "F 3 1 1 20 20 0 0 0 0",320,"food_ticks")
    case("natural regeneration disabled", "F 2 0 0 12 20 20 5 0 50",160,"food_ticks")
    case("exhaustion cap", "F 2 1 0 20 20 20 5 0 0",80,"food_ticks",["E 100"])
    for seed in (0,1,189,123456789):
        case(f"initial weather seed {seed}",f"W {seed} 0 1 0 0 0 0 0 0 0 0 0",300,"weather_ticks")
        case(f"weather transitions seed {seed}",f"W {seed} 0 1 500 23000 0 2 3 0 0 .5 .5",160,"weather_ticks")
    case("clear weather", "W 189 0 1 0 0 80 50 50 1 1 1 1",200,"weather_ticks")
    case("daylight cycle disabled", "W 189 0 0 10 6000 0 100 100 0 0 0 0",160,"weather_ticks")
    case("End has no weather", "W 189 1 1 0 0 0 1 1 1 1 .5 .5",100,"weather_ticks")
    case("Nether has no weather", "W 189 -1 1 0 0 0 1 1 1 1 .5 .5",100,"weather_ticks")
    cases=[("coal", "15 3 0 263 1 0 -1 0 0 0 0 0 200",700),
           ("wood","15 2 0 5 1 0 -1 0 0 0 0 0 200",500),
           ("lava wet sponge","19 1 1 327 1 0 -1 0 0 0 0 0 200",250),
           ("output full","15 1 0 263 1 0 265 64 0 0 0 0 200",50),
           ("mismatched output","15 1 0 263 1 0 266 1 0 100 1600 99 200",110),
           ("cooling","15 1 0 -1 0 0 -1 0 0 0 0 100 200",60),
           ("invalid fuel","15 1 0 1 1 0 -1 0 0 0 0 100 200",5),
           ("last burning tick","15 1 0 -1 0 0 -1 0 0 1 1600 199 200",3),
           ("refuel final cook","15 1 0 263 1 0 -1 0 0 1 1600 199 200",3),
           ("charcoal metadata","17 1 2 280 2 0 -1 0 0 0 0 0 200",250)]
    for label,setup,ticks in cases:case(label,"C "+setup,ticks,"furnace_ticks")
    brewing=[("water to awkward","373 1 0 373 1 0 -1 0 0 372 2 0 0 -1",805),
             ("speed","373 1 16 -1 0 0 -1 0 0 353 1 0 0 -1",402),
             ("mixed splash","373 1 8194 373 1 0 373 1 16 289 1 0 0 -1",402),
             ("loaded brew restart","373 1 16 -1 0 0 -1 0 0 353 1 0 200 -1",405),
             ("loaded last brew tick","373 1 16 -1 0 0 -1 0 0 353 1 0 1 -1",2),
             ("empty bottles","374 1 0 -1 0 0 -1 0 0 372 1 0 0 -1",3)]
    for label,setup,ticks in brewing:case(label,"B "+setup,ticks,"brewing_ticks")
    case("ingredient starts","B 373 1 16 -1 0 0 -1 0 0 353 1 0 0 -1",1,"brewing_ticks")
    lines.append("S 3 370 1 0");lines.append("T 2");labels.extend(["ingredient changes"]*2);counts["brewing_ticks"]+=2
    registry=json.loads((root/"build/simulation_export/brewing.json").read_text(encoding="utf-8"))
    for rule in registry["ingredients"]:
        variant=next(bit for bit in range(16) if rule["mask"]&(1<<bit))
        for meta in range(32768):lines.append(f"R {meta} {rule['item']} {variant}");labels.append(f"potion {meta} ingredient {rule['item']}:{variant}")
        counts["potion_cases"]+=32768
    for kind in range(4):
        for seed in (0,189,987):
            for cursor in ((-1,0,0),(4,3,0),(278,1,1555)):
                for populated in (0,1):
                    items=(5,17,35,263,264,265,266,280,339)
                    stacks=[(items[i],1+(i%3),i%2) if populated else (-1,0,0) for i in range(9)]
                    lines.append("O "+" ".join(map(str,(kind,seed,*cursor,*(value for stack in stacks for value in stack)))))
                    labels.append(f"close container {kind} seed {seed} cursor {cursor} filled {populated}");counts["container_close_cases"]+=1
    fixture=build/"simulation.fixture";fixture.write_text("\n".join(lines)+"\n",encoding="ascii")
    run(["java","-Djava.awt.headless=true","-cp",classpath,"SimulationReference",str(fixture),str(build/"java.trace")],build)
    actual=run([str(args.native.resolve()),"--trace",str(fixture)],root);(build/"native.trace").write_text(actual,encoding="ascii")
    expected=(build/"java.trace").read_text(encoding="ascii").splitlines();native=actual.splitlines()
    if len(expected)!=len(native):raise RuntimeError(f"Trace length mismatch Java={len(expected)} native={len(native)}")
    failures=[{"row":i,"case":labels[i],"java":a,"native":b} for i,(a,b) in enumerate(zip(expected,native)) if a!=b]
    report={"oracle":"Original FoodStats, EntityPlayer healing/damage, World.updateWeather, TileEntityFurnace/TileEntityBrewingStand and PotionHelper; WorldServer time increments", "rows":len(expected),"counts":counts,"mismatches":len(failures),"failures":failures[:30],"source_sha256":{str(path.relative_to(mcp)):hashlib.sha256(path.read_bytes()).hexdigest() for path in originals}}
    (build/"report.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    if failures:
        for failure in failures[:8]:print(json.dumps(failure))
        raise RuntimeError(f"{len(failures)} simulation differences; see {build/'report.json'}")
    print(f"PASS {len(expected)} exact simulation rows; {counts}")

if __name__=="__main__":main()
