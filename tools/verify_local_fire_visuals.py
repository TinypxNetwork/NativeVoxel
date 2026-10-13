"""Replay native visual fire lifetimes against original Entity/EntityPlayer code."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def method(source, signature):
    begin = source.index(signature)
    cursor = source.index("{", begin) + 1
    depth = 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[begin:cursor]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, default=ROOT / "bin/local-fire-visuals-tests.exe")
    parser.add_argument("--mcp", type=Path, default=ROOT.parent / "MCP-919-main")
    args = parser.parse_args()
    output = ROOT / "build/local_fire_reference"
    output.mkdir(parents=True, exist_ok=True)
    base = args.mcp / "src/minecraft/net/minecraft"
    paths = [base / "entity/Entity.java", base / "entity/player/EntityPlayer.java",
             base / "enchantment/EnchantmentProtection.java"]
    entity, player, protection = (p.read_text(encoding="utf-8") for p in paths)
    update = method(entity, "public void onEntityUpdate(")
    update = update[update.index("if (this.worldObj.isRemote)"):update.index("if (this.posY < -64.0D)")]
    movement = method(entity, "public void moveEntity(")
    movement = movement[movement.index("boolean flag2 = this.isWet();"):]
    movement = movement[:movement.index("this.worldObj.theProfiler.endSection();")]
    extinguish = method(player, "public void onUpdate(")
    extinguish = extinguish[extinguish.index("if (this.isBurning() && this.capabilities.disableDamage)"):]
    extinguish = extinguish[:extinguish.index("this.prevChasingPosX")]
    java = r'''import java.io.*;import java.util.*;
class DamageSource {static DamageSource onFire=new DamageSource(),lava=new DamageSource();}
class Box {Box contract(double x,double y,double z){return this;}}
class World {boolean isRemote,flammable;boolean isFlammableWithin(Box box){return flammable;}}
class Capabilities {boolean disableDamage;}
class Enchantment {static Enchantment fireProtection=new Enchantment();int effectId;}
class EnchantmentHelper {static int getMaxEnchantmentLevel(int id,int level){return level;}}
class MathHelper {static int floor_float(float value){int i=(int)value;return value<i?i-1:i;}}
class EnchantmentProtection {
''' + method(protection, "public static int getFireTimeForEntity(") + r'''}
class Entity {
 int fire,fireResistance=20,protection;boolean isImmuneToFire,water,lava,wet;float fallDistance;
 World worldObj=new World();Capabilities capabilities=new Capabilities();Random rand=new Random(919);
 void attackEntityFrom(DamageSource source,float damage){}void dealFireDamage(int amount){}
 boolean isInLava(){return lava;}boolean isWet(){return wet||water;}boolean isBurning(){return fire>0;}
 void playSound(String name,float volume,float pitch){}Box getEntityBoundingBox(){return new Box();}
 int getInventory(){return protection;}
''' + method(entity, "protected void setOnFireFromLava(") + method(entity, "public void setFire(") + method(entity, "public void extinguish(") + r'''
 void tick(){if(water)fire=0;
''' + update + movement + extinguish + r'''}
}
public class LocalFireReference {
 public static void main(String[] args)throws Exception {
  Entity entity=new Entity();
  try(Scanner input=new Scanner(new File(args[0]));PrintWriter output=new PrintWriter(args[1])){
   while(input.hasNextInt()){
    boolean reset=input.nextInt()!=0,active=input.nextInt()!=0;
    entity.water=input.nextInt()!=0;entity.lava=input.nextInt()!=0;entity.wet=input.nextInt()!=0;
    entity.worldObj.flammable=input.nextInt()!=0;entity.capabilities.disableDamage=input.nextInt()!=0;entity.protection=input.nextInt();
    if(reset||!active)entity.fire=0;if(active)entity.tick();output.println(Math.max(0,entity.fire));
   }
  }
 }
}
'''
    source = output / "LocalFireReference.java"
    source.write_text(java, encoding="utf-8")
    rng = random.Random(919)
    fixtures = []
    for _ in range(180):
        fixtures.append((1, 1, 0, 0, 0, 0, 0, 0))
        for _ in range(rng.randint(2, 12)):
            material = rng.randrange(5)
            state = (0, rng.random() > .03, material == 1, material == 2,
                     rng.random() < .12, material in (2, 3), rng.random() < .08, rng.choice((0, 0, 0, 1, 2, 3, 4)))
            fixtures.extend([state] * rng.randint(1, 400))
    inputs = output / "fixtures.txt"
    inputs.write_text("".join(" ".join(str(int(value)) for value in state) + "\n" for state in fixtures))
    expected, actual = output / "java.txt", output / "native.txt"
    subprocess.run(["javac", "-encoding", "UTF-8", str(source)], check=True)
    subprocess.run(["java", "-cp", str(output), "LocalFireReference", str(inputs), str(expected)], check=True)
    subprocess.run([str(args.native.resolve()), "--fixtures", str(inputs), str(actual)], check=True)
    java_ticks, native_ticks = expected.read_text().splitlines(), actual.read_text().splitlines()
    assert len(java_ticks) == len(native_ticks) == len(fixtures)
    mismatches = [i for i, (a, b) in enumerate(zip(java_ticks, native_ticks)) if a != b]
    report = {"ticks": len(fixtures), "mismatches": len(mismatches),
              "source_sha256": {str(p.relative_to(base)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))
    if mismatches:
        print("First differences:", [(i, java_ticks[i], native_ticks[i]) for i in mismatches[:10]])
        raise SystemExit(1)


if __name__ == "__main__":
    main()
