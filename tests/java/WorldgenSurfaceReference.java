import java.io.*;
import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Blocks;
import net.minecraft.util.BlockPos;
import net.minecraft.world.WorldType;
import net.minecraft.world.biome.BiomeGenBase;
import net.minecraft.world.chunk.ChunkPrimer;

public final class WorldgenSurfaceReference {
    public static void main(String[] args)throws Exception{
        Bootstrap.register();WorldgenChunkReference.ReferenceWorld world=null;long previous=0;
        try(BufferedReader input=new BufferedReader(new FileReader(args[0]));DataOutputStream out=new DataOutputStream(new BufferedOutputStream(new FileOutputStream(args[1])))){
            for(String line;(line=input.readLine())!=null;){if(line.isEmpty()||line.startsWith("#"))continue;String[] f=line.split("\\s+");long seed=Long.parseLong(f[0]),randomSeed=Long.parseLong(f[1]);int biome=Integer.parseInt(f[2]),x=Integer.parseInt(f[3]),z=Integer.parseInt(f[4]),height=Integer.parseInt(f[5]);double noise=Double.parseDouble(f[6]);
                if(world==null||previous!=seed){world=new WorldgenChunkReference.ReferenceWorld(seed,WorldType.DEFAULT);previous=seed;}
                ChunkPrimer primer=new ChunkPrimer();int bx=z&15,bz=x&15;for(int y=0;y<=height;++y)primer.setBlockState(bx,y,bz,Blocks.stone.getDefaultState());for(int y=height+1;y<63;++y)primer.setBlockState(bx,y,bz,Blocks.water.getDefaultState());
                Random random=new Random(randomSeed);BiomeGenBase b=BiomeGenBase.getBiome(biome);b.genTerrainBlocks(world,random,primer,x,z,noise);
                for(int y=0;y<256;++y){IBlockState state=primer.getBlockState(bx,y,bz);out.writeShort((Block.getIdFromBlock(state.getBlock())<<4)|state.getBlock().getMetaFromState(state));}
                out.writeLong(random.nextLong());out.writeInt(Float.floatToRawIntBits(b.getFloatTemperature(new BlockPos(x,height,z))));
            }
        }
    }
}
