import java.io.*;
import java.lang.reflect.Field;
import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.client.multiplayer.ChunkProviderClient;
import net.minecraft.init.Bootstrap;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.BlockPos;
import net.minecraft.world.*;
import net.minecraft.world.biome.BiomeGenBase;
import net.minecraft.world.chunk.*;
import net.minecraft.world.gen.ChunkProviderGenerate;
import net.minecraft.world.gen.ChunkProviderHell;
import net.minecraft.world.gen.ChunkProviderEnd;
import net.minecraft.world.storage.*;

public final class WorldgenChunkReference {
    static final class ReferenceWorld extends World {
        ReferenceWorld(long seed,WorldType type){this(seed,type,0);}
        ReferenceWorld(long seed,WorldType type,int dimension){
            super(new SaveHandlerMP(),new WorldInfo(new WorldSettings(seed,WorldSettings.GameType.SURVIVAL,false,false,type),"generation_reference"),WorldProvider.getProviderForDimension(dimension),new Profiler(),true);
            provider.registerWorld(this);chunkProvider=createChunkProvider();
        }
        protected IChunkProvider createChunkProvider(){return new ChunkProviderClient(this);}
        protected int getRenderDistanceChunks(){return 8;}
        // A target chunk is not installed into the server provider until its
        // generation completes. Use the original biome-manager fallback instead
        // of accidentally caching all coordinates in ChunkProviderClient's blank.
        protected boolean isChunkLoaded(int x,int z,boolean allowEmpty){return false;}
    }
    public static void main(String[] args)throws Exception{
        Bootstrap.register();ReferenceWorld world=null;IChunkProvider generator=null;long previous=0;int previousType=-1,previousStage=-1,previousDimension=99;
        Field randomField=null;
        try(BufferedReader input=new BufferedReader(new FileReader(args[0]));DataOutputStream out=new DataOutputStream(new BufferedOutputStream(new FileOutputStream(args[1])))){
            for(String line;(line=input.readLine())!=null;){if(line.isEmpty()||line.startsWith("#"))continue;String[] f=line.split("\\s+");long seed=Long.parseLong(f[0]);int type=Integer.parseInt(f[1]),stage=Integer.parseInt(f[2]),x=Integer.parseInt(f[3]),z=Integer.parseInt(f[4]),dimension=f.length>5?Integer.parseInt(f[5]):0;
                if(generator==null||seed!=previous||type!=previousType||stage!=previousStage||dimension!=previousDimension){world=new ReferenceWorld(seed,type==1?WorldType.LARGE_BIOMES:type==2?WorldType.AMPLIFIED:WorldType.DEFAULT,dimension);
                    generator=dimension==-1?new ChunkProviderHell(world,false,seed):dimension==1?new ChunkProviderEnd(world,seed):new ChunkProviderGenerate(world,seed,false,"");
                    randomField=generator.getClass().getDeclaredField(dimension==-1?"hellRNG":dimension==1?"endRNG":"rand");randomField.setAccessible(true);
                    previous=seed;previousType=type;previousStage=stage;previousDimension=dimension;}
                ((Random)randomField.get(generator)).setSeed((long)x*341873128712L+(long)z*132897987541L);
                Chunk chunk;
                if(stage>=2)chunk=generator.provideChunk(x,z);else{
                    ChunkPrimer primer=new ChunkPrimer();
                    if(dimension==-1)((ChunkProviderHell)generator).func_180515_a(x,z,primer);else if(dimension==1)((ChunkProviderEnd)generator).func_180520_a(x,z,primer);else ((ChunkProviderGenerate)generator).setBlocksInChunk(x,z,primer);
                    BiomeGenBase[] biomes=world.getWorldChunkManager().loadBlockGeneratorData(null,x*16,z*16,16,16);
                    if(stage>=1){if(dimension==-1)((ChunkProviderHell)generator).func_180516_b(x,z,primer);else if(dimension==1)((ChunkProviderEnd)generator).func_180519_a(primer);else ((ChunkProviderGenerate)generator).replaceBlocksForBiome(x,z,primer,biomes);}
                    chunk=new Chunk(world,primer,x,z);for(int i=0;i<256;++i)chunk.getBiomeArray()[i]=(byte)biomes[i].biomeID;if(dimension==-1)chunk.resetRelightChecks();else chunk.generateSkylightMap();}
                for(int bx=0;bx<16;++bx)for(int bz=0;bz<16;++bz)for(int y=0;y<256;++y){IBlockState state=chunk.getBlockState(new BlockPos(bx,y,bz));out.writeShort((Block.getIdFromBlock(state.getBlock())<<4)|state.getBlock().getMetaFromState(state));}
                for(int bx=0;bx<16;++bx)for(int bz=0;bz<16;++bz)for(int y=0;y<256;++y){BlockPos p=new BlockPos(bx,y,bz);out.writeByte(((dimension==0?chunk.getLightFor(EnumSkyBlock.SKY,p):0)<<4)|chunk.getLightFor(EnumSkyBlock.BLOCK,p));}
                out.write(chunk.getBiomeArray());
            }
        }
    }
}
