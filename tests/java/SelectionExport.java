import java.io.*;
import com.google.gson.*;
import net.minecraft.block.*;
import net.minecraft.block.state.IBlockState;
import net.minecraft.init.Bootstrap;
import net.minecraft.util.*;

/** Exports selected and ray bounds separately; cactus/torch are not collision cubes. */
public final class SelectionExport {
    static JsonArray bounds(double... values){JsonArray out=new JsonArray();for(double v:values)out.add(new JsonPrimitive(v));return out;}
    public static void main(String[] args)throws Exception{
        Bootstrap.register();PhysicsReference.FixtureWorld world=new PhysicsReference.FixtureWorld();BlockPos pos=new BlockPos(0,0,0);world.load(0,0);
        JsonArray records=new JsonArray();
        for(int id=0;id<198;++id)for(int meta=0;meta<16;++meta){
            JsonObject record=new JsonObject();record.addProperty("state",(id<<4)|meta);Block block=Block.getBlockById(id);
            try{
                IBlockState state=block.getStateFromMeta(meta);world.blocks.clear();world.blocks.put(pos,state);
                block.setBlockBoundsBasedOnState(world,pos);
                record.addProperty("collision",block.getCollisionBoundingBox(world,pos,state)!=null);
                record.addProperty("ray",block.canCollideCheck(state,false));record.addProperty("liquidRay",block.canCollideCheck(state,true));
                // Torch bounds are initialized by collisionRayTrace, not by its
                // inherited setBlockBoundsBasedOnState; prime the exact routine.
                block.collisionRayTrace(world,pos,new Vec3(-2,.5,.5),new Vec3(2,.5,.5));
                record.add("rayBounds",bounds(block.getBlockBoundsMinX(),block.getBlockBoundsMinY(),block.getBlockBoundsMinZ(),block.getBlockBoundsMaxX(),block.getBlockBoundsMaxY(),block.getBlockBoundsMaxZ()));
                AxisAlignedBB selected=block.getSelectedBoundingBox(world,pos);
                record.add("selected",bounds(selected.minX,selected.minY,selected.minZ,selected.maxX,selected.maxY,selected.maxZ));
                record.addProperty("valid",true);
            }catch(IllegalArgumentException ex){record.addProperty("valid",false);}
            records.add(record);
        }
        try(Writer out=new FileWriter(args[0])){new GsonBuilder().setPrettyPrinting().create().toJson(records,out);}
    }
}
