import java.io.*;
import java.lang.reflect.*;
import java.util.*;
import net.minecraft.block.Block;
import net.minecraft.client.multiplayer.ChunkProviderClient;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.BlockPos;
import net.minecraft.world.*;
import net.minecraft.world.biome.*;
import net.minecraft.world.chunk.*;
import net.minecraft.world.gen.ChunkProviderGenerate;
import net.minecraft.world.storage.*;

public final class WorldgenSpawnReference {
    static final class TerrainWorld extends World {
        final Map<Long,Chunk> chunks=new HashMap<>();
        ChunkProviderGenerate generator;
        TerrainWorld(long seed,WorldType type,int dimension){
            super(new SaveHandlerMP(),new WorldInfo(new WorldSettings(seed,WorldSettings.GameType.SURVIVAL,false,false,type),"spawn_reference"),WorldProvider.getProviderForDimension(dimension),new Profiler(),true);
            provider.registerWorld(this);chunkProvider=createChunkProvider();generator=new ChunkProviderGenerate(this,seed,false,"");
        }
        protected IChunkProvider createChunkProvider(){return new ChunkProviderClient(this);}
        protected int getRenderDistanceChunks(){return 8;}
        protected boolean isChunkLoaded(int x,int z,boolean allowEmpty){return chunks!=null&&chunks.containsKey((long)x&0xffffffffL|((long)z<<32));}
        public Chunk getChunkFromChunkCoords(int x,int z){
            long key=(long)x&0xffffffffL|((long)z<<32);Chunk chunk=chunks.get(key);
            if(chunk==null){chunk=generator.provideChunk(x,z);chunks.put(key,chunk);}return chunk;
        }
    }
    // Constructor is deliberately bypassed: createSpawnPosition requires only
    // original World fields, a real biome provider, and its ground predicate.
    // The original private WorldServer method itself runs unmodified.
    static final class SpawnWorld extends WorldServer {
        TerrainWorld terrain;PrintWriter output;int mode,calls;boolean lastAccepted;
        SpawnWorld(){super(null,null,null,0,null);}
        public Block getGroundAboveSeaLevel(BlockPos position){
            ++calls;int x=position.getX(),z=position.getZ();output.println("Q "+x+" "+z);
            lastAccepted=mode==0||mode==2&&((x*31+z*17)&63)==0||mode==3&&calls>=250||mode==4&&terrain.getGroundAboveSeaLevel(position)==Blocks.grass;
            return lastAccepted?Blocks.grass:Blocks.stone;
        }
    }
    static Object allocate(Class<?> type)throws Exception{
        Class<?> cls=Class.forName("sun.misc.Unsafe");Field field=cls.getDeclaredField("theUnsafe");field.setAccessible(true);return cls.getMethod("allocateInstance",Class.class).invoke(field.get(null),type);
    }
    public static void main(String[] args)throws Exception{
        Bootstrap.register();Method create=WorldServer.class.getDeclaredMethod("createSpawnPosition",WorldSettings.class);create.setAccessible(true);
        try(BufferedReader input=new BufferedReader(new FileReader(args[0]));PrintWriter out=new PrintWriter(new BufferedWriter(new FileWriter(args[1])))){
            for(String line;(line=input.readLine())!=null;){
                if(line.isEmpty()||line.startsWith("#"))continue;String[] f=line.split("\\s+");long seed=Long.parseLong(f[1]);int type=Integer.parseInt(f[2]);WorldType worldType=type==1?WorldType.LARGE_BIOMES:type==2?WorldType.AMPLIFIED:WorldType.DEFAULT;
                if(f[0].equals("B")){
                    WorldChunkManager manager=new WorldChunkManager(seed,worldType,"");int x=Integer.parseInt(f[3]),z=Integer.parseInt(f[4]),range=Integer.parseInt(f[5]),count=Integer.parseInt(f[7]);Random random=new Random(Long.parseLong(f[6]));List<BiomeGenBase> allowed=new ArrayList<>();
                    for(int i=0;i<count;++i)allowed.add(BiomeGenBase.getBiome(Integer.parseInt(f[8+i])));
                    BlockPos pos=manager.findBiomePosition(x,z,range,allowed,random);out.println("B "+(pos==null?"0 0 0":"1 "+pos.getX()+" "+pos.getZ())+" "+random.nextLong());
                }else{
                    int dimension=Integer.parseInt(f[3]),mode=Integer.parseInt(f[4]);TerrainWorld terrain=new TerrainWorld(seed,worldType,dimension);SpawnWorld world=(SpawnWorld)allocate(SpawnWorld.class);
                    for(Field field:World.class.getDeclaredFields())if(!Modifier.isStatic(field.getModifiers())){field.setAccessible(true);field.set(world,field.get(terrain));}
                    world.terrain=terrain;world.output=out;world.mode=mode;
                    WorldProvider provider=WorldProvider.getProviderForDimension(dimension);Field providerField=World.class.getDeclaredField("provider");providerField.setAccessible(true);providerField.set(world,provider);provider.registerWorld(world);
                    create.invoke(world,new WorldSettings(seed,WorldSettings.GameType.SURVIVAL,false,false,worldType));
                    WorldInfo info=world.getWorldInfo();int moves=world.calls==0?0:world.lastAccepted?world.calls-1:1000;
                    out.println("S "+info.getSpawnX()+" "+info.getSpawnY()+" "+info.getSpawnZ()+" 1 "+(world.calls==0||world.lastAccepted?1:0)+" "+moves);
                }
            }
        }
    }
}
