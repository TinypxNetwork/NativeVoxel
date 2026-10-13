import java.io.*;
import java.util.*;
import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.client.multiplayer.ChunkProviderClient;
import net.minecraft.entity.Entity;
import net.minecraft.init.Bootstrap;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.profiler.Profiler;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.util.BlockPos;
import net.minecraft.world.*;
import net.minecraft.world.chunk.*;
import net.minecraft.world.gen.feature.WorldGenDungeons;
import net.minecraft.world.storage.*;

public final class WorldgenDungeonReference {
    static final Comparator<BlockPos> ORDER=(a,b)->{int c=Integer.compare(a.getX(),b.getX());if(c==0)c=Integer.compare(a.getZ(),b.getZ());if(c==0)c=Integer.compare(a.getY(),b.getY());return c;};
    static final class ReferenceWorld extends World {
        final Map<Long,Chunk> chunks=new HashMap<>();int pattern,height;
        ReferenceWorld(int pattern,int height){
            super(new SaveHandlerMP(),new WorldInfo(new WorldSettings(0,WorldSettings.GameType.SURVIVAL,false,false,WorldType.DEFAULT),"dungeon_reference"),WorldProvider.getProviderForDimension(0),new Profiler(),false);
            this.pattern=pattern;this.height=height;provider.registerWorld(this);chunkProvider=createChunkProvider();
            for(int x=-1;x<=0;++x)for(int z=-1;z<=0;++z)getChunkFromChunkCoords(x,z);
            if(pattern==26)getTileEntity(new BlockPos(0,height,-1));
        }
        protected IChunkProvider createChunkProvider(){return new ChunkProviderClient(this);}
        protected int getRenderDistanceChunks(){return 8;}
        protected boolean isChunkLoaded(int x,int z,boolean allowEmpty){return chunks!=null&&chunks.containsKey((long)x&0xffffffffL|((long)z<<32));}
        public Chunk getChunkFromChunkCoords(int x,int z){
            long key=(long)x&0xffffffffL|((long)z<<32);Chunk chunk=chunks.get(key);
            if(chunk==null){ChunkPrimer primer=new ChunkPrimer();for(int bx=0;bx<16;++bx)for(int bz=0;bz<16;++bz)for(int y=0;y<256;++y){int value=WorldgenFeatureReference.base(pattern,height,x*16+bx,y,z*16+bz);primer.setBlockState(bx,y,bz,Block.getBlockById(value>>4).getStateFromMeta(value&15));}
                chunk=new Chunk(this,primer,x,z);chunks.put(key,chunk);chunk.generateSkylightMap();chunk.onChunkLoad();}
            return chunk;
        }
        // This fixture verifies generated block/tile data. Entity drops caused
        // by overwriting pre-existing test chests belong to the runtime kernel.
        public boolean spawnEntityInWorld(Entity entity){return true;}
    }
    public static void main(String[] args)throws Exception{
        Bootstrap.register();
        try(BufferedReader input=new BufferedReader(new FileReader(args[0]));PrintWriter out=new PrintWriter(new BufferedWriter(new FileWriter(args[1])))){
            for(String line;(line=input.readLine())!=null;){
                if(line.isEmpty()||line.startsWith("#"))continue;String[] f=line.split("\\s+");long seed=Long.parseLong(f[0]);int pattern=Integer.parseInt(f[1]),y=Integer.parseInt(f[2]),repeats=Integer.parseInt(f[3]);ReferenceWorld world=new ReferenceWorld(pattern,y);Random random=new Random(seed);WorldGenDungeons generator=new WorldGenDungeons();
                out.print("R");for(int i=0;i<repeats;++i)out.print(" "+(generator.generate(world,random,new BlockPos(0,y,0))?1:0));out.println(" "+random.nextLong());
                TreeMap<BlockPos,TileEntity> tiles=new TreeMap<>(ORDER);for(Chunk chunk:world.chunks.values())tiles.putAll(chunk.getTileEntityMap());
                for(TileEntity tile:tiles.values()){NBTTagCompound tag=new NBTTagCompound();tile.writeToNBT(tag);out.print("T ");WorldgenLootReference.printTag(out,tag);out.println();}
                for(int x=-16;x<16;++x)for(int z=-16;z<16;++z)for(int by=0;by<256;++by){IBlockState state=world.getBlockState(new BlockPos(x,by,z));int packed=WorldgenFeatureReference.packed(state);if(packed!=WorldgenFeatureReference.base(pattern,y,x,by,z))out.println("B "+x+" "+by+" "+z+" "+packed);}
                if(world.chunks.size()!=4)throw new IllegalStateException("dungeon exceeded supplied test columns");out.println("END");
            }
        }
    }
}
