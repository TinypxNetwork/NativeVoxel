import java.io.*;
import java.util.*;
import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.client.multiplayer.ChunkProviderClient;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityList;
import net.minecraft.init.Bootstrap;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.BlockPos;
import net.minecraft.world.*;
import net.minecraft.world.biome.*;
import net.minecraft.world.chunk.*;
import net.minecraft.world.gen.ChunkProviderGenerate;
import net.minecraft.world.gen.ChunkProviderEnd;
import net.minecraft.world.storage.*;

public final class WorldgenPopulationReference {
    static final class ReferenceWorld extends World {
        final Map<Long,Chunk> chunks=new HashMap<>();
        final List<Entity> spawns=new ArrayList<>();
        IChunkProvider generator;
        ReferenceWorld(long seed,WorldType type,int dimension){
            super(new SaveHandlerMP(),new WorldInfo(new WorldSettings(seed,WorldSettings.GameType.SURVIVAL,false,false,type),"population_reference"),WorldProvider.getProviderForDimension(dimension),new Profiler(),true);
            provider.registerWorld(this);chunkProvider=createChunkProvider();generator=dimension==1?new ChunkProviderEnd(this,seed):new ChunkProviderGenerate(this,seed,false,"");
        }
        protected IChunkProvider createChunkProvider(){return new ChunkProviderClient(this);}
        protected int getRenderDistanceChunks(){return 8;}
        protected boolean isChunkLoaded(int x,int z,boolean allowEmpty){return chunks!=null&&chunks.containsKey((long)x&0xffffffffL|((long)z<<32));}
        public Chunk getChunkFromChunkCoords(int x,int z){
            long key=(long)x&0xffffffffL|((long)z<<32);Chunk chunk=chunks.get(key);
            if(chunk==null){chunk=generator.provideChunk(x,z);chunks.put(key,chunk);}return chunk;
        }
        public boolean spawnEntityInWorld(Entity entity){spawns.add(entity);return true;}
    }
    static final class OreDecorator extends BiomeDecorator {
        protected void genDecorations(BiomeGenBase biome){generateOres();}
    }
    public static void main(String[] args)throws Exception{
        Bootstrap.register();
        try(BufferedReader input=new BufferedReader(new FileReader(args[0]));DataOutputStream out=new DataOutputStream(new BufferedOutputStream(new FileOutputStream(args[1])))){
            for(String line;(line=input.readLine())!=null;){
                if(line.isEmpty()||line.startsWith("#"))continue;String[] f=line.split("\\s+");long seed=Long.parseLong(f[0]),randomSeed=Long.parseLong(f[5]);int type=Integer.parseInt(f[1]),dimension=Integer.parseInt(f[2]),x=Integer.parseInt(f[3]),z=Integer.parseInt(f[4]);
                ReferenceWorld world=new ReferenceWorld(seed,type==1?WorldType.LARGE_BIOMES:type==2?WorldType.AMPLIFIED:WorldType.DEFAULT,dimension);
                for(int cx=x;cx<=x+1;++cx)for(int cz=z;cz<=z+1;++cz)world.getChunkFromChunkCoords(cx,cz);
                world.rand.setSeed(randomSeed);
                if(dimension==1)world.generator.populate(world.generator,x,z);else new OreDecorator().decorate(world,world.rand,BiomeGenBase.plains,new BlockPos(x*16,0,z*16));
                if(world.chunks.size()!=4)throw new IllegalStateException("population unexpectedly touched outside supplied columns");
                for(int cx=x;cx<=x+1;++cx)for(int cz=z;cz<=z+1;++cz)for(int bx=0;bx<16;++bx)for(int bz=0;bz<16;++bz)for(int y=0;y<256;++y){IBlockState state=world.getBlockState(new BlockPos(cx*16+bx,y,cz*16+bz));out.writeShort((Block.getIdFromBlock(state.getBlock())<<4)|state.getBlock().getMetaFromState(state));}
                out.writeInt(world.spawns.size());for(Entity entity:world.spawns){out.writeUTF(EntityList.getEntityString(entity));out.writeDouble(entity.posX);out.writeDouble(entity.posY);out.writeDouble(entity.posZ);out.writeFloat(entity.rotationYaw);out.writeFloat(entity.rotationPitch);}
                out.writeLong(world.rand.nextLong());
            }
        }
    }
}
