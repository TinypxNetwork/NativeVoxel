import java.io.*;
import java.lang.reflect.Field;
import java.util.*;
import net.minecraft.block.*;
import net.minecraft.block.state.IBlockState;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.util.BlockPos;
import net.minecraft.world.gen.feature.*;

public final class WorldgenTreeReference {
    public static void main(String[] args)throws Exception{
        Bootstrap.register();WorldGenBigMushroom randomMushroom=new WorldGenBigMushroom();WorldGenBigTree[] sharedBig={new WorldGenBigTree(false),new WorldGenBigTree(false)};
        Field heightField=WorldGenBigTree.class.getDeclaredField("heightLimit"),leafField=WorldGenBigTree.class.getDeclaredField("leafDistanceLimit");heightField.setAccessible(true);leafField.setAccessible(true);
        try(BufferedReader input=new BufferedReader(new FileReader(args[0]));PrintWriter out=new PrintWriter(new BufferedWriter(new FileWriter(args[1])))){
            for(String line;(line=input.readLine())!=null;){
                if(line.isEmpty()||line.startsWith("#"))continue;String[] f=line.split("\\s+");long seed=Long.parseLong(f[0]);int kind=Integer.parseInt(f[1]),pattern=Integer.parseInt(f[2]),height=Integer.parseInt(f[3]),x=Integer.parseInt(f[4]),z=Integer.parseInt(f[5]);
                WorldgenFeatureReference.ReferenceWorld world=new WorldgenFeatureReference.ReferenceWorld(pattern,height,0,15);Random random=new Random(seed);BlockPos at=new BlockPos(x,height+1,z);WorldGenerator generator;
                switch(kind){
                case 0:generator=new WorldGenTrees(false);break;
                case 1:case 11:generator=new WorldGenTrees(false,kind==1?7:4,Blocks.log.getStateFromMeta(3),Blocks.leaves.getStateFromMeta(3),true);break;
                case 2:generator=new WorldGenForest(false,false);break;
                case 3:generator=new WorldGenForest(false,true);break;
                case 4:generator=new WorldGenTaiga1();break;
                case 5:generator=new WorldGenTaiga2(false);break;
                case 6:generator=new WorldGenSwamp();break;
                case 7:at=at.up(10);generator=new WorldGenShrub(Blocks.log.getStateFromMeta(3),Blocks.leaves.getStateFromMeta(0));break;
                case 8:generator=new WorldGenBigMushroom(Blocks.brown_mushroom_block);break;
                case 9:generator=new WorldGenBigMushroom(Blocks.red_mushroom_block);break;
                case 10:generator=randomMushroom;break;
                case 12:generator=new WorldGenSavannaTree(false);break;
                case 13:generator=new WorldGenCanopyTree(false);break;
                case 14:generator=new WorldGenMegaJungle(false,10,20,Blocks.log.getStateFromMeta(3),Blocks.leaves.getStateFromMeta(3));break;
                case 15:case 17:generator=new WorldGenMegaPineTree(false,false);break;
                case 16:case 18:generator=new WorldGenMegaPineTree(false,true);break;
                case 19:case 20:generator=new WorldGenBigTree(false);if(kind==20)generator.func_175904_e();break;
                case 21:case 22:generator=sharedBig[kind-21];if(kind==22)generator.func_175904_e();break;
                default:throw new IllegalArgumentException("unknown tree kind");
                }
                boolean result=generator.generate(world,random,at);if(result&&(kind==17||kind==18))((WorldGenAbstractTree)generator).func_180711_a(world,random,at);
                out.println("R "+(result?1:0)+" "+random.nextLong());if(kind>=19)out.println("S "+heightField.getInt(generator)+" "+leafField.getInt(generator));
                for(Map.Entry<BlockPos,IBlockState> entry:world.changes.entrySet()){BlockPos p=entry.getKey();out.println("B "+p.getX()+" "+p.getY()+" "+p.getZ()+" "+WorldgenFeatureReference.packed(entry.getValue()));}out.println("END");
            }
        }
    }
}
