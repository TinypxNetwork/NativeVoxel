import java.io.*;
import java.util.*;
import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.init.Bootstrap;
import net.minecraft.util.BlockPos;
import net.minecraft.world.EnumSkyBlock;
import net.minecraft.world.biome.BiomeGenBase;
import net.minecraft.world.gen.feature.WorldGenLakes;

public final class WorldgenLakeReference {
    static final class ReferenceWorld extends WorldgenFeatureReference.ReferenceWorld {
        int biome,sky,skyQueries,freezeQueries;
        ReferenceWorld(int pattern,int height,int biome,int sky){super(pattern,height,0,13);this.biome=biome;this.sky=sky;}
        public BiomeGenBase getBiomeGenForCoords(BlockPos p){return BiomeGenBase.getBiome(biome);}
        public int getLightFor(EnumSkyBlock type,BlockPos p){if(type==EnumSkyBlock.BLOCK)return 0;++skyQueries;return p.getY()>=256?15:sky;}
        public boolean canBlockFreezeWater(BlockPos p){++freezeQueries;return super.canBlockFreezeWater(p);}
    }
    public static void main(String[] args)throws Exception{
        Bootstrap.register();
        try(BufferedReader input=new BufferedReader(new FileReader(args[0]));PrintWriter out=new PrintWriter(new BufferedWriter(new FileWriter(args[1])))){
            for(String line;(line=input.readLine())!=null;){
                if(line.isEmpty()||line.startsWith("#"))continue;String[] f=line.split("\\s+");long seed=Long.parseLong(f[0]);int pattern=Integer.parseInt(f[1]),ground=Integer.parseInt(f[2]),x=Integer.parseInt(f[3]),y=Integer.parseInt(f[4]),z=Integer.parseInt(f[5]),liquid=Integer.parseInt(f[6]),biome=Integer.parseInt(f[7]),sky=Integer.parseInt(f[8]);
                ReferenceWorld world=new ReferenceWorld(pattern,ground,biome,sky);Random random=new Random(seed);boolean result=new WorldGenLakes(Block.getBlockById(liquid)).generate(world,random,new BlockPos(x,y,z));
                out.println("R "+(result?1:0)+" "+random.nextLong()+" "+world.skyQueries+" "+world.freezeQueries);
                for(Map.Entry<BlockPos,IBlockState> entry:world.changes.entrySet()){BlockPos p=entry.getKey();out.println("B "+p.getX()+" "+p.getY()+" "+p.getZ()+" "+WorldgenFeatureReference.packed(entry.getValue()));}out.println("END");
            }
        }
    }
}
