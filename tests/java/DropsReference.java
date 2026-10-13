import java.io.*;
import java.util.*;
import com.mojang.authlib.GameProfile;
import net.minecraft.block.*;
import net.minecraft.block.state.IBlockState;
import net.minecraft.client.multiplayer.ChunkProviderClient;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.*;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.*;
import net.minecraft.item.*;
import net.minecraft.nbt.*;
import net.minecraft.profiler.Profiler;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.util.*;
import net.minecraft.world.*;
import net.minecraft.world.chunk.*;
import net.minecraft.world.storage.*;

/** Actual block/item methods, with server removal ordering in a headless world.
 * Neighbor updates, tile inventories and entity motion are deliberately outside
 * this fixture; spawned stack, position, XP and RNG continuation are captured.
 */
public final class DropsReference {
    static final BlockPos POS=new BlockPos(2,64,0);
    static class FixtureWorld extends World {
        final Map<BlockPos,IBlockState> blocks=new HashMap<>();
        final List<EntityItem> items=new ArrayList<>();
        final List<Integer> experience=new ArrayList<>();
        FixtureWorld(int dimension){
            super(new SaveHandlerMP(),new WorldInfo(new WorldSettings(0L,WorldSettings.GameType.SURVIVAL,false,false,WorldType.DEFAULT),"drops_reference"),WorldProvider.getProviderForDimension(dimension),new Profiler(),false);
            provider.registerWorld(this);chunkProvider=createChunkProvider();((ChunkProviderClient)chunkProvider).loadChunk(0,0);
        }
        protected IChunkProvider createChunkProvider(){return new ChunkProviderClient(this);}
        protected int getRenderDistanceChunks(){return 8;}
        protected boolean isChunkLoaded(int x,int z,boolean allowEmpty){return x==0&&z==0;}
        public IBlockState getBlockState(BlockPos pos){return blocks.getOrDefault(pos,Blocks.air.getDefaultState());}
        public boolean setBlockState(BlockPos pos,IBlockState state,int flags){blocks.put(pos,state);return true;}
        public TileEntity getTileEntity(BlockPos pos){return null;}
        public void markChunkDirty(BlockPos pos,TileEntity tile){}
        public boolean spawnEntityInWorld(Entity entity){if(entity instanceof EntityItem)items.add((EntityItem)entity);else if(entity instanceof EntityXPOrb)experience.add(((EntityXPOrb)entity).getXpValue());else throw new IllegalStateException("Unexpected entity "+entity.getClass());return true;}
        public void playSound(double x,double y,double z,String sound,float volume,float pitch,boolean distant){}
    }
    static class FixturePlayer extends EntityPlayer {
        FixturePlayer(FixtureWorld world){super(world,new GameProfile(new UUID(0,189),"NativeDrops"));setPosition(.5,64,.5);}
        public boolean isSpectator(){return false;}
        public void playSound(String name,float volume,float pitch){}
        void reset(ItemStack held,long seed,boolean creative){inventory.mainInventory=new ItemStack[36];inventory.currentItem=0;inventory.mainInventory[0]=held;foodStats=new FoodStats();rand.setSeed(seed);capabilities.isCreativeMode=creative;capabilities.disableDamage=creative;}
    }
    static String bits(float value){return Integer.toUnsignedString(Float.floatToRawIntBits(value));}
    static String bits(double value){return Long.toUnsignedString(Double.doubleToRawLongBits(value));}
    static void printItem(PrintWriter out,ItemStack item){if(item==null||item.stackSize<=0)out.print(" -1 0 0");else out.print(" "+Item.getIdFromItem(item.getItem())+" "+item.stackSize+" "+item.getMetadata());}
    static void enchant(NBTTagList list,int id,int level){if(level==0)return;NBTTagCompound value=new NBTTagCompound();value.setShort("id",(short)id);value.setShort("lvl",(short)level);list.appendTag(value);}
    public static void main(String[] args)throws Exception {
        Bootstrap.register();FixtureWorld[] worlds={new FixtureWorld(0),new FixtureWorld(-1)};FixturePlayer[] players={new FixturePlayer(worlds[0]),new FixturePlayer(worlds[1])};
        try(BufferedReader input=new BufferedReader(new FileReader(args[0]));PrintWriter out=new PrintWriter(new FileWriter(args[1]))){
            for(String line;(line=input.readLine())!=null;){if(line.isEmpty()||line.charAt(0)=='#')continue;String[] f=line.split("\\s+");int at=0;char mode=f[at++].charAt(0);int encoded=Integer.parseInt(f[at++]);int itemId=Integer.parseInt(f[at++]),count=Integer.parseInt(f[at++]),damage=Integer.parseInt(f[at++]);int silk=Integer.parseInt(f[at++]),fortune=Integer.parseInt(f[at++]),unbreaking=Integer.parseInt(f[at++]),immutable=Integer.parseInt(f[at++]);long worldSeed=Long.parseLong(f[at++]),playerSeed=Long.parseLong(f[at++]);boolean tileDrops=f[at++].equals("1"),nether=f[at++].equals("1"),below=f[at++].equals("1"),creative=f[at++].equals("1");float chance=Float.parseFloat(f[at++]);
                FixtureWorld world=worlds[nether?1:0];FixturePlayer player=players[nether?1:0];Block block=Block.getBlockById(encoded>>4);IBlockState state=block.getStateFromMeta(encoded&15);world.items.clear();world.experience.clear();world.blocks.clear();world.blocks.put(POS,Blocks.air.getDefaultState());if(below)world.blocks.put(POS.down(),Blocks.stone.getDefaultState());world.getGameRules().setOrCreateGameRule("doTileDrops",tileDrops?"true":"false");
                ItemStack held=itemId<0?null:new ItemStack(Item.getItemById(itemId),count,damage);if(held!=null&&(silk!=0||fortune!=0||unbreaking!=0||immutable!=0)){NBTTagCompound tag=new NBTTagCompound();NBTTagList ench=new NBTTagList();enchant(ench,33,silk);enchant(ench,35,fortune);enchant(ench,34,unbreaking);tag.setTag("ench",ench);if(immutable!=0)tag.setBoolean("Unbreakable",true);held.setTagCompound(tag);}
                player.reset(held,playerSeed,creative);world.rand.setSeed(worldSeed);boolean eligible=player.canHarvestBlock(block);
                if(mode=='D')block.dropBlockAsItemWithChance(world,POS,state,chance,fortune);
                else if(mode=='H')block.harvestBlock(world,player,POS,state,null);
                else if(mode=='B'&&!creative){if(held!=null){held.onBlockDestroyed(world,block,POS,player);if(held.stackSize==0)player.destroyCurrentEquippedItem();}if(eligible)block.harvestBlock(world,player,POS,state,null);}
                else if(mode!='B')throw new IllegalArgumentException("Unknown fixture mode");
                NBTTagCompound food=new NBTTagCompound();player.getFoodStats().writeNBT(food);IBlockState after=world.getBlockState(POS);int replacement=(Block.getIdFromBlock(after.getBlock())<<4)|after.getBlock().getMetaFromState(after);
                out.print("R "+(eligible?1:0));printItem(out,player.getHeldItem());out.print(" "+bits(food.getFloat("foodExhaustionLevel"))+" "+replacement+" "+world.items.size());
                for(EntityItem entity:world.items){printItem(out,entity.getEntityItem());out.print(" "+bits(entity.posX-POS.getX())+" "+bits(entity.posY-POS.getY())+" "+bits(entity.posZ-POS.getZ()));}
                out.print(" "+world.experience.size());for(int amount:world.experience)out.print(" "+amount);
                out.println(" "+Integer.toUnsignedString(world.rand.nextInt())+" "+Integer.toUnsignedString(world.rand.nextInt())+" "+Integer.toUnsignedString(player.getRNG().nextInt())+" "+Integer.toUnsignedString(player.getRNG().nextInt()));
            }
        }
    }
}
