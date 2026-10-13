import java.io.*;
import java.util.Locale;
import net.minecraft.block.Block;
import net.minecraft.init.Bootstrap;
import net.minecraft.util.*;

/** Original World.rayTraceBlocks and selected AABBs, with no renderer/network. */
public final class SelectionReference {
    public static void main(String[] args)throws Exception {
        Bootstrap.register();Locale.setDefault(Locale.ROOT);
        PhysicsReference.FixtureWorld world=new PhysicsReference.FixtureWorld();
        try(BufferedReader input=new BufferedReader(new FileReader(args[0]));PrintWriter out=new PrintWriter(new FileWriter(args[1]))){
            for(String line;(line=input.readLine())!=null;){
                if(line.isEmpty()||line.startsWith("#"))continue;String[] f=line.split("\\s+");
                if(f[0].equals("W"))world=new PhysicsReference.FixtureWorld();
                else if(f[0].equals("B"))world.put(Integer.parseInt(f[1]),Integer.parseInt(f[2]),Integer.parseInt(f[3]),Integer.parseInt(f[4]));
                else if(f[0].equals("Q")){
                    int x=Integer.parseInt(f[1]),y=Integer.parseInt(f[2]),z=Integer.parseInt(f[3]);BlockPos pos=new BlockPos(x,y,z);
                    Block block=world.getBlockState(pos).getBlock();block.setBlockBoundsBasedOnState(world,pos);
                    // Selection is queried after aiming in vanilla. This primes
                    // the source's mutable torch/ray bounds for the exact state.
                    block.collisionRayTrace(world,pos,new Vec3(x-2,y+.5,z+.5),new Vec3(x+2,y+.5,z+.5));
                    AxisAlignedBB b=block.getSelectedBoundingBox(world,pos);
                    out.printf("Q %.17g %.17g %.17g %.17g %.17g %.17g%n",b.minX,b.minY,b.minZ,b.maxX,b.maxY,b.maxZ);
                }else if(f[0].equals("R")){
                    Vec3 start=new Vec3(Double.parseDouble(f[1]),Double.parseDouble(f[2]),Double.parseDouble(f[3]));
                    Vec3 end=new Vec3(Double.parseDouble(f[4]),Double.parseDouble(f[5]),Double.parseDouble(f[6]));
                    MovingObjectPosition hit=world.rayTraceBlocks(start,end,f[7].equals("1"),f[8].equals("1"),f[9].equals("1"));
                    if(hit==null)out.println("R 0");else out.printf("R %d %d %d %d %d %.17g %.17g %.17g%n",
                        hit.typeOfHit==MovingObjectPosition.MovingObjectType.BLOCK?1:2,hit.getBlockPos().getX(),hit.getBlockPos().getY(),hit.getBlockPos().getZ(),hit.sideHit.getIndex(),hit.hitVec.xCoord,hit.hitVec.yCoord,hit.hitVec.zCoord);
                }else throw new IOException("unknown fixture command");
            }
        }
    }
}
