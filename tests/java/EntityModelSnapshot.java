import java.io.*;
import java.lang.reflect.*;
import java.util.*;
import com.google.gson.*;
import net.minecraft.client.model.*;
import net.minecraft.entity.*;
import net.minecraft.entity.passive.*;
import net.minecraft.entity.monster.*;
import net.minecraft.entity.boss.*;
import net.minecraft.entity.item.EntityArmorStand;
import net.minecraft.init.Bootstrap;
import net.minecraft.util.Rotations;
import net.minecraft.world.World;

/** Actual original Model*.setLivingAnimations/setRotationAngles snapshots. */
public final class EntityModelSnapshot {
    public static class Pose {
        public int flags,entityId,horseType,invulnerabilityTicks;
        public boolean child,sneaking;
        public float headYaw,headPitch,limbSwing,limbSwingAmount,ageTicks,partialTick,health=20,grazing,rearing,mouthOpen,horseSize=.5f;
        public float interestedAngle,headShake,maneShake,bodyShake,tailShake,jumpProgress,spikeExtension=1,tailPhase=-1,bodyYaw;
        public float[] sideHeadYaw={0,0},sideHeadPitch={0,0},standHead={0,0,0},standBody={0,0,0},standLeftArm={-10,0,-10},standRightArm={-15,0,10},standLeftLeg={-1,0,-1},standRightLeg={1,0,1};
        boolean flag(int bit){return (flags&(1<<bit))!=0;}
        static Pose parse(String[] fields)throws Exception{
            Pose p=new Pose();for(int i=1;i<fields.length;++i){String[] pair=fields[i].split("=");String key=pair[0];
                if(Character.isDigit(key.charAt(key.length()-1))){int n=key.charAt(key.length()-1)-'0';float[] a=(float[])Pose.class.getField(key.substring(0,key.length()-1)).get(p);a[n]=Float.parseFloat(pair[1]);}
                else{Field f=Pose.class.getField(key);if(f.getType()==int.class)f.setInt(p,Integer.parseInt(pair[1]));else if(f.getType()==boolean.class)f.setBoolean(p,!pair[1].equals("0"));else f.setFloat(p,Float.parseFloat(pair[1]));}
            }return p;
        }
    }
    static class Wolf extends EntityWolf {
        Pose p;Wolf(World w,Pose pose){super(w);p=pose;setTamed(p.flag(2));setHealth(p.health);getDataWatcher().updateObject(18,Float.valueOf(p.health));}
        public boolean isAngry(){return p!=null&&p.flag(1);}public boolean isSitting(){return p!=null&&p.flag(0);}
        public float getInterestedAngle(float f){return p.interestedAngle;}
        public float getShakeAngle(float f,float offset){return offset==0?p.headShake:offset==-.08f?p.maneShake:offset==-.16f?p.bodyShake:p.tailShake;}
    }
    static class Cat extends EntityOcelot {
        Pose p;Cat(World w,Pose pose){super(w);p=pose;}public boolean isSitting(){return p!=null&&p.flag(0);}public boolean isSprinting(){return p!=null&&p.flag(3);}public boolean isSneaking(){return p!=null&&p.sneaking;}
    }
    static class Horse extends EntityHorse {
        Pose p;Horse(World w,Pose pose){super(w);p=pose;field_110278_bp=p.flag(8)?1:0;if(p.flag(7))riddenByEntity=new EntityPig(w);}
        public float getGrassEatingAmount(float f){return p.grazing;}public float getRearingAmount(float f){return p.rearing;}public float getMouthOpennessAngle(float f){return p.mouthOpen;}
        public boolean isHorseSaddled(){return p!=null&&p.flag(5);}public boolean isChested(){return p!=null&&p.flag(6);}public boolean isAdultHorse(){return p==null||!p.child;}
        public float getHorseSize(){return p==null?1:p.horseSize;}public int getHorseType(){return p==null?0:p.horseType;}
    }
    static class Rabbit extends EntityRabbit {Pose p;Rabbit(World w,Pose pose){super(w);p=pose;}public float func_175521_o(float f){return p.jumpProgress;}}
    static class Guardian extends EntityGuardian {
        Pose p;Guardian(World w,Pose pose){super(w);p=pose;}public float func_175469_o(float f){return p.spikeExtension;}public float func_175471_a(float f){return p.tailPhase>=0?p.tailPhase:p.ageTicks*.2f;}public boolean hasTargetedEntity(){return false;}
    }
    static class Wither extends EntityWither {
        Pose p;Wither(World w,Pose pose){super(w);p=pose;}public float func_82207_a(int i){return p.sideHeadYaw[i]*(180f/(float)Math.PI);}public float func_82210_r(int i){return p.sideHeadPitch[i]*(180f/(float)Math.PI);}
    }
    static class Bat extends EntityBat {Pose p;Bat(World w,Pose pose){super(w);p=pose;}public boolean getIsBatHanging(){return p!=null&&p.flag(4);}}
    static class Stand extends EntityArmorStand {
        Pose p;Stand(World w,Pose pose){super(w);p=pose;}
        Rotations rotation(float[] r){return new Rotations(r[0],r[1],r[2]);}
        public Rotations getHeadRotation(){return p==null?super.getHeadRotation():rotation(p.standHead);}public Rotations getBodyRotation(){return p==null?super.getBodyRotation():rotation(p.standBody);}
        public Rotations getLeftArmRotation(){return p==null?super.getLeftArmRotation():rotation(p.standLeftArm);}public Rotations getRightArmRotation(){return p==null?super.getRightArmRotation():rotation(p.standRightArm);}
        public Rotations getLeftLegRotation(){return p==null?super.getLeftLegRotation():rotation(p.standLeftLeg);}public Rotations getRightLegRotation(){return p==null?super.getRightLegRotation():rotation(p.standRightLeg);}
        public boolean getShowArms(){return p!=null&&p.flag(11);}public boolean hasNoBasePlate(){return p!=null&&p.flag(12);}
    }
    public static void main(String[] args)throws Exception{
        Bootstrap.register();PhysicsReference.FixtureWorld w=new PhysicsReference.FixtureWorld();int sample=0;
        try(BufferedReader input=new BufferedReader(new FileReader(args[0]));PrintWriter out=new PrintWriter(new FileWriter(args[1]))){
            for(String line;(line=input.readLine())!=null;){if(line.isEmpty()||line.startsWith("#"))continue;String[] f=line.split("\\s+");Pose p=Pose.parse(f);ModelBase m;EntityLivingBase e;
                switch(f[0]){
                case "wolf":m=new ModelWolf();e=new Wolf(w,p);break;case "ocelot":m=new ModelOcelot();e=new Cat(w,p);break;
                case "horse":m=new ModelHorse();e=new Horse(w,p);break;case "rabbit":m=new ModelRabbit();e=new Rabbit(w,p);break;
                case "witch":m=new ModelWitch(0);((ModelWitch)m).field_82900_g=p.flag(14);e=new EntityWitch(w);break;
                case "guardian":m=new ModelGuardian();e=new Guardian(w,p);break;case "wither":m=new ModelWither(0);e=new Wither(w,p);break;
                case "bat":m=new ModelBat();e=new Bat(w,p);break;case "armorstand":m=new ModelArmorStand();e=new Stand(w,p);break;
                default:throw new IOException("unknown family");}
                e.setEntityId(p.entityId);e.ticksExisted=(int)p.ageTicks;e.prevRotationPitch=e.rotationPitch=p.headPitch*(180f/(float)Math.PI);
                e.prevRenderYawOffset=e.renderYawOffset=0;e.prevRotationYawHead=e.rotationYawHead=p.headYaw*(180f/(float)Math.PI);e.rotationYaw=p.bodyYaw*(180f/(float)Math.PI);
                m.isChild=p.child;m.setLivingAnimations(e,p.limbSwing,p.limbSwingAmount,p.partialTick);
                m.setRotationAngles(p.limbSwing,p.limbSwingAmount,e instanceof Wolf?((Wolf)e).getTailRotation():p.ageTicks,
                    p.headYaw*(180f/(float)Math.PI),p.headPitch*(180f/(float)Math.PI),.0625f,e);
                JsonObject snapshot=EntityModelExport.export(m);
                for(JsonElement value:snapshot.getAsJsonArray("parts")){
                    JsonObject part=value.getAsJsonObject();out.print(sample+" "+part.get("name").getAsString());
                    for(String key:new String[]{"pivot","angle","offset"})for(JsonElement coordinate:part.getAsJsonArray(key))out.printf(java.util.Locale.ROOT," %.17g",coordinate.getAsDouble());out.println();
                }++sample;
            }
        }
    }
}
