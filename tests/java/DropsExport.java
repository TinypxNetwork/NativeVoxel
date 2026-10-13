import java.io.*;
import java.util.*;
import java.lang.reflect.*;
import com.google.gson.*;
import net.minecraft.block.*;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Bootstrap;
import net.minecraft.item.*;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.util.BlockPos;
import net.minecraft.world.World;

/** Static state getters; randomized algorithms remain source-derived native code. */
public final class DropsExport {
    static class CountingRandom extends Random {
        int draws;
        CountingRandom(){super(189L);}
        protected int next(int bits){++draws;return super.next(bits);}
    }
    static String owner(Block block,String name,Class<?>... args)throws Exception {
        Class<?> type=block.getClass();
        while(type!=null){try{return type.getDeclaredMethod(name,args).getDeclaringClass().getSimpleName();}catch(NoSuchMethodException ignored){type=type.getSuperclass();}}
        throw new NoSuchMethodException(name);
    }
    static void allowed(String value,String... choices){for(String choice:choices)if(value.equals(choice))return;throw new IllegalStateException("Unreviewed drop override: "+value);}
    public static void main(String[] args)throws Exception {
        Bootstrap.register();Method silk=Block.class.getDeclaredMethod("canSilkHarvest");silk.setAccessible(true);
        Method stacked=Block.class.getDeclaredMethod("createStackedBlock",IBlockState.class);stacked.setAccessible(true);
        JsonObject root=new JsonObject();JsonArray states=new JsonArray();
        for(int id=0;id<198;++id){Block block=Block.getBlockById(id);String kind=block.getClass().getSimpleName();
            String dropOwner=owner(block,"dropBlockAsItemWithChance",World.class,BlockPos.class,IBlockState.class,float.class,int.class);
            String harvestOwner=owner(block,"harvestBlock",World.class,EntityPlayer.class,BlockPos.class,IBlockState.class,TileEntity.class);
            allowed(dropOwner,"Block","BlockAir","BlockBarrier","BlockBanner","BlockBed","BlockCocoa","BlockCrops","BlockJukebox","BlockLeaves","BlockNetherWart","BlockMobSpawner","BlockOre","BlockPistonMoving","BlockPotato","BlockRedstoneOre","BlockSilverfish","BlockSkull","BlockStem");
            allowed(harvestOwner,"Block","BlockBanner","BlockDeadBush","BlockDoublePlant","BlockIce","BlockNewLeaf","BlockOldLeaf","BlockSnow","BlockTallGrass","BlockVine");
            for(int meta=0;meta<16;++meta){IBlockState state;try{state=block.getStateFromMeta(meta);}catch(IllegalArgumentException exception){continue;}
                JsonObject value=new JsonObject();value.addProperty("state",id*16+meta);value.addProperty("canonical",block.getMetaFromState(state));value.addProperty("kind",kind);
                value.addProperty("drop_owner",dropOwner);value.addProperty("harvest_owner",harvestOwner);
                String quantity="Fixed";
                if(block instanceof BlockOre)quantity="Ore";
                else if(block instanceof BlockRedstoneOre)quantity="Redstone";
                else if(block instanceof BlockGlowstone)quantity="Glowstone";
                else if(block instanceof BlockMelon)quantity="Melon";
                else if(block instanceof BlockSeaLantern)quantity="SeaLantern";
                else if(block instanceof BlockHugeMushroom)quantity="Mushroom";
                else if(block instanceof BlockTallGrass)quantity="TallGrass";
                else if(block instanceof BlockLeaves)quantity="Leaves";
                CountingRandom qr=new CountingRandom();int count=block.quantityDroppedWithBonus(0,qr);
                if(quantity.equals("Fixed")&&qr.draws!=0)throw new IllegalStateException("Random quantity lacks native rule: "+kind);
                value.addProperty("quantity",quantity);value.addProperty("count",quantity.equals("Fixed")?count:0);
                String itemRule="Fixed";if(block instanceof BlockGravel)itemRule="Gravel";else if(block instanceof BlockTallGrass)itemRule="Seeds";else if(block instanceof BlockDoublePlant&&state.getValue(BlockDoublePlant.HALF)==BlockDoublePlant.EnumBlockHalf.LOWER&&state.getValue(BlockDoublePlant.VARIANT)==BlockDoublePlant.EnumPlantType.GRASS)itemRule="Seeds";
                CountingRandom ir=new CountingRandom();Item item=block.getItemDropped(state,ir,0);
                if(itemRule.equals("Fixed")&&ir.draws!=0)throw new IllegalStateException("Random item lacks native rule: "+kind);
                value.addProperty("item_rule",itemRule);value.addProperty("item",item==null?-1:Item.getIdFromItem(item));value.addProperty("damage",block.damageDropped(state));
                boolean canSilk=(Boolean)silk.invoke(block);ItemStack silkStack=canSilk?(ItemStack)stacked.invoke(block,state):null;
                value.addProperty("silk",canSilk);value.addProperty("silk_item",silkStack==null?-1:Item.getIdFromItem(silkStack.getItem()));value.addProperty("silk_count",silkStack==null?0:silkStack.stackSize);value.addProperty("silk_damage",silkStack==null?0:silkStack.getMetadata());
                String behavior="Default";
                if(block instanceof BlockAir||block instanceof BlockBarrier)behavior="None";
                else if(block instanceof BlockLeaves)behavior="Leaves";
                else if(block instanceof BlockCrops)behavior="Crops";
                else if(block instanceof BlockCocoa)behavior="Cocoa";
                else if(block instanceof BlockNetherWart)behavior="Wart";
                else if(block instanceof BlockStem)behavior="Stem";
                else if(block instanceof BlockBed)behavior="Bed";
                else if(block instanceof BlockBanner||block instanceof BlockSkull||block instanceof BlockPistonMoving||block instanceof BlockSilverfish)behavior="Unsupported";
                value.addProperty("behavior",behavior);
                int age=0;if(block instanceof BlockCrops)age=(Integer)state.getValue(BlockCrops.AGE);else if(block instanceof BlockCocoa)age=(Integer)state.getValue(BlockCocoa.AGE);else if(block instanceof BlockNetherWart)age=(Integer)state.getValue(BlockNetherWart.AGE);else if(block instanceof BlockStem)age=(Integer)state.getValue(BlockStem.AGE);else if(block instanceof BlockSnow)age=(Integer)state.getValue(BlockSnow.LAYERS);
                value.addProperty("age",age);states.add(value);
            }
        }
        root.add("states",states);try(Writer out=new OutputStreamWriter(new FileOutputStream(args[0]),"UTF-8")){new GsonBuilder().setPrettyPrinting().create().toJson(root,out);}
    }
}
