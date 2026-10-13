import java.io.*;
import java.util.*;
import java.lang.reflect.*;
import java.util.concurrent.atomic.AtomicLong;
import com.mojang.authlib.GameProfile;
import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.client.multiplayer.ChunkProviderClient;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.*;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.*;
import net.minecraft.item.*;
import net.minecraft.nbt.*;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.*;
import net.minecraft.world.*;
import net.minecraft.world.chunk.*;
import net.minecraft.world.storage.*;

/** Unmodified EntityItem/EntityXPOrb motion, collision, inventory and XP methods. */
public final class DropEntityReference {
    static Field field(Class<?> type,String name)throws Exception {Field value=type.getDeclaredField(name);value.setAccessible(true);return value;}
    static final Field ITEM_AGE,ITEM_DELAY,ITEM_HEALTH,FIRE,FIRST,WEB,RNG,SEED,XP_HEALTH,XP_TARGET,XP_PLAYER;
    static {try{ITEM_AGE=field(EntityItem.class,"age");ITEM_DELAY=field(EntityItem.class,"delayBeforeCanPickup");ITEM_HEALTH=field(EntityItem.class,"health");FIRE=field(Entity.class,"fire");FIRST=field(Entity.class,"firstUpdate");WEB=field(Entity.class,"isInWeb");RNG=field(Entity.class,"rand");SEED=field(Random.class,"seed");XP_HEALTH=field(EntityXPOrb.class,"xpOrbHealth");XP_TARGET=field(EntityXPOrb.class,"xpTargetColor");XP_PLAYER=field(EntityXPOrb.class,"closestPlayer");}catch(Exception e){throw new RuntimeException(e);}}
    static class FixtureWorld extends World {
        final Map<BlockPos,IBlockState> blocks=new HashMap<>();
        final Set<Long> loaded=new HashSet<>();
        FixturePlayer player;boolean rain;
        FixtureWorld(){super(new SaveHandlerMP(),new WorldInfo(new WorldSettings(0L,WorldSettings.GameType.SURVIVAL,false,false,WorldType.DEFAULT),"drop_entity_reference"),WorldProvider.getProviderForDimension(0),new Profiler(),false);provider.registerWorld(this);chunkProvider=createChunkProvider();for(int x=-2;x<=2;++x)for(int z=-2;z<=2;++z)load(x,z);}
        protected IChunkProvider createChunkProvider(){return new ChunkProviderClient(this);}
        protected int getRenderDistanceChunks(){return 8;}
        protected boolean isChunkLoaded(int x,int z,boolean allowEmpty){return loaded.contains(((long)x<<32)|(z&0xffffffffL));}
        void load(int x,int z){if(loaded.add(((long)x<<32)|(z&0xffffffffL)))((ChunkProviderClient)chunkProvider).loadChunk(x,z);}
        void put(int x,int y,int z,int state){load(x>>4,z>>4);blocks.put(new BlockPos(x,y,z),Block.getBlockById(state>>4).getStateFromMeta(state&15));}
        public IBlockState getBlockState(BlockPos pos){return blocks.getOrDefault(pos,Blocks.air.getDefaultState());}
        public boolean isRainingAt(BlockPos pos){return rain;}
        public EntityPlayer getClosestPlayerToEntity(Entity entity,double range){return player!=null&&!player.spectator&&entity.getDistanceSqToEntity(player)<range*range?player:null;}
        public <T extends Entity> List<T> getEntitiesWithinAABB(Class<? extends T> type,AxisAlignedBB bounds){return Collections.emptyList();}
        public boolean spawnEntityInWorld(Entity entity){return true;}
        public void playSound(double x,double y,double z,String name,float volume,float pitch,boolean delay){}
    }
    static class FixturePlayer extends EntityPlayer {
        boolean spectator;
        FixturePlayer(FixtureWorld world){super(world,new GameProfile(new UUID(0,189),"NativeDrops"));}
        public boolean isSpectator(){return spectator;}
        public void playSound(String name,float volume,float pitch){}
    }
    static String bits(float value){return Integer.toUnsignedString(Float.floatToRawIntBits(value));}
    static String bits(double value){return Long.toUnsignedString(Double.doubleToRawLongBits(value));}
    static void printItem(PrintWriter out,ItemStack item){if(item==null||item.stackSize<=0)out.print(" -1 0 0");else out.print(" "+Item.getIdFromItem(item.getItem())+" "+item.stackSize+" "+item.getMetadata());}
    static ItemStack item(int id,int count,int damage){return id<0||count<=0?null:new ItemStack(Item.getItemById(id),count,damage);}
    static void dump(Entity entity,PrintWriter out)throws Exception {
        boolean orb=entity instanceof EntityXPOrb;EntityXPOrb xp=orb?(EntityXPOrb)entity:null;EntityItem item=orb?null:(EntityItem)entity;
        int age=orb?xp.xpOrbAge:ITEM_AGE.getInt(item),delay=orb?xp.delayBeforeCanPickup:ITEM_DELAY.getInt(item),health=orb?XP_HEALTH.getInt(xp):ITEM_HEALTH.getInt(item);
        out.print("E "+(entity.isDead?1:0)+" "+(entity.onGround?1:0)+" "+(entity.noClip?1:0)+" "+(WEB.getBoolean(entity)?1:0)+" "+(entity.isInWater()?1:0)+" "+age+" "+delay+" "+health+" "+FIRE.getInt(entity)+" "+entity.ticksExisted+" "+(orb?xp.xpColor:0)+" "+(orb?XP_TARGET.getInt(xp):0)+" "+(orb&&XP_PLAYER.get(xp)!=null?1:0));
        AxisAlignedBB bb=entity.getEntityBoundingBox();for(double v:new double[]{entity.posX,entity.posY,entity.posZ,entity.motionX,entity.motionY,entity.motionZ,bb.minX,bb.minY,bb.minZ,bb.maxX,bb.maxY,bb.maxZ})out.print(" "+bits(v));
        out.println(" "+bits(entity.fallDistance)+" "+((AtomicLong)SEED.get((Random)RNG.get(entity))).get());
    }
    public static void main(String[] args)throws Exception {
        Bootstrap.register();FixtureWorld world=new FixtureWorld();FixturePlayer player=new FixturePlayer(world);Entity entity=null;
        try(BufferedReader in=new BufferedReader(new FileReader(args[0]));PrintWriter out=new PrintWriter(new FileWriter(args[1]))){
            for(String line;(line=in.readLine())!=null;){if(line.isEmpty()||line.charAt(0)=='#')continue;String[] f=line.split("\\s+");int at=1;char mode=f[0].charAt(0);
                if(mode=='W'){world=new FixtureWorld();player=new FixturePlayer(world);}
                else if(mode=='B')world.put(Integer.parseInt(f[1]),Integer.parseInt(f[2]),Integer.parseInt(f[3]),Integer.parseInt(f[4]));
                else if(mode=='S'){boolean orb=f[at++].equals("1");int id=Integer.parseInt(f[at++]);double x=Double.parseDouble(f[at++]),y=Double.parseDouble(f[at++]),z=Double.parseDouble(f[at++]);entity=orb?new EntityXPOrb(world,x,y,z,3):new EntityItem(world,x,y,z,new ItemStack(Blocks.cobblestone));entity.setEntityId(id);entity.motionX=Double.parseDouble(f[at++]);entity.motionY=Double.parseDouble(f[at++]);entity.motionZ=Double.parseDouble(f[at++]);((Random)RNG.get(entity)).setSeed(Long.parseLong(f[at++]));int age=Integer.parseInt(f[at++]),delay=Integer.parseInt(f[at++]),health=Integer.parseInt(f[at++]);if(orb){EntityXPOrb xp=(EntityXPOrb)entity;xp.xpOrbAge=age;xp.delayBeforeCanPickup=delay;XP_HEALTH.setInt(xp,health);}else{ITEM_AGE.setInt(entity,age);ITEM_DELAY.setInt(entity,delay);ITEM_HEALTH.setInt(entity,health);}FIRE.setInt(entity,Integer.parseInt(f[at++]));FIRST.setBoolean(entity,f[at++].equals("1"));field(Entity.class,"inWater").setBoolean(entity,f[at++].equals("1"));WEB.setBoolean(entity,f[at++].equals("1"));entity.onGround=f[at++].equals("1");}
                else if(mode=='P'){world.player=f[1].equals("1")?player:null;player.setPosition(Double.parseDouble(f[2]),Double.parseDouble(f[3]),Double.parseDouble(f[4]));player.spectator=f[5].equals("1");}
                else if(mode=='R')world.rain=f[1].equals("1");
                else if(mode=='T'){int count=Integer.parseInt(f[1]);for(int i=0;i<count;++i){if(!entity.isDead){++entity.ticksExisted;entity.onUpdate();}dump(entity,out);}}
                else if(mode=='I'){int fill=Integer.parseInt(f[at++]),id=Integer.parseInt(f[at++]),count=Integer.parseInt(f[at++]),damage=Integer.parseInt(f[at++]);player=new FixturePlayer(world);player.capabilities.isCreativeMode=f[at++].equals("1");if(fill>=0)for(int i=0;i<36;++i)player.inventory.mainInventory[i]=item(fill,64,0);int slots=Integer.parseInt(f[at++]);for(int i=0;i<slots;++i){int slot=Integer.parseInt(f[at++]),itemId=Integer.parseInt(f[at++]),amount=Integer.parseInt(f[at++]),meta=Integer.parseInt(f[at++]);player.inventory.mainInventory[slot]=item(itemId,amount,meta);}ItemStack incoming=item(id,count,damage);boolean accepted=player.inventory.addItemStackToInventory(incoming);out.print("I "+(accepted?1:0));printItem(out,incoming);for(ItemStack stack:player.inventory.mainInventory)printItem(out,stack);out.println();}
                else if(mode=='X'){player=new FixturePlayer(world);player.experienceLevel=Integer.parseInt(f[1]);player.experienceTotal=Integer.parseInt(f[2]);player.experience=Float.parseFloat(f[3]);player.addExperience(Integer.parseInt(f[4]));out.println("X "+player.experienceLevel+" "+player.experienceTotal+" "+bits(player.experience));}
                else if(mode=='M'){EntityItem[] pair=new EntityItem[2];for(int i=0;i<2;++i){int id=Integer.parseInt(f[at++]),count=Integer.parseInt(f[at++]),damage=Integer.parseInt(f[at++]),age=Integer.parseInt(f[at++]),delay=Integer.parseInt(f[at++]),kind=Integer.parseInt(f[at++]);ItemStack stack=item(id,count,damage);if(kind>0){NBTTagCompound tag=new NBTTagCompound();if(kind>1){if(kind==3)tag.setByte("marker",(byte)1);else tag.setInteger("marker",kind==4?2:1);}stack.setTagCompound(tag);}pair[i]=new EntityItem(world,.5,64,.5,stack);ITEM_AGE.setInt(pair[i],age);ITEM_DELAY.setInt(pair[i],delay);}Method merge=EntityItem.class.getDeclaredMethod("combineItems",EntityItem.class);merge.setAccessible(true);boolean merged=(Boolean)merge.invoke(pair[0],pair[1]);out.print("M "+(merged?1:0));for(EntityItem item:pair){out.print(" "+(item.isDead?1:0)+" "+ITEM_AGE.getInt(item)+" "+ITEM_DELAY.getInt(item));printItem(out,item.getEntityItem());}out.println();}
                else if(mode=='V'){boolean around=f[1].equals("1"),traced=f[2].equals("1");player=new FixturePlayer(world);player.setPosition(1.25,64,-2.5);player.setSneaking(f[3].equals("1"));player.rotationYaw=Float.parseFloat(f[4]);player.rotationPitch=Float.parseFloat(f[5]);player.getRNG().setSeed(Long.parseLong(f[6]));EntityItem thrown=player.dropItem(new ItemStack(Blocks.cobblestone),around,traced);out.print("V "+ITEM_DELAY.getInt(thrown)+" "+(thrown.getThrower()!=null?1:0));for(double v:new double[]{thrown.posX,thrown.posY,thrown.posZ,thrown.motionX,thrown.motionY,thrown.motionZ})out.print(" "+bits(v));out.println(" "+((AtomicLong)SEED.get(player.getRNG())).get());}
            }
        }
    }
}
