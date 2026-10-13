import java.io.*;
import java.lang.reflect.*;
import java.util.*;
import net.minecraft.enchantment.EnchantmentHelper;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Items;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.*;
import net.minecraft.tileentity.*;
import net.minecraft.util.*;
import net.minecraft.world.gen.feature.WorldGenDungeons;

public final class WorldgenLootReference {
    static String hex(byte[] bytes){StringBuilder out=new StringBuilder();for(byte b:bytes)out.append(String.format(Locale.ROOT,"%02x",b&255));return out.toString();}
    static String modified(String text)throws Exception{ByteArrayOutputStream bytes=new ByteArrayOutputStream();new DataOutputStream(bytes).writeUTF(text);return hex(Arrays.copyOfRange(bytes.toByteArray(),2,bytes.size()));}
    static void printTag(PrintWriter out,NBTBase tag)throws Exception{
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
    @SuppressWarnings("unchecked")
    public static void main(String[] args)throws Exception{
        Bootstrap.register();Field contentField=WorldGenDungeons.class.getDeclaredField("CHESTCONTENT");contentField.setAccessible(true);List<WeightedRandomChestContent> content=(List<WeightedRandomChestContent>)contentField.get(null);
        Method pick=WorldGenDungeons.class.getDeclaredMethod("pickMobSpawner",Random.class);pick.setAccessible(true);WorldGenDungeons dungeon=new WorldGenDungeons();
        try(BufferedReader input=new BufferedReader(new FileReader(args[0]));PrintWriter out=new PrintWriter(new BufferedWriter(new FileWriter(args[1])))){
            for(String line;(line=input.readLine())!=null;){
                if(line.isEmpty()||line.startsWith("#"))continue;String[] f=line.split("\\s+");Random random=new Random(Long.parseLong(f[1]));NBTTagCompound result=new NBTTagCompound();
                if(f[0].equals("B")){ItemStack book=EnchantmentHelper.addRandomEnchantment(random,new ItemStack(Items.book,1,0),Integer.parseInt(f[2]));book.writeToNBT(result);}
                else{
                    BlockPos position=new BlockPos(Integer.parseInt(f[2]),Integer.parseInt(f[3]),Integer.parseInt(f[4]));
                    if(f[0].equals("C")){TileEntityChest chest=new TileEntityChest();chest.setPos(position);List<WeightedRandomChestContent> list=WeightedRandomChestContent.func_177629_a(content,new WeightedRandomChestContent[]{Items.enchanted_book.getRandom(random)});WeightedRandomChestContent.generateChestContents(random,list,chest,8);chest.writeToNBT(result);}
                    else{TileEntityMobSpawner spawner=new TileEntityMobSpawner();spawner.setPos(position);spawner.getSpawnerBaseLogic().setEntityName((String)pick.invoke(dungeon,random));spawner.writeToNBT(result);}
                }
                printTag(out,result);out.println(" "+random.nextLong());
            }
        }
    }
}
