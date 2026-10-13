import java.io.*;
import net.minecraft.init.Bootstrap;
import net.minecraft.world.WorldType;
import net.minecraft.world.gen.layer.*;

public final class WorldgenBiomeReference {
    public static void main(String[] args)throws Exception{
        Bootstrap.register();GenLayer[] layers=null;long previous=0;int previousType=-1;
        try(BufferedReader input=new BufferedReader(new FileReader(args[0]));DataOutputStream out=new DataOutputStream(new BufferedOutputStream(new FileOutputStream(args[1])))){
            for(String line;(line=input.readLine())!=null;){if(line.isEmpty()||line.startsWith("#"))continue;String[] f=line.split("\\s+");
                long seed=Long.parseLong(f[0]);int type=Integer.parseInt(f[1]),level=Integer.parseInt(f[2]),x=Integer.parseInt(f[3]),z=Integer.parseInt(f[4]),width=Integer.parseInt(f[5]),height=Integer.parseInt(f[6]);
                if(layers==null||previous!=seed||previousType!=type){layers=GenLayer.initializeAllBiomeGenerators(seed,type==1?WorldType.LARGE_BIOMES:type==2?WorldType.AMPLIFIED:WorldType.DEFAULT,"");previous=seed;previousType=type;}
                IntCache.resetIntCache();int[] data=layers[level].getInts(x,z,width,height);for(int i=0;i<width*height;++i)out.writeInt(data[i]);
            }
        }
    }
}
