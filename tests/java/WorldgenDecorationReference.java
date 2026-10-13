import java.io.*;
import java.lang.reflect.Field;
import java.util.*;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityList;
import net.minecraft.init.Bootstrap;
import net.minecraft.util.BlockPos;
import net.minecraft.world.biome.*;
import net.minecraft.world.gen.NoiseGeneratorPerlin;

public final class WorldgenDecorationReference {
    static Field randomSeed;
    static final class ReferenceWorld extends WorldgenFeatureReference.ReferenceWorld {
        final List<String> ticks=new ArrayList<>();
        ReferenceWorld(int pattern,int noSky){super(pattern,63,noSky,13);}
        public BlockPos getHeight(BlockPos p){
            if(p.getX()< -30000000||p.getX()>=30000000||p.getZ()< -30000000||p.getZ()>=30000000)return new BlockPos(p.getX(),64,p.getZ());
            int y=256;while(y>0&&getBlockState(new BlockPos(p.getX(),y-1,p.getZ())).getBlock().getLightOpacity()==0)--y;return new BlockPos(p.getX(),y,p.getZ());
        }
        public BlockPos getTopSolidOrLiquidBlock(BlockPos p){
            int y=256;for(;y>=0;--y){Material material=getBlockState(new BlockPos(p.getX(),y-1,p.getZ())).getBlock().getMaterial();if(material.blocksMovement()&&material!=Material.leaves)break;}return new BlockPos(p.getX(),y,p.getZ());
        }
        // Capture the callback boundary. Real server fluid execution remains a
        // separate kernel; no fake fluid simulation is used in this reference.
        public void forceBlockUpdateTick(Block block,BlockPos p,Random random){
            try{ticks.add("F "+p.getX()+" "+p.getY()+" "+p.getZ()+" "+(Block.getIdFromBlock(block)<<4)+" "+((AtomicLong)randomSeed.get(random)).get());}catch(Exception e){throw new RuntimeException(e);}
        }
    }
    public static void main(String[] args)throws Exception{
        Bootstrap.register();
        if(args[0].equals("--sites")){
            Field field=BiomeGenBase.class.getDeclaredField("GRASS_COLOR_NOISE");field.setAccessible(true);NoiseGeneratorPerlin noise=(NoiseGeneratorPerlin)field.get(null);
            try(PrintWriter out=new PrintWriter(new FileWriter(args[1]))){int count=0;for(int x=-128;x<=128&&count<3;++x)for(int z=-128;z<=128&&count<3;++z)if(noise.func_151601_a((x*16+8)/200.0,(z*16+8)/200.0)<-.8){out.println(x+" "+z);++count;}}
            return;
        }
        randomSeed=Random.class.getDeclaredField("seed");randomSeed.setAccessible(true);Field decorator=BiomeGenBase.class.getDeclaredField("theBiomeDecorator");decorator.setAccessible(true);
        try(BufferedReader input=new BufferedReader(new FileReader(args[0]));PrintWriter out=new PrintWriter(new BufferedWriter(new FileWriter(args[1])))){
            for(String line;(line=input.readLine())!=null;){
                if(line.isEmpty()||line.startsWith("#"))continue;String[] f=line.split("\\s+");int biome=Integer.parseInt(f[0]),pattern=Integer.parseInt(f[2]),cx=Integer.parseInt(f[3]),cz=Integer.parseInt(f[4]),springs=Integer.parseInt(f[5]);long seed=Long.parseLong(f[1]);
                for(BiomeGenBase b:BiomeGenBase.getBiomeGenArray())if(b!=null)((BiomeDecorator)decorator.get(b)).generateLakes=springs!=0;
                ReferenceWorld world=new ReferenceWorld(pattern,biome==8||biome==9?1:0);Random random=new Random(seed);BiomeGenBase.getBiome(biome).decorate(world,random,new BlockPos(cx*16,0,cz*16));
                out.println("R "+random.nextLong());
                for(Entity e:world.spawns)out.println("E "+EntityList.getEntityString(e)+" "+Double.doubleToLongBits(e.posX)+" "+Double.doubleToLongBits(e.posY)+" "+Double.doubleToLongBits(e.posZ)+" "+Integer.toUnsignedLong(Float.floatToIntBits(e.rotationYaw))+" "+Integer.toUnsignedLong(Float.floatToIntBits(e.rotationPitch)));
                for(String tick:world.ticks)out.println(tick);
                for(Map.Entry<BlockPos,IBlockState> entry:world.changes.entrySet()){BlockPos p=entry.getKey();out.println("B "+p.getX()+" "+p.getY()+" "+p.getZ()+" "+WorldgenFeatureReference.packed(entry.getValue()));}out.println("END");
            }
        }
    }
}
