import java.io.*;
import java.lang.reflect.*;
import java.util.*;
import net.minecraft.entity.Entity;
import net.minecraft.entity.projectile.*;
import net.minecraft.entity.item.EntityExpBottle;
import net.minecraft.entity.item.EntityEnderPearl;
import net.minecraft.init.*;
import net.minecraft.item.*;
import net.minecraft.potion.*;
import net.minecraft.world.World;

/** Runs the original projectile onUpdate implementations in a controlled world. */
public final class ProjectileReference {
    static String bits(double value){return Long.toUnsignedString(Double.doubleToRawLongBits(value));}
    public static void main(String[] args)throws Exception {
        Bootstrap.register();
        try(PrintWriter out=new PrintWriter(new OutputStreamWriter(new FileOutputStream(args[1]),"UTF-8"))){
            if(args[0].equals("--potions")){
                for(int meta=0;meta<32768;++meta){ItemStack stack=new ItemStack(Items.potionitem,1,meta);List<PotionEffect> effects=Items.potionitem.getEffects(stack);PotionEffect effect=effects==null||effects.isEmpty()?null:effects.get(0);out.println(meta+" "+(effect==null?0:effect.getPotionID())+" "+(effect==null?0:effect.getAmplifier())+" "+(effect==null?0:effect.getDuration())+" "+Items.potionitem.getItemStackDisplayName(stack));}return;
            }
            Field remote=World.class.getDeclaredField("isRemote");remote.setAccessible(true);Field rng=Entity.class.getDeclaredField("rand");rng.setAccessible(true);
            DropEntityReference.FixtureWorld world=new DropEntityReference.FixtureWorld();remote.setBoolean(world,true);
            try(BufferedReader input=new BufferedReader(new FileReader(args[0]))){
                for(String line;(line=input.readLine())!=null;){if(line.isEmpty()||line.startsWith("#"))continue;String[] values=line.split(" ");int type=Integer.parseInt(values[0]),scenario=Integer.parseInt(values[1]),count=Integer.parseInt(values[8]);
                    double x=Double.parseDouble(values[2]),y=Double.parseDouble(values[3]),z=Double.parseDouble(values[4]),mx=Double.parseDouble(values[5]),my=Double.parseDouble(values[6]),mz=Double.parseDouble(values[7]);
                    world.blocks.clear();if(scenario!=0)for(int bx=-4;bx<=4;++bx)for(int bz=-4;bz<=4;++bz)for(int by=63;by<=(scenario==2?70:63);++by)world.put(bx,by,bz,(scenario==2?9:1)<<4);
                    Entity entity;
                    switch(type){case 60:entity=new EntityArrow(world,x,y,z);break;case 61:entity=new EntitySnowball(world,x,y,z);break;case 62:entity=new EntityEgg(world,x,y,z);break;case 65:entity=new EntityEnderPearl(world,x,y,z);break;case 73:entity=new EntityPotion(world,x,y,z,16389);break;case 75:entity=new EntityExpBottle(world,x,y,z);break;case 90:entity=new EntityFishHook(world,x,y,z,new DropEntityReference.FixturePlayer(world));break;default:throw new IllegalArgumentException();}
                    entity.setEntityId(5);entity.setVelocity(mx,my,mz);((Random)rng.get(entity)).setSeed(0);
                    Class<?> owner=type==60?EntityArrow.class:type==90?EntityFishHook.class:EntityThrowable.class;Field ground=owner.getDeclaredField("inGround");ground.setAccessible(true);
                    for(int tick=0;tick<count;++tick){if(!entity.isDead){++entity.ticksExisted;entity.onUpdate();}out.print(type+" "+tick+" "+(ground.getBoolean(entity)?1:0));for(double value:new double[]{entity.posX,entity.posY,entity.posZ,entity.motionX,entity.motionY,entity.motionZ})out.print(" "+bits(value));out.println();}
                }
            }
        }
    }
}
