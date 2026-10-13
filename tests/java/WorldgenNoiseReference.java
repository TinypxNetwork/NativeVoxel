import java.io.*;
import java.util.*;
import net.minecraft.world.gen.*;

/** Original unmodified 1.8.9 noise classes + java.util.Random reference. */
public final class WorldgenNoiseReference {
    static void values(PrintWriter out,int id,double[] values){for(int i=0;i<values.length;++i)out.println("N "+id+" "+i+" "+Long.toHexString(Double.doubleToRawLongBits(values[i])));}
    public static void main(String[] args)throws Exception{
        try(BufferedReader input=new BufferedReader(new FileReader(args[0]));PrintWriter out=new PrintWriter(new FileWriter(args[1]))){int id=0;
            for(String line;(line=input.readLine())!=null;){if(line.isEmpty()||line.startsWith("#"))continue;Scanner in=new Scanner(line);in.useLocale(Locale.ROOT);char kind=in.next().charAt(0);Random random=new Random(in.nextLong());
                if(kind=='R'){int count=in.nextInt();int[] bounds={1,2,3,16,17,1000,1073741825,2147483647};for(int i=0;i<count;++i){
                    int a=random.nextInt(),b=random.nextInt(bounds[i%8]);long c=random.nextLong();float f=random.nextFloat();double d=random.nextDouble();boolean flag=random.nextBoolean();byte[] bytes=new byte[i%9];random.nextBytes(bytes);
                    StringBuilder encoded=new StringBuilder();for(byte v:bytes)encoded.append(String.format(Locale.ROOT,"%02x",v&255));
                    out.println("R "+id+" "+i+" "+a+" "+b+" "+c+" "+Integer.toHexString(Float.floatToRawIntBits(f))+" "+Long.toHexString(Double.doubleToRawLongBits(d))+" "+(flag?1:0)+" "+(bytes.length==0?"-":encoded));
                }}else if(kind=='G'){int count=in.nextInt();for(int i=0;i<count;++i)out.println("G "+id+" "+i+" "+Long.toHexString(Double.doubleToRawLongBits(random.nextGaussian())));}
                else if(kind=='I'){
                    double x=in.nextDouble(),y=in.nextDouble(),z=in.nextDouble();int nx=in.nextInt(),ny=in.nextInt(),nz=in.nextInt();double xs=in.nextDouble(),ys=in.nextDouble(),zs=in.nextDouble(),scale=in.nextDouble();
                    NoiseGeneratorImproved noise=new NoiseGeneratorImproved(random);double[] a=new double[nx*ny*nz];Arrays.fill(a,.125);noise.populateNoiseArray(a,x,y,z,nx,ny,nz,xs,ys,zs,scale);values(out,id,a);
                }else if(kind=='O'){
                    int octaves=in.nextInt(),x=in.nextInt(),y=in.nextInt(),z=in.nextInt(),nx=in.nextInt(),ny=in.nextInt(),nz=in.nextInt();double xs=in.nextDouble(),ys=in.nextDouble(),zs=in.nextDouble();
                    NoiseGeneratorOctaves noise=new NoiseGeneratorOctaves(random,octaves);double[] a=new double[nx*ny*nz+3];Arrays.fill(a,7);values(out,id,noise.generateNoiseOctaves(a,x,y,z,nx,ny,nz,xs,ys,zs));
                }else if(kind=='S'){
                    double x=in.nextDouble(),z=in.nextDouble();int count=in.nextInt();NoiseGeneratorSimplex noise=new NoiseGeneratorSimplex(random);double[] a=new double[count];for(int i=0;i<count;++i)a[i]=noise.func_151605_a(x+(double)i*.125,z-(double)i*.0625);values(out,id,a);
                }else if(kind=='A'){
                    double x=in.nextDouble(),z=in.nextDouble();int nx=in.nextInt(),nz=in.nextInt();double xs=in.nextDouble(),zs=in.nextDouble(),scale=in.nextDouble();NoiseGeneratorSimplex noise=new NoiseGeneratorSimplex(random);double[] a=new double[nx*nz];Arrays.fill(a,.125);noise.func_151606_a(a,x,z,nx,nz,xs,zs,scale);values(out,id,a);
                }else if(kind=='P'){
                    int octaves=in.nextInt();double x=in.nextDouble(),z=in.nextDouble();int nx=in.nextInt(),nz=in.nextInt();double xs=in.nextDouble(),zs=in.nextDouble(),lacunarity=in.nextDouble(),persistence=in.nextDouble();NoiseGeneratorPerlin noise=new NoiseGeneratorPerlin(random,octaves);double[] a=new double[nx*nz+3];Arrays.fill(a,8);values(out,id,noise.func_151600_a(a,x,z,nx,nz,xs,zs,lacunarity,persistence));
                }else if(kind=='V'){
                    int octaves=in.nextInt();double x=in.nextDouble(),z=in.nextDouble();int count=in.nextInt();NoiseGeneratorPerlin noise=new NoiseGeneratorPerlin(random,octaves);double[] a=new double[count];for(int i=0;i<count;++i)a[i]=noise.func_151601_a(x+(double)i*.125,z-(double)i*.0625);values(out,id,a);
                }else throw new IOException("unknown noise command");
                out.println("T "+id+" "+random.nextLong());++id;in.close();
            }
        }
    }
}
