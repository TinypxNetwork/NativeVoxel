// Development oracle: real FoodStats, EntityPlayer damage/heal, World weather
// and TileEntityFurnace methods. The fixture only replaces rendering/storage.
import java.io.*;
import java.util.*;
import com.mojang.authlib.GameProfile;
import net.minecraft.block.state.IBlockState;
import net.minecraft.client.multiplayer.ChunkProviderClient;
import net.minecraft.entity.SharedMonsterAttributes;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.inventory.*;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.*;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.profiler.Profiler;
import net.minecraft.tileentity.*;
import net.minecraft.util.*;
import net.minecraft.world.*;
import net.minecraft.world.chunk.*;
import net.minecraft.world.storage.*;
import java.lang.reflect.*;

public final class SimulationReference {
    static final BlockPos FURNACE=new BlockPos(2,64,0);
    static class FixtureWorld extends World {
        final Map<BlockPos,IBlockState> blocks=new HashMap<>();
        final Map<BlockPos,TileEntity> tiles=new HashMap<>();
        final List<EntityItem> capturedDrops=new ArrayList<>();boolean captureDrops;
        FixtureWorld(int dimension) {
            super(new SaveHandlerMP(),new WorldInfo(new WorldSettings(0L,WorldSettings.GameType.SURVIVAL,false,false,WorldType.DEFAULT),"simulation_reference"),WorldProvider.getProviderForDimension(dimension),new Profiler(),false);
            provider.registerWorld(this);chunkProvider=createChunkProvider();((ChunkProviderClient)chunkProvider).loadChunk(0,0);
            blocks.put(new BlockPos(0,63,0),Blocks.stone.getDefaultState());blocks.put(FURNACE,Blocks.furnace.getDefaultState());
        }
        protected IChunkProvider createChunkProvider(){return new ChunkProviderClient(this);}
        protected int getRenderDistanceChunks(){return 8;}
        protected boolean isChunkLoaded(int x,int z,boolean allowEmpty){return x==0&&z==0;}
        public IBlockState getBlockState(BlockPos pos){return blocks.getOrDefault(pos,Blocks.air.getDefaultState());}
        public boolean setBlockState(BlockPos pos,IBlockState state,int flags){blocks.put(pos,state);return true;}
        public TileEntity getTileEntity(BlockPos pos){return tiles.get(pos);}
        public void setTileEntity(BlockPos pos,TileEntity tile){tiles.put(pos,tile);tile.setWorldObj(this);tile.setPos(pos);}
        public void markChunkDirty(BlockPos pos,TileEntity tile){}
        public boolean spawnEntityInWorld(Entity entity){if(!captureDrops)return super.spawnEntityInWorld(entity);if(entity instanceof EntityItem)capturedDrops.add((EntityItem)entity);return true;}
        public void playSound(double x,double y,double z,String sound,float volume,float pitch,boolean distant){}
        void setWeather(float rain,float thunder){rainingStrength=prevRainingStrength=rain;thunderingStrength=prevThunderingStrength=thunder;}
        void weatherTick(boolean daylight){
            // The two time writes are the WorldServer.tick statements. Weather
            // itself executes the actual protected World.updateWeather method.
            worldInfo.setWorldTotalTime(worldInfo.getWorldTotalTime()+1L);
            if(daylight)worldInfo.setWorldTime(worldInfo.getWorldTime()+1L);
            super.updateWeather();
        }
        void printWeather(PrintWriter out){out.println("W "+worldInfo.getWorldTotalTime()+" "+worldInfo.getWorldTime()+" "+worldInfo.getCleanWeatherTime()+" "+worldInfo.getRainTime()+" "+worldInfo.getThunderTime()+" "+(worldInfo.isRaining()?1:0)+" "+(worldInfo.isThundering()?1:0)+" "+bits(prevRainingStrength)+" "+bits(rainingStrength)+" "+bits(prevThunderingStrength)+" "+bits(thunderingStrength));}
    }
    static class FixturePlayer extends EntityPlayer {
        FixturePlayer(FixtureWorld world){super(world,new GameProfile(new UUID(0,189),"NativeSimulation"));setPosition(.5,64,.5);onGround=true;}
        public boolean isSpectator(){return false;}
        public void playSound(String sound,float volume,float pitch){}
    }
    static String bits(float value){return Integer.toUnsignedString(Float.floatToRawIntBits(value));}
    static ItemStack item(String[] f,int at){int id=Integer.parseInt(f[at]),count=Integer.parseInt(f[at+1]);return id<0||count<=0?null:new ItemStack(Item.getItemById(id),count,Integer.parseInt(f[at+2]));}
    static void printItem(PrintWriter out,ItemStack stack){if(stack==null||stack.stackSize<=0)out.print(" -1 0 0");else out.print(" "+Item.getIdFromItem(stack.getItem())+" "+stack.stackSize+" "+stack.getMetadata());}
    public static void main(String[] args)throws Exception {
        Bootstrap.register();FixtureWorld world=new FixtureWorld(0);FixturePlayer player=new FixturePlayer(world);TileEntityFurnace furnace=null;TileEntityBrewingStand brewer=null;TileEntityBrewingStand probe=new TileEntityBrewingStand();char mode='f';boolean daylight=true;
        Method potionResult=TileEntityBrewingStand.class.getDeclaredMethod("getPotionResult",int.class,ItemStack.class);potionResult.setAccessible(true);Method canBrew=TileEntityBrewingStand.class.getDeclaredMethod("canBrew");canBrew.setAccessible(true);Field ingredient=TileEntityBrewingStand.class.getDeclaredField("ingredientID");ingredient.setAccessible(true);
        try(BufferedReader input=new BufferedReader(new FileReader(args[0]));PrintWriter out=new PrintWriter(new FileWriter(args[1]))) {
            for(String line;(line=input.readLine())!=null;){if(line.isEmpty()||line.charAt(0)=='#')continue;String[] f=line.split("\\s+");char command=f[0].charAt(0);
                if(command=='F'){world=new FixtureWorld(0);world.getWorldInfo().setDifficulty(EnumDifficulty.getDifficultyEnum(Integer.parseInt(f[1])));world.getGameRules().setOrCreateGameRule("naturalRegeneration",f[2].equals("1")?"true":"false");player=new FixturePlayer(world);player.capabilities.disableDamage=f[3].equals("1");player.getEntityAttribute(SharedMonsterAttributes.maxHealth).setBaseValue(Double.parseDouble(f[5]));player.setHealth(Float.parseFloat(f[4]));
                    NBTTagCompound stats=new NBTTagCompound();stats.setInteger("foodLevel",Integer.parseInt(f[6]));stats.setFloat("foodSaturationLevel",Float.parseFloat(f[7]));stats.setFloat("foodExhaustionLevel",Float.parseFloat(f[8]));stats.setInteger("foodTickTimer",Integer.parseInt(f[9]));player.getFoodStats().readNBT(stats);mode='f';}
                else if(command=='E')player.getFoodStats().addExhaustion(Float.parseFloat(f[1]));
                else if(command=='W'){world=new FixtureWorld(Integer.parseInt(f[2]));world.rand.setSeed(Long.parseLong(f[1]));daylight=f[3].equals("1");WorldInfo info=world.getWorldInfo();info.setWorldTotalTime(Long.parseLong(f[4]));info.setWorldTime(Long.parseLong(f[5]));info.setCleanWeatherTime(Integer.parseInt(f[6]));info.setRainTime(Integer.parseInt(f[7]));info.setThunderTime(Integer.parseInt(f[8]));info.setRaining(f[9].equals("1"));info.setThundering(f[10].equals("1"));world.setWeather(Float.parseFloat(f[11]),Float.parseFloat(f[12]));mode='w';}
                else if(command=='C'){world=new FixtureWorld(0);furnace=new TileEntityFurnace();world.setTileEntity(FURNACE,furnace);for(int i=0;i<3;++i)furnace.setInventorySlotContents(i,item(f,1+i*3));for(int i=0;i<4;++i)furnace.setField(i,Integer.parseInt(f[10+i]));mode='c';}
                else if(command=='B'){world=new FixtureWorld(0);world.blocks.put(FURNACE,Blocks.brewing_stand.getDefaultState());brewer=new TileEntityBrewingStand();world.setTileEntity(FURNACE,brewer);for(int i=0;i<4;++i)brewer.setInventorySlotContents(i,item(f,1+i*3));brewer.setField(0,Integer.parseInt(f[13]));int id=Integer.parseInt(f[14]);ingredient.set(brewer,id<0?null:Item.getItemById(id));mode='b';}
                else if(command=='S')brewer.setInventorySlotContents(Integer.parseInt(f[1]),item(f,2));
                else if(command=='R'){int meta=Integer.parseInt(f[1]);ItemStack addition=new ItemStack(Item.getItemById(Integer.parseInt(f[2])),1,Integer.parseInt(f[3]));probe.setInventorySlotContents(0,new ItemStack(Items.potionitem,1,meta));probe.setInventorySlotContents(3,addition);out.println("R "+potionResult.invoke(probe,meta,addition)+" "+((Boolean)canBrew.invoke(probe)?1:0));}
                else if(command=='O'){int kind=Integer.parseInt(f[1]);long seed=Long.parseLong(f[2]);world=new FixtureWorld(0);world.captureDrops=true;player=new FixturePlayer(world);player.setPosition(1.25,64,-2.5);player.inventory.setItemStack(item(f,3));Container container;IInventory inputSlots;
                    if(kind==0){ContainerPlayer value=new ContainerPlayer(player.inventory,true,player);container=value;inputSlots=value.craftMatrix;}
                    else if(kind==1){ContainerWorkbench value=new ContainerWorkbench(player.inventory,world,FURNACE);container=value;inputSlots=value.craftMatrix;}
                    else if(kind==2){TileEntityFurnace machine=new TileEntityFurnace();world.setTileEntity(FURNACE,machine);container=new ContainerFurnace(player.inventory,machine);inputSlots=machine;}
                    else{TileEntityBrewingStand machine=new TileEntityBrewingStand();world.setTileEntity(FURNACE,machine);container=new ContainerBrewingStand(player.inventory,machine);inputSlots=machine;}
                    for(int slot=0;slot<inputSlots.getSizeInventory();++slot)inputSlots.setInventorySlotContents(slot,item(f,6+slot*3));player.getRNG().setSeed(seed^0x8e1d189L);container.onContainerClosed(player);out.print("O");printItem(out,player.inventory.getItemStack());out.print(" "+inputSlots.getSizeInventory());for(int slot=0;slot<inputSlots.getSizeInventory();++slot)printItem(out,inputSlots.getStackInSlot(slot));out.print(" "+world.capturedDrops.size());
                    for(EntityItem drop:world.capturedDrops){printItem(out,drop.getEntityItem());out.print(" "+Long.toUnsignedString(Double.doubleToRawLongBits(drop.posY))+" "+Long.toUnsignedString(Double.doubleToRawLongBits(drop.motionX))+" "+Long.toUnsignedString(Double.doubleToRawLongBits(drop.motionY))+" "+Long.toUnsignedString(Double.doubleToRawLongBits(drop.motionZ)));}out.println(" "+Integer.toUnsignedString(player.getRNG().nextInt())+" "+Integer.toUnsignedString(player.getRNG().nextInt()));}
                else if(command=='T'){int count=Integer.parseInt(f[1]);for(int i=0;i<count;++i){
                    if(mode=='f'){if(player.hurtResistantTime>0)--player.hurtResistantTime;if(player.hurtTime>0)--player.hurtTime;player.getFoodStats().onUpdate(player);NBTTagCompound stats=new NBTTagCompound();player.getFoodStats().writeNBT(stats);out.println("F "+bits(player.getHealth())+" "+player.getFoodStats().getFoodLevel()+" "+bits(player.getFoodStats().getSaturationLevel())+" "+bits(stats.getFloat("foodExhaustionLevel"))+" "+stats.getInteger("foodTickTimer")+" "+player.getFoodStats().getPrevFoodLevel());}
                    else if(mode=='w'){world.weatherTick(daylight);world.printWeather(out);}
                    else if(mode=='c'){furnace.update();out.print("C "+furnace.getField(0)+" "+furnace.getField(1)+" "+furnace.getField(2)+" "+furnace.getField(3));for(int slot=0;slot<3;++slot)printItem(out,furnace.getStackInSlot(slot));out.println();}
                    else{brewer.update();int id=ingredient.get(brewer)==null?-1:Item.getIdFromItem((Item)ingredient.get(brewer));boolean[] bottles=brewer.func_174902_m();int mask=0;for(int slot=0;slot<3;++slot)if(bottles[slot])mask|=1<<slot;out.print("B "+brewer.getField(0)+" "+id+" "+mask);for(int slot=0;slot<4;++slot)printItem(out,brewer.getStackInSlot(slot));out.println();}
                }}
            }
        }
    }
}
