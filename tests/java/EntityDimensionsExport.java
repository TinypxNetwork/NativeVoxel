import java.io.*;
import com.google.gson.*;
import net.minecraft.entity.*;
import net.minecraft.entity.item.*;
import net.minecraft.entity.monster.EntitySlime;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.projectile.*;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Blocks;
import net.minecraft.util.*;
import net.minecraft.world.World;

/** Samples actual vanilla spawn constructors and target-collision getters. */
public final class EntityDimensionsExport {
    static JsonObject sample(int kind,int type,Entity e) {
        if(e instanceof EntityAgeable)((EntityAgeable)e).setScaleForAge(false);
        if(e instanceof EntitySlime)e.onDataWatcherUpdate(16); // protocol size-1 base
        JsonObject out=new JsonObject();out.addProperty("kind",kind);out.addProperty("type",type);
        out.addProperty("className",kind==0?"EntityPlayer":e.getClass().getSimpleName());out.addProperty("width",e.width);out.addProperty("height",e.height);
        out.addProperty("collisionBorder",e.getCollisionBorderSize());out.addProperty("collidable",e.canBeCollidedWith());
        out.addProperty("customBounds",e instanceof EntityHanging);out.addProperty("multipart",e.getParts()!=null);
        return out;
    }
    public static void main(String[] args)throws Exception {
        Bootstrap.register();World w=new PhysicsReference.FixtureWorld();JsonArray result=new JsonArray();
        EntityPlayer player=new PhysicsReference.FixturePlayer((PhysicsReference.FixtureWorld)w);result.add(sample(0,0,player));
        int[] mobs={30,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,90,91,92,93,94,95,96,97,98,99,100,101,120};
        for(int id:mobs){Entity e=EntityList.createEntityByID(id,w);if(e==null)throw new IOException("missing vanilla entity "+id);result.add(sample(1,id,e));}
        // Match NetHandlerPlayClient.handleSpawnObject constructors, including
        // those whose positional constructor sets size unlike the World-only one.
        result.add(sample(2,1,new EntityBoat(w,0,64,0)));
        result.add(sample(2,2,new EntityItem(w,0,64,0)));
        result.add(sample(2,10,EntityMinecart.getMinecart(w,0,64,0,EntityMinecart.EnumMinecartType.RIDEABLE)));
        result.add(sample(2,50,new EntityTNTPrimed(w,0,64,0,null)));
        result.add(sample(2,51,new EntityEnderCrystal(w,0,64,0)));
        result.add(sample(2,60,new EntityArrow(w,0,64,0)));
        result.add(sample(2,61,new EntitySnowball(w,0,64,0)));
        result.add(sample(2,62,new EntityEgg(w,0,64,0)));
        result.add(sample(2,63,new EntityLargeFireball(w,0,64,0,0,0,1)));
        result.add(sample(2,64,new EntitySmallFireball(w,0,64,0,0,0,1)));
        result.add(sample(2,65,new EntityEnderPearl(w,0,64,0)));
        result.add(sample(2,66,new EntityWitherSkull(w,0,64,0,0,0,1)));
        result.add(sample(2,70,new EntityFallingBlock(w,0,64,0,Blocks.sand.getDefaultState())));
        result.add(sample(2,71,new EntityItemFrame(w,new BlockPos(0,64,0),EnumFacing.SOUTH)));
        result.add(sample(2,72,new EntityEnderEye(w,0,64,0)));
        result.add(sample(2,73,new EntityPotion(w,0,64,0,0)));
        result.add(sample(2,75,new EntityExpBottle(w,0,64,0)));
        result.add(sample(2,76,new EntityFireworkRocket(w,0,64,0,null)));
        result.add(sample(2,77,new EntityLeashKnot(w,new BlockPos(0,64,0))));
        result.add(sample(2,78,new EntityArmorStand(w,0,64,0)));
        result.add(sample(2,90,new EntityFishHook(w,0,64,0,player)));
        try(Writer out=new FileWriter(args[0])){new GsonBuilder().setPrettyPrinting().create().toJson(result,out);}
    }
}
