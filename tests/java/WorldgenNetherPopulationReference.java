import java.io.*;
import java.lang.reflect.Field;
import java.util.*;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.init.Bootstrap;
import net.minecraft.util.BlockPos;
import net.minecraft.world.gen.ChunkProviderHell;
import net.minecraft.world.storage.MapStorage;
import net.minecraft.world.storage.SaveHandlerMP;

public final class WorldgenNetherPopulationReference {
    static Field seedField;
    static final class ReferenceWorld extends WorldgenFeatureReference.ReferenceWorld {
        final List<String> ticks=new ArrayList<>();
        ReferenceWorld(int pattern,int light){super(pattern,63,-1,light);mapStorage=new MapStorage(new SaveHandlerMP());}
        public void forceBlockUpdateTick(Block block,BlockPos p,Random random){try{ticks.add("F "+p.getX()+" "+p.getY()+" "+p.getZ()+" "+(Block.getIdFromBlock(block)<<4)+" "+((AtomicLong)seedField.get(random)).get());}catch(Exception e){throw new RuntimeException(e);}}
    }
    public static void main(String[] args)throws Exception{
        Bootstrap.register();seedField=Random.class.getDeclaredField("seed");seedField.setAccessible(true);Field providerRandom=ChunkProviderHell.class.getDeclaredField("hellRNG");providerRandom.setAccessible(true);
        try(BufferedReader input=new BufferedReader(new FileReader(args[0]));PrintWriter out=new PrintWriter(new BufferedWriter(new FileWriter(args[1])))){
            for(String line;(line=input.readLine())!=null;){
                if(line.isEmpty()||line.startsWith("#"))continue;String[] f=line.split("\\s+");long seed=Long.parseLong(f[0]);int pattern=Integer.parseInt(f[1]),cx=Integer.parseInt(f[2]),cz=Integer.parseInt(f[3]),light=Integer.parseInt(f[4]);
                ReferenceWorld world=new ReferenceWorld(pattern,light);ChunkProviderHell generator=new ChunkProviderHell(world,false,0);Random random=(Random)providerRandom.get(generator);random.setSeed(seed);generator.populate(generator,cx,cz);
                out.println("R "+random.nextLong());for(String tick:world.ticks)out.println(tick);
                for(Map.Entry<BlockPos,IBlockState> entry:world.changes.entrySet()){BlockPos p=entry.getKey();out.println("B "+p.getX()+" "+p.getY()+" "+p.getZ()+" "+WorldgenFeatureReference.packed(entry.getValue()));}out.println("END");
            }
        }
    }
}
