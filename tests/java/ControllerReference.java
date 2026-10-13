// Development oracle only. Gameplay methods execute in original compiled MCP
// Block, ItemStack, InventoryPlayer, ContainerPlayer, ContainerChest and Slot.
import java.io.*;
import java.util.*;
import net.minecraft.block.Block;
import net.minecraft.enchantment.Enchantment;
import net.minecraft.init.Bootstrap;
import net.minecraft.inventory.*;
import net.minecraft.item.*;
import net.minecraft.item.crafting.*;
import net.minecraft.entity.passive.EntityHorse;
import net.minecraft.tileentity.*;
import net.minecraft.potion.*;
import net.minecraft.util.BlockPos;
import net.minecraft.util.ChatComponentText;
import net.minecraft.entity.NpcMerchant;
import net.minecraft.village.*;
import net.minecraft.nbt.*;
import java.nio.charset.StandardCharsets;
import java.lang.reflect.Field;

public final class ControllerReference {
    static ItemStack stack(int id,int count,int damage) {
        return id<0||count<=0?null:new ItemStack(Item.getItemById(id),count,damage);
    }
    static void printItem(PrintWriter out,ItemStack stack) {
        if(stack==null||stack.stackSize<=0)out.print(" -1 0 0");
        else out.print(" "+Item.getIdFromItem(stack.getItem())+" "+stack.stackSize+" "+stack.getMetadata());
    }
    static byte[] unhex(String text){if(text.equals("-"))return new byte[0];byte[] result=new byte[text.length()/2];for(int i=0;i<result.length;++i)result[i]=(byte)Integer.parseInt(text.substring(i*2,i*2+2),16);return result;}
    static String hex(byte[] bytes){StringBuilder out=new StringBuilder();for(byte b:bytes)out.append(String.format(Locale.ROOT,"%02x",b&255));return out.toString();}
    static String modified(String text)throws Exception {ByteArrayOutputStream bytes=new ByteArrayOutputStream();new DataOutputStream(bytes).writeUTF(text);return hex(Arrays.copyOfRange(bytes.toByteArray(),2,bytes.size()));}
    static ItemStack fixtureStack(String[] f,int start)throws Exception {ItemStack result=stack(Integer.parseInt(f[start]),Integer.parseInt(f[start+1]),Integer.parseInt(f[start+2]));if(result!=null&&!f[start+3].equals("-"))result.setTagCompound(CompressedStreamTools.read(new DataInputStream(new ByteArrayInputStream(unhex(f[start+3]))),new NBTSizeTracker(2097152L)));return result;}
    static void printTag(PrintWriter out,NBTBase tag)throws Exception {
        int type=tag==null?0:tag.getId();out.print(type+":");
        if(type>=1&&type<=4)out.print(((NBTBase.NBTPrimitive)tag).getLong());
        else if(type==5)out.print(Integer.toUnsignedString(Float.floatToRawIntBits(((NBTBase.NBTPrimitive)tag).getFloat())));
        else if(type==6)out.print(Long.toUnsignedString(Double.doubleToRawLongBits(((NBTBase.NBTPrimitive)tag).getDouble())));
        else if(type==7)out.print(hex(((NBTTagByteArray)tag).getByteArray()));
        else if(type==8)out.print(modified(((NBTTagString)tag).getString()));
        else if(type==9){NBTTagList list=(NBTTagList)tag;out.print(list.getTagType()+"[");for(int i=0;i<list.tagCount();++i){printTag(out,list.get(i));out.print(';');}out.print(']');}
        else if(type==10){NBTTagCompound compound=(NBTTagCompound)tag;out.print('{');for(String key:new TreeSet<String>(compound.getKeySet())){out.print(modified(key)+"=");printTag(out,compound.getTag(key));out.print(';');}out.print('}');}
        else if(type==11){ByteArrayOutputStream bytes=new ByteArrayOutputStream();DataOutputStream data=new DataOutputStream(bytes);for(int i:((NBTTagIntArray)tag).getIntArray())data.writeInt(i);out.print(hex(bytes.toByteArray()));}
    }
    static void printDetailed(PrintWriter out,ItemStack stack)throws Exception {printItem(out,stack);out.print(' ');printTag(out,stack==null||stack.stackSize<=0?null:stack.getTagCompound());}
    static int materialCost(ContainerRepair container)throws Exception{Field field=ContainerRepair.class.getDeclaredField("materialCost");field.setAccessible(true);return field.getInt(container);}
    public static void main(String[] args)throws Exception {
        Bootstrap.register();
        PhysicsReference.FixtureWorld world=new PhysicsReference.FixtureWorld();
        PhysicsReference.FixturePlayer player=new PhysicsReference.FixturePlayer(world);
        world.load(0,0);
        Container container=player.inventoryContainer;
        NpcMerchant merchant=null;MerchantRecipeList offers=null;
        try(BufferedReader input=new BufferedReader(new FileReader(args[0]));PrintWriter out=new PrintWriter(new FileWriter(args[1]))) {
            for(String line;(line=input.readLine())!=null;) {
                if(line.isEmpty()||line.charAt(0)=='#')continue;
                String[] f=line.split("\\s+");
                char command=f[0].charAt(0);
                if(command=='W') {
                    world=new PhysicsReference.FixtureWorld();world.load(0,0);player=new PhysicsReference.FixturePlayer(world);container=player.inventoryContainer;
                    merchant=null;offers=null;
                } else if(command=='H') {
                    int block=Integer.parseInt(f[1]),meta=Integer.parseInt(f[2]),tool=Integer.parseInt(f[3]),eff=Integer.parseInt(f[4]);
                    int haste=Integer.parseInt(f[5]),fatigue=Integer.parseInt(f[6]);
                    boolean water=f[7].equals("1"),ground=f[8].equals("1"),aqua=f[9].equals("1");
                    world=new PhysicsReference.FixtureWorld();world.load(0,0);player=new PhysicsReference.FixturePlayer(world);container=player.inventoryContainer;
                    world.put(2,64,0,(block<<4)|meta);if(water)world.put(0,65,0,9<<4);player.onGround=ground;
                    if(tool>=0){player.inventory.mainInventory[0]=stack(tool,1,0);if(eff>0)player.inventory.mainInventory[0].addEnchantment(Enchantment.efficiency,eff);}
                    if(aqua){player.inventory.armorInventory[3]=stack(310,1,0);player.inventory.armorInventory[3].addEnchantment(Enchantment.aquaAffinity,1);}
                    if(haste>=0)player.addPotionEffect(new PotionEffect(Potion.digSpeed.id,1000,haste));
                    if(fatigue>=0)player.addPotionEffect(new PotionEffect(Potion.digSlowdown.id,1000,fatigue));
                    float hardness=Block.getBlockById(block).getPlayerRelativeBlockHardness(player,world,new BlockPos(2,64,0));
                    out.println("H "+Integer.toUnsignedString(Float.floatToRawIntBits(hardness)));
                } else if(command=='R') {
                    int width=Integer.parseInt(f[1]),height=Integer.parseInt(f[2]);
                    InventoryCrafting matrix=new InventoryCrafting(player.inventoryContainer,width,height);
                    for(int i=0;i<width*height;++i)matrix.setInventorySlotContents(i,stack(Integer.parseInt(f[3+i*3]),Integer.parseInt(f[4+i*3]),Integer.parseInt(f[5+i*3])));
                    out.print("R");printItem(out,CraftingManager.getInstance().findMatchingRecipe(matrix,world));
                    for(ItemStack remain:CraftingManager.getInstance().func_180303_b(matrix,world))printItem(out,remain);out.println();
                } else if(command=='A') {
                    player.capabilities.isCreativeMode=f[10].equals("1");ContainerRepair anvil=new ContainerRepair(player.inventory,world,player);
                    anvil.putStackInSlot(0,fixtureStack(f,1));anvil.putStackInSlot(1,fixtureStack(f,5));anvil.updateItemName(new String(unhex(f[9]),StandardCharsets.UTF_8));
                    out.print("A "+anvil.maximumCost+" "+materialCost(anvil));printDetailed(out,anvil.getSlot(2).getStack());out.println();
                } else if(command=='C') {
                    if(f[1].equals("workbench"))container=new ContainerWorkbench(player.inventory,world,new BlockPos(0,64,0));
                    else if(f[1].equals("furnace"))container=new ContainerFurnace(player.inventory,new TileEntityFurnace());
                    else if(f[1].equals("brewing"))container=new ContainerBrewingStand(player.inventory,new TileEntityBrewingStand());
                    else if(f[1].equals("enchant"))container=new ContainerEnchantment(player.inventory,world);
                    else if(f[1].equals("beacon"))container=new ContainerBeacon(player.inventory,new TileEntityBeacon());
                    else if(f[1].equals("anvil"))container=new ContainerRepair(player.inventory,world,player);
                    else if(f[1].equals("merchant")){merchant=new NpcMerchant(player,new ChatComponentText("fixture"));offers=new MerchantRecipeList();merchant.setRecipes(offers);container=new ContainerMerchant(player.inventory,merchant,world);}
                    else if(f[1].equals("horse")||f[1].equals("donkey")) {
                        boolean chest=f[1].equals("donkey");EntityHorse horse=new EntityHorse(world);horse.setHorseType(chest?1:0);horse.setChested(chest);
                        container=new ContainerHorseInventory(player.inventory,new AnimalChest("fixture",chest?17:2),horse,player);
                    } else {int count=Integer.parseInt(f[1]);container=count==0?player.inventoryContainer:new ContainerChest(player.inventory,new InventoryBasic("fixture",false,count),player);}
                    player.openContainer=container;
                } else if(command=='S') {
                    int slot=Integer.parseInt(f[1]);ItemStack value=stack(Integer.parseInt(f[2]),Integer.parseInt(f[3]),Integer.parseInt(f[4]));
                    if(value!=null&&f.length>5&&!f[5].equals("-"))value.setTagCompound(CompressedStreamTools.read(new DataInputStream(new ByteArrayInputStream(unhex(f[5]))),new NBTSizeTracker(2097152L)));container.putStackInSlot(slot,value);
                } else if(command=='K')player.inventory.setItemStack(stack(Integer.parseInt(f[1]),Integer.parseInt(f[2]),Integer.parseInt(f[3])));
                else if(command=='G')player.capabilities.isCreativeMode=f[1].equals("1");
                else if(command=='X')player.experienceLevel=Integer.parseInt(f[1]);
                else if(command=='N')((ContainerRepair)container).updateItemName(new String(unhex(f[1]),StandardCharsets.UTF_8));
                else if(command=='O'){offers.add(new MerchantRecipe(fixtureStack(f,1),fixtureStack(f,5),fixtureStack(f,9),Integer.parseInt(f[13]),Integer.parseInt(f[14])));((ContainerMerchant)container).getMerchantInventory().resetRecipeAndSlots();}
                else if(command=='J')((ContainerMerchant)container).setCurrentRecipeIndex(Integer.parseInt(f[1]));
                else if(command=='V')container.updateProgressBar(Integer.parseInt(f[1]),Integer.parseInt(f[2]));
                else if(command=='E'){out.print("E "+(((ContainerEnchantment)container).enchantItem(player,Integer.parseInt(f[1]))?1:0)+" "+player.experienceLevel);printDetailed(out,container.getSlot(0).getStack());printDetailed(out,container.getSlot(1).getStack());out.println();}
                else if(command=='P'){
                    out.print("P "+player.experienceLevel);
                    if(container instanceof ContainerRepair)out.print(" "+((ContainerRepair)container).maximumCost+" "+materialCost((ContainerRepair)container));
                    else if(container instanceof ContainerMerchant){out.print(" "+offers.indexOf(((ContainerMerchant)container).getMerchantInventory().getCurrentRecipe())+" [");for(MerchantRecipe offer:offers)out.print(offer.getToolUses()+","+offer.getMaxTradeUses()+","+(offer.isRecipeDisabled()?1:0)+";");out.print(']');}
                    printDetailed(out,player.inventory.getItemStack());for(int i=0;i<Math.min(3,container.inventorySlots.size());++i)printDetailed(out,container.getSlot(i).getStack());out.println();
                }
                else if(command=='I') {
                    ItemStack result=container.slotClick(Integer.parseInt(f[1]),Integer.parseInt(f[2]),Integer.parseInt(f[3]),player);
                    out.print("I");printItem(out,result);printItem(out,player.inventory.getItemStack());
                    for(Object slot:container.inventorySlots)printItem(out,((Slot)slot).getStack());
                    out.println();
                }
            }
        }
    }
}
