import java.awt.image.BufferedImage;
import java.io.*;
import javax.imageio.ImageIO;
import net.minecraft.client.renderer.ImageBufferDownload;

public class SkinReference {
    public static void main(String[] args) throws Exception {
        try(DataOutputStream out=new DataOutputStream(new FileOutputStream(args[0]))) {
            out.writeBytes("NVSKREF1");out.writeInt(20);
            for(int test=0;test<20;test++) {
                int width=64,height=test<4&&(test&1)!=0?32:64;
                BufferedImage image=new BufferedImage(width,height,BufferedImage.TYPE_INT_ARGB);
                out.writeInt(width);out.writeInt(height);
                for(int y=0;y<height;y++)for(int x=0;x<width;x++) {
                    int r=(x*17+y*11)&255,g=(x*7+y*29)&255,b=(x*13+y*3)&255,a=test<2?255:(x*37+y*61)&255;
                    if(test>=4){int sample=(test-4)*4096+y*64+x;r=sample&255;g=255-r;b=(r*53)&255;a=sample>>>8;}
                    image.setRGB(x,y,(a<<24)|(r<<16)|(g<<8)|b);out.writeByte(r);out.writeByte(g);out.writeByte(b);out.writeByte(a);
                }
                if(test<4)ImageIO.write(image,"png",new File(new File(args[0]).getParentFile(),"skin_input_"+test+".png"));
                BufferedImage result=new ImageBufferDownload().parseUserSkin(image);
                for(int y=0;y<64;y++)for(int x=0;x<64;x++){int c=result.getRGB(x,y);out.writeByte(c>>16);out.writeByte(c>>8);out.writeByte(c);out.writeByte(c>>>24);}
            }
        }
    }
}
