import java.io.*;
import java.util.*;
import net.minecraft.block.*;
import net.minecraft.block.state.IBlockState;
import net.minecraft.client.multiplayer.ChunkProviderClient;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityList;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.BlockPos;
import net.minecraft.world.*;
import net.minecraft.world.chunk.*;
import net.minecraft.world.gen.GeneratorBushFeature;
import net.minecraft.world.gen.feature.*;
import net.minecraft.world.storage.*;

public final class WorldgenFeatureReference {
    static int state(int id,int meta){return id<<4|meta;}
    static int base(int pattern,int height,int x,int y,int z){
        if(y<0||y>=256||x< -30000000||x>=30000000||z< -30000000||z>=30000000)return 0;
        int bits=(x*31+z*17)&31;
        if(pattern==27){if(y>=128)return 0;if(y==0||y==127)return state(7,0);if(y<32)return state(11,0);if(((y>=40&&y<52)||(y>=66&&y<78)||(y>=105&&y<116))&&((x+z)&7)!=0)return 0;return state(87,0);}
        if(pattern>=19&&pattern<=26){
            if(pattern==26&&y==height&&x==0&&z==-1)return state(54,2);
            if(pattern==22&&y==height-1&&x==0&&z==0)return 0;
            if(pattern==23&&y==height+4&&x==0&&z==0)return 0;
            if(pattern==25&&y==height-2&&bits==0)return 0;
            if(y==height||y==height+1){if(pattern==24)return 0;if(pattern!=19&&z==0&&x<=-2)return 0;if(pattern==21&&((z==0&&x>=2)||(x==0&&(z<=-2||z>=2))))return 0;}
            return state(1,0);
        }
        if(pattern==17&&y<height-3&&(x&3)==0&&(z&3)==0)return 0;
        if(pattern==18&&y==height&&((x&15)==0||(x&15)==15)&&((z&15)==0||(z&15)==15))return state(9,0);
        if(pattern==13)return y==height?state(80,0):y<height?state(y==height-1&&bits<8?79:3,0):0;
        if(pattern==14)return y<=height?state(12,1):0;
        if(pattern==15&&y>height&&y<130&&((x&15)==1||(z&15)==1))return state(17,0);
        if(pattern==16){if(y>height)return 0;if((y==height-1||y==height-2)&&bits==0)return 0;return state(12,0);}
        if(pattern==3){if(y<=height||y==height+12)return state(87,0);return 0;}
        if(pattern==6)return y<=height?state(121,0):0;
        if(pattern==10)return y==height?state(9,0):y<height?state(3,0):0;
        if(pattern==11)return y==height?state(60,0):y<height?state(3,0):0;
        if(pattern==9){if(y<=height)return state(3,y==height?2:0);if(y<=height+4&&(x&15)==0&&(z&15)==0)return state(17,3);if(y==height+4&&bits<16)return state(18,0);return 0;}
        if(y>height){if(pattern==0&&y==height+1&&bits%4==0)return state(31,1);if(pattern==1&&y==height+1&&bits==0)return state(24,0);if(pattern==8&&y==height+2&&bits%3==0)return state(1,0);if(pattern==12&&y==height+7)return state(1,0);return 0;}
        if(pattern==5)return state(1,0);
        if(pattern==1)return state(y>=height-3?12:1,0);
        if(pattern==2){if(y==height)return state(bits<8?2:bits<16?8:9,bits<8?0:bits&7);if(y>=height-2)return state(bits<8?3:82,0);return state(1,0);}
        if(pattern==4){if(y==height)return bits<8?state(110,0):bits<16?state(3,2):bits<24?state(1,0):state(18,4);return state(3,0);}
        return state(y==height?2:y>=height-3?3:1,0);
    }
    static int packed(IBlockState state){return Block.getIdFromBlock(state.getBlock())<<4|state.getBlock().getMetaFromState(state);}
    static class ReferenceWorld extends World {
        int pattern,height,light;
        final Map<BlockPos,IBlockState> changes=new TreeMap<>((a,b)->{int c=Integer.compare(a.getX(),b.getX());if(c==0)c=Integer.compare(a.getZ(),b.getZ());if(c==0)c=Integer.compare(a.getY(),b.getY());return c;});
        final List<Entity> spawns=new ArrayList<>();
        ReferenceWorld(int pattern,int height,int noSky,int light){
            super(new SaveHandlerMP(),new WorldInfo(new WorldSettings(0,WorldSettings.GameType.SURVIVAL,false,false,WorldType.DEFAULT),"feature_reference"),WorldProvider.getProviderForDimension(noSky<0?-1:noSky!=0?1:0),new Profiler(),true);
            this.pattern=pattern;this.height=height;this.light=light;provider.registerWorld(this);chunkProvider=createChunkProvider();
        }
        protected IChunkProvider createChunkProvider(){return new ChunkProviderClient(this);}
        protected int getRenderDistanceChunks(){return 8;}
        public IBlockState getBlockState(BlockPos at){IBlockState changed=changes.get(at);if(changed!=null)return changed;int value=base(pattern,height,at.getX(),at.getY(),at.getZ());return Block.getBlockById(value>>4).getStateFromMeta(value&15);}
        public boolean setBlockState(BlockPos at,IBlockState state,int flags){
            if(at.getY()<0||at.getY()>=256||at.getX()< -30000000||at.getX()>=30000000||at.getZ()< -30000000||at.getZ()>=30000000)return false;
            if(packed(getBlockState(at))==packed(state))return false;
            BlockPos copy=new BlockPos(at.getX(),at.getY(),at.getZ());
            if(packed(state)==base(pattern,height,at.getX(),at.getY(),at.getZ()))changes.remove(copy);else changes.put(copy,state);return true;
        }
        public int getLight(BlockPos at){return light-((at.getX()+at.getZ())&1);}
        public boolean spawnEntityInWorld(Entity entity){spawns.add(entity);return true;}
    }
    public static void main(String[] args)throws Exception{
        Bootstrap.register();
        try(BufferedReader input=new BufferedReader(new FileReader(args[0]));PrintWriter out=new PrintWriter(new BufferedWriter(new FileWriter(args[1])))){
            for(String line;(line=input.readLine())!=null;){
                if(line.isEmpty()||line.startsWith("#"))continue;String[] f=line.split("\\s+");long seed=Long.parseLong(f[0]);int kind=Integer.parseInt(f[1]),pattern=Integer.parseInt(f[2]),height=Integer.parseInt(f[3]),x=Integer.parseInt(f[4]),z=Integer.parseInt(f[5]),noSky=Integer.parseInt(f[6]),light=Integer.parseInt(f[7]);
                ReferenceWorld world=new ReferenceWorld(pattern,height,noSky,light);Random random=new Random(seed);BlockPos at=new BlockPos(x,height+1,z);WorldGenerator generator;
                switch(kind){
                case 0:generator=new WorldGenFlowers(Blocks.yellow_flower,BlockFlower.EnumFlowerType.DANDELION);break;
                case 1:generator=new WorldGenFlowers(Blocks.red_flower,BlockFlower.EnumFlowerType.OXEYE_DAISY);break;
                case 2:at=at.up(10);generator=new WorldGenTallGrass(BlockTallGrass.EnumType.GRASS);break;
                case 3:at=at.up(10);generator=new WorldGenTallGrass(BlockTallGrass.EnumType.FERN);break;
                case 4:at=at.up(10);generator=new WorldGenDeadBush();break;
                case 5:case 6:WorldGenDoublePlant doublePlant=new WorldGenDoublePlant();doublePlant.setPlantType(kind==5?BlockDoublePlant.EnumPlantType.SUNFLOWER:BlockDoublePlant.EnumPlantType.ROSE);generator=doublePlant;break;
                case 7:generator=new WorldGenCactus();break;
                case 8:generator=new WorldGenReed();break;
                case 9:generator=new WorldGenPumpkin();break;
                case 10:generator=new WorldGenMelon();break;
                case 11:generator=new WorldGenWaterlily();break;
                case 12:generator=new GeneratorBushFeature(Blocks.brown_mushroom);break;
                case 13:generator=new GeneratorBushFeature(Blocks.red_mushroom);break;
                case 14:generator=new WorldGenFire();break;
                case 15:at=at.up(10);generator=new WorldGenGlowStone1();break;
                case 16:at=at.up(10);generator=new WorldGenGlowStone2();break;
                case 17:at=at.down();generator=new WorldGenClay(4);break;
                case 18:at=at.down();generator=new WorldGenSand(Blocks.sand,7);break;
                case 19:at=at.down();generator=new WorldGenSand(Blocks.gravel,6);break;
                case 20:at=at.up(10);generator=new WorldGenBlockBlob(Blocks.mossy_cobblestone,0);break;
                case 21:generator=new WorldGenSpikes(Blocks.end_stone);break;
                case 22:generator=new WorldGenSpikes(Blocks.stone);break;
                case 23:generator=new WorldGenIceSpike();break;
                case 24:generator=new WorldGenIcePath(4);break;
                case 25:generator=new WorldGenDesertWells();break;
                case 26:generator=new WorldGenVines();break;
                default:throw new IllegalArgumentException("unknown feature kind");
                }
                boolean result=generator.generate(world,random,at);out.println("R "+(result?1:0)+" "+random.nextLong());
                for(Entity e:world.spawns)out.println("E "+EntityList.getEntityString(e)+" "+Double.doubleToLongBits(e.posX)+" "+Double.doubleToLongBits(e.posY)+" "+Double.doubleToLongBits(e.posZ)+" "+Integer.toUnsignedLong(Float.floatToIntBits(e.rotationYaw))+" "+Integer.toUnsignedLong(Float.floatToIntBits(e.rotationPitch)));
                for(Map.Entry<BlockPos,IBlockState> entry:world.changes.entrySet()){BlockPos p=entry.getKey();out.println("B "+p.getX()+" "+p.getY()+" "+p.getZ()+" "+packed(entry.getValue()));}out.println("END");
            }
        }
    }
}
