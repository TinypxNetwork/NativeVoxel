import java.io.*;
import java.lang.reflect.*;
import com.google.gson.*;
import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.init.Bootstrap;
import net.minecraft.world.biome.*;

public final class WorldgenBiomeExport {
    static int state(IBlockState value){return (Block.getIdFromBlock(value.getBlock())<<4)|value.getBlock().getMetaFromState(value);}
    public static void main(String[] args)throws Exception{
        Bootstrap.register();JsonObject root=new JsonObject();JsonArray rows=new JsonArray();JsonObject ids=new JsonObject();
        for(Field f:BiomeGenBase.class.getFields())if(Modifier.isStatic(f.getModifiers())&&f.getType()==BiomeGenBase.class){BiomeGenBase b=(BiomeGenBase)f.get(null);if(b!=null)ids.addProperty(f.getName(),b.biomeID);}
        for(BiomeGenBase b:BiomeGenBase.getBiomeGenArray())if(b!=null){
            JsonObject r=new JsonObject();r.addProperty("id",b.biomeID);r.addProperty("name",b.biomeName);
            r.addProperty("class",b.getBiomeClass().getName());r.addProperty("actualClass",b.getClass().getName());r.addProperty("category",b.getTempCategory().name());
            r.addProperty("snow",b.isSnowyBiome());r.addProperty("mesa",b instanceof BiomeGenMesa);
            r.addProperty("height",b.minHeight);r.addProperty("variation",b.maxHeight);r.addProperty("temperature",b.temperature);r.addProperty("rainfall",b.rainfall);
            r.addProperty("top",state(b.topBlock));r.addProperty("filler",state(b.fillerBlock));
            r.addProperty("surfaceClass",b.getClass().getMethod("genTerrainBlocks",net.minecraft.world.World.class,java.util.Random.class,net.minecraft.world.chunk.ChunkPrimer.class,int.class,int.class,double.class).getDeclaringClass().getName());
            r.addProperty("decorateClass",b.getClass().getMethod("decorate",net.minecraft.world.World.class,java.util.Random.class,net.minecraft.util.BlockPos.class).getDeclaringClass().getName());
            r.addProperty("treeClass",b.getClass().getMethod("genBigTreeChance",java.util.Random.class).getDeclaringClass().getName());
            r.addProperty("flowerClass",b.getClass().getMethod("pickRandomFlower",java.util.Random.class,net.minecraft.util.BlockPos.class).getDeclaringClass().getName());
            r.addProperty("grassClass",b.getClass().getMethod("getRandomWorldGenForGrass",java.util.Random.class).getDeclaringClass().getName());
            Field decoratorField=BiomeGenBase.class.getDeclaredField("theBiomeDecorator");decoratorField.setAccessible(true);Object decorator=decoratorField.get(b);JsonObject decoration=new JsonObject();
            for(String name:new String[]{"waterlilyPerChunk","treesPerChunk","flowersPerChunk","grassPerChunk","deadBushPerChunk","mushroomsPerChunk","reedsPerChunk","cactiPerChunk","sandPerChunk","sandPerChunk2","clayPerChunk","bigMushroomsPerChunk","generateLakes"}){
                Field field=BiomeDecorator.class.getDeclaredField(name);field.setAccessible(true);Object value=field.get(decorator);if(value instanceof Boolean)decoration.addProperty(name,(Boolean)value);else decoration.addProperty(name,(Number)value);
            }
            r.add("decoration",decoration);
            int delegate=-1;JsonObject fields=new JsonObject();
            for(Class<?> c=b.getClass();c!=BiomeGenBase.class;c=c.getSuperclass())for(Field f:c.getDeclaredFields()){
                if(Modifier.isStatic(f.getModifiers()))continue;f.setAccessible(true);Object value=f.get(b);
                if(f.getName().equals("baseBiome")&&value instanceof BiomeGenBase)delegate=((BiomeGenBase)value).biomeID;
                if(value instanceof Number)fields.addProperty(f.getName(),(Number)value);else if(value instanceof Boolean)fields.addProperty(f.getName(),(Boolean)value);
            }
            r.addProperty("delegate",delegate);r.add("fields",fields);rows.add(r);
        }
        root.add("ids",ids);root.add("biomes",rows);try(Writer out=new FileWriter(args[0])){new GsonBuilder().setPrettyPrinting().create().toJson(root,out);}
    }
}
