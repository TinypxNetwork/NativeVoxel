#!/usr/bin/env python3
"""Compare native pickup frames with the unmodified MCP EntityPickupFX.java."""
import argparse
import hashlib
import json
import random
import subprocess
from pathlib import Path


STUBS = {
    "net/minecraft/entity/Entity.java": """
package net.minecraft.entity;
public class Entity {
    public double posX,posY,posZ,lastTickPosX,lastTickPosY,lastTickPosZ,motionX,motionY,motionZ;
    public float rotationYaw;
    public boolean isDead;
}
""",
    "net/minecraft/world/World.java": "package net.minecraft.world; public class World {}",
    "net/minecraft/client/particle/EntityFX.java": """
package net.minecraft.client.particle;
import net.minecraft.entity.Entity;
import net.minecraft.world.World;
public class EntityFX extends Entity {
    public static double interpPosX,interpPosY,interpPosZ;
    public EntityFX(World world,double x,double y,double z,double mx,double my,double mz){posX=x;posY=y;posZ=z;motionX=mx;motionY=my;motionZ=mz;}
    public int getBrightnessForRender(float partial){return 0x800030;}
    public void setDead(){isDead=true;}
}
""",
    "net/minecraft/client/Minecraft.java": """
package net.minecraft.client;
import net.minecraft.client.renderer.entity.RenderManager;
public class Minecraft {
    private static final Minecraft instance=new Minecraft();
    private final RenderManager manager=new RenderManager();
    public static Minecraft getMinecraft(){return instance;}
    public RenderManager getRenderManager(){return manager;}
}
""",
    "net/minecraft/client/renderer/WorldRenderer.java": "package net.minecraft.client.renderer; public class WorldRenderer {}",
    "net/minecraft/client/renderer/GlStateManager.java": "package net.minecraft.client.renderer; public class GlStateManager {public static void color(float r,float g,float b,float a){}}",
    "net/minecraft/client/renderer/OpenGlHelper.java": "package net.minecraft.client.renderer; public class OpenGlHelper {public static int lightmapTexUnit;public static void setLightmapTextureCoords(int unit,float block,float sky){}}",
    "net/minecraft/client/renderer/entity/RenderManager.java": """
package net.minecraft.client.renderer.entity;
import net.minecraft.entity.Entity;
public class RenderManager {
    public double x,y,z;
    public float yaw;
    public boolean renderEntityWithPosYaw(Entity entity,double x,double y,double z,float yaw,float partial){this.x=x;this.y=y;this.z=z;this.yaw=yaw;return true;}
}
""",
    "PickupReference.java": """
import java.io.*;
import net.minecraft.client.Minecraft;
import net.minecraft.client.particle.EntityFX;
import net.minecraft.client.particle.EntityPickupFX;
import net.minecraft.client.renderer.entity.RenderManager;
import net.minecraft.entity.Entity;
import net.minecraft.world.World;
public class PickupReference {
    static String bits(float value){return Integer.toUnsignedString(Float.floatToRawIntBits(value));}
    public static void main(String[] args)throws Exception{
        try(BufferedReader input=new BufferedReader(new FileReader(args[0]))){
            for(String line;(line=input.readLine())!=null;){
                if(line.isEmpty()||line.startsWith("#"))continue;
                String[] values=line.split(" ");int age=Integer.parseInt(values[0]);float partial=Float.parseFloat(values[1]);
                Entity source=new Entity(),target=new Entity();
                source.posX=Double.parseDouble(values[2]);source.posY=Double.parseDouble(values[3]);source.posZ=Double.parseDouble(values[4]);
                target.lastTickPosX=Double.parseDouble(values[5]);target.lastTickPosY=Double.parseDouble(values[6]);target.lastTickPosZ=Double.parseDouble(values[7]);
                target.posX=Double.parseDouble(values[8]);target.posY=Double.parseDouble(values[9]);target.posZ=Double.parseDouble(values[10]);
                source.rotationYaw=Float.parseFloat(values[11]);
                EntityFX.interpPosX=Double.parseDouble(values[12]);EntityFX.interpPosY=Double.parseDouble(values[13]);EntityFX.interpPosZ=Double.parseDouble(values[14]);
                EntityPickupFX fx=new EntityPickupFX(new World(),source,target,0.5F);
                if(fx.getFXLayer()!=3)throw new AssertionError("wrong pickup particle layer");
                for(int i=0;i<age;++i)fx.onUpdate();
                if(fx.isDead)System.out.println("dead");
                else{
                    fx.renderParticle(null,null,partial,0,0,0,0,0);
                    RenderManager result=Minecraft.getMinecraft().getRenderManager();
                    System.out.println("live "+bits((float)result.x)+" "+bits((float)result.y)+" "+bits((float)result.z)+" "+bits(result.yaw));
                }
            }
        }
    }
}
""",
}


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcp", type=Path, default=root.parent / "MCP-919-main")
    parser.add_argument("--native", type=Path, default=root / "build/tests/pickup_tests.exe")
    args = parser.parse_args()
    original = args.mcp.resolve() / "src/minecraft/net/minecraft/client/particle/EntityPickupFX.java"
    build = root / "build/pickup_reference"
    sources, classes = build / "stubs", build / "classes"
    classes.mkdir(parents=True, exist_ok=True)
    for name, contents in STUBS.items():
        target = sources / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(contents, encoding="utf-8")
    subprocess.run(["javac", "-encoding", "UTF-8", "--release", "8", "-d", str(classes),
                    *map(str, sorted(sources.rglob("*.java"))), str(original)], check=True)
    rows = []
    for age in range(5):
        for partial in (0, .125, .5, .875, .9999999403953552, 1):
            for base in (0, 16, -16, 29999999.5, -29999999.5):
                rows.append([age, partial, base + 2, 63.875, base - 1, base, 64, base,
                             base + 1.25, 65.75, base - 2, 97.5, base - 3, 65.62, base - 3])
    rng = random.Random(18903)
    for _ in range(6000):
        source = [rng.uniform(-30000000, 30000000), rng.uniform(-64, 320), rng.uniform(-30000000, 30000000)]
        previous = [value + rng.uniform(-8, 8) for value in source]
        current = [value + rng.uniform(-4, 4) for value in previous]
        origin = [value + rng.uniform(-128, 128) for value in source]
        rows.append([rng.randrange(5), rng.random(), *source, *previous, *current, rng.uniform(-1080, 1080), *origin])
    fixture = build / "pickup.fixture"
    fixture.write_text("\n".join(" ".join(map(str, row)) for row in rows) + "\n", encoding="ascii")
    java = subprocess.run(["java", "-cp", str(classes), "PickupReference", str(fixture)], check=True, capture_output=True, text=True).stdout.splitlines()
    native = subprocess.run([str(args.native.resolve()), "--trace", str(fixture)], check=True, capture_output=True, text=True).stdout.splitlines()
    (build / "java.txt").write_text("\n".join(java) + "\n", encoding="ascii")
    (build / "native.txt").write_text("\n".join(native) + "\n", encoding="ascii")
    if len(java) != len(rows) or len(native) != len(rows):
        raise RuntimeError(f"unexpected output lengths Java={len(java)}, native={len(native)}, expected={len(rows)}")
    for index, (expected, actual) in enumerate(zip(java, native)):
        if expected != actual:
            raise RuntimeError(f"pickup case {index}: Java={expected}, native={actual}, input={rows[index]}")
    report = {
        "cases": len(rows),
        "active_frames": sum(line.startswith("live") for line in java),
        "expired_frames": sum(line == "dead" for line in java),
        "mismatches": 0,
        "source": str(original),
        "source_sha256": hashlib.sha256(original.read_bytes()).hexdigest(),
        "method": "Unmodified EntityPickupFX; stub only the world/entity and GL/render endpoints. Compare lifecycle, render-relative binary32 positions and yaw bit for bit.",
    }
    (build / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Original EntityPickupFX: {report['cases']} cases, {report['active_frames']} active and {report['expired_frames']} expired, bit-exact")


if __name__ == "__main__":
    main()
