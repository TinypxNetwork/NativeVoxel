"""Compare fishing lines with the actual MCP RenderFish method and Vec3 rotations."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def method(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, default=ROOT / "bin/entity-effects-tests.exe")
    parser.add_argument("--mcp", type=Path, default=ROOT.parent / "MCP-919-main")
    args = parser.parse_args()
    build = ROOT / "build/entity_effects_reference"
    build.mkdir(parents=True, exist_ok=True)
    base = args.mcp / "src/minecraft/net/minecraft"
    fish_path = base / "client/renderer/entity/RenderFish.java"
    vec_path = base / "util/Vec3.java"
    math_path = base / "util/MathHelper.java"
    fish, vec, math = (p.read_text(encoding="utf-8") for p in (fish_path, vec_path, math_path))
    java = r'''import java.io.*;import java.util.*;
class Entity {double posX,posY,posZ,prevPosX,prevPosY,prevPosZ;}
class EntityPlayer extends Entity {float prevRotationPitch,rotationPitch,prevRotationYaw,rotationYaw,prevRenderYawOffset,renderYawOffset,swing,eye;boolean sneaking;
 float getSwingProgress(float p){return swing;}float getEyeHeight(){return eye;}boolean isSneaking(){return sneaking;}}
class EntityFishHook extends Entity {EntityPlayer angler;}
class Options {int thirdPersonView;}
class RenderManager {Options options=new Options();float playerViewY,playerViewX;}
class Minecraft {static Minecraft instance=new Minecraft();EntityPlayer thePlayer;static Minecraft getMinecraft(){return instance;}}
class DefaultVertexFormats {static int POSITION_TEX_NORMAL,POSITION_COLOR;}
class GlStateManager {
 static void pushMatrix(){}static void popMatrix(){}static void enableRescaleNormal(){}static void disableRescaleNormal(){}
 static void disableTexture2D(){}static void enableTexture2D(){}static void disableLighting(){}static void enableLighting(){}
 static void translate(float x,float y,float z){}static void rotate(float angle,float x,float y,float z){}static void scale(float x,float y,float z){}
}
class WorldRenderer {
 int mode;List<double[]> points=new ArrayList<>();
 void begin(int m,int f){mode=m;if(m==3)points.clear();}
 WorldRenderer pos(double x,double y,double z){if(mode==3)points.add(new double[]{x,y,z});return this;}
 WorldRenderer tex(double u,double v){return this;}WorldRenderer normal(float x,float y,float z){return this;}
 WorldRenderer color(int r,int g,int b,int a){return this;}void endVertex(){}
}
class Tessellator {static Tessellator instance=new Tessellator();WorldRenderer renderer=new WorldRenderer();static Tessellator getInstance(){return instance;}WorldRenderer getWorldRenderer(){return renderer;}void draw(){}}
class RenderBase {RenderManager renderManager=new RenderManager();void bindEntityTexture(EntityFishHook hook){}public void doRender(EntityFishHook hook,double x,double y,double z,float yaw,float partial){}}
class MathHelper {static final float[] SIN_TABLE=new float[65536];static{for(int i=0;i<65536;++i)SIN_TABLE[i]=(float)Math.sin((double)i*Math.PI*2.0D/65536.0D);}
''' + "\n".join(method(math, signature) for signature in ("public static float sin(", "public static float cos(", "public static float sqrt_float(")) + r'''}
class Vec3 {double xCoord,yCoord,zCoord;Vec3(double x,double y,double z){xCoord=x;yCoord=y;zCoord=z;}
''' + method(vec, "public Vec3 rotatePitch(") + method(vec, "public Vec3 rotateYaw(") + r'''}
public class EntityEffectsReference extends RenderBase {
''' + method(fish, "public void doRender(") + r'''
 public static void main(String[] args)throws Exception {
  EntityEffectsReference render=new EntityEffectsReference();
  try(DataInputStream in=new DataInputStream(new FileInputStream(args[0]));DataOutputStream out=new DataOutputStream(new FileOutputStream(args[1]))){
   int count=in.readInt();out.writeInt(count);
   for(int i=0;i<count;i++){
    EntityFishHook hook=new EntityFishHook();EntityPlayer player=new EntityPlayer();hook.angler=player;
    hook.posX=hook.prevPosX=in.readDouble();hook.posY=hook.prevPosY=in.readDouble();hook.posZ=hook.prevPosZ=in.readDouble();
    player.posX=player.prevPosX=in.readDouble();player.posY=player.prevPosY=in.readDouble();player.posZ=player.prevPosZ=in.readDouble();
    player.rotationYaw=player.prevRotationYaw=in.readFloat();player.rotationPitch=player.prevRotationPitch=in.readFloat();player.renderYawOffset=player.prevRenderYawOffset=in.readFloat();player.swing=in.readFloat();player.eye=in.readFloat();
    Minecraft.instance.thePlayer=in.readBoolean()?player:null;render.renderManager.options.thirdPersonView=in.readBoolean()?1:0;player.sneaking=in.readBoolean();
    render.doRender(hook,hook.posX,hook.posY,hook.posZ,0,.5F);
    List<double[]> points=Tessellator.instance.renderer.points;if(points.size()!=17)throw new IOException("Source did not draw a 17-vertex line");
    for(double[] point:points)for(double value:point)out.writeDouble(value);
   }
  }
 }
}
'''
    java_path = build / "EntityEffectsReference.java"
    java_path.write_text(java, encoding="utf-8")
    rng = random.Random(919)
    cases = []
    for local in (False, True):
        for third in (False, True):
            for sneaking in (False, True):
                for _ in range(128):
                    x, y, z = rng.uniform(-30000000, 30000000), rng.uniform(-60, 300), rng.uniform(-30000000, 30000000)
                    case = (x + rng.uniform(-35, 35), y + rng.uniform(-10, 5), z + rng.uniform(-35, 35), x, y, z,
                            rng.uniform(-720, 720), rng.choice((-90, 90, 0, rng.uniform(-90, 90))), rng.uniform(-360, 360),
                            rng.choice((0, .25, .5, .75, rng.random())), 1.54 if sneaking else 1.62, local, third, sneaking)
                    cases.append(case)
    data = struct.pack(">i", len(cases)) + b"".join(struct.pack(">6d5f???", *case) for case in cases)
    fixtures = build / "fixtures.bin"
    fixtures.write_bytes(data)
    expected, actual = build / "java.bin", build / "native.bin"
    subprocess.run(["javac", "-encoding", "UTF-8", str(java_path)], check=True)
    subprocess.run(["java", "-cp", str(build), "EntityEffectsReference", str(fixtures), str(expected)], check=True)
    env = dict(os.environ)
    env["PATH"] = "D:/Strawberry/c/bin" + os.pathsep + env.get("PATH", "")
    subprocess.run([str(args.native.resolve()), "--line-fixtures", str(fixtures), str(actual)], check=True, env=env)
    java_data, native_data = expected.read_bytes(), actual.read_bytes()
    assert len(java_data) == len(native_data) == 4 + len(cases) * 17 * 3 * 8
    maximum, mismatches = 0, []
    for index, ((a,), (b,)) in enumerate(zip(struct.iter_unpack(">d", java_data[4:]), struct.iter_unpack(">d", native_data[4:]))):
        error = abs(a - b)
        maximum = max(maximum, error)
        if error > 1e-8:
            mismatches.append({"case": index // 51, "component": index % 51, "java": a, "native": b})
    report = {"cases": len(cases), "coordinates": len(cases) * 51, "mismatches": len(mismatches), "maximum_error": maximum,
              "source_sha256": {str(p.relative_to(base)): hashlib.sha256(p.read_bytes()).hexdigest() for p in (fish_path, vec_path, math_path)}}
    (build / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (build / "mismatches.json").write_text(json.dumps(mismatches[:100], indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))
    if mismatches:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
