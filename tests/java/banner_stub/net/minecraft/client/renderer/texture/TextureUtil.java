package net.minecraft.client.renderer.texture;
import java.awt.image.BufferedImage;
import java.io.*;
import javax.imageio.ImageIO;
public final class TextureUtil {
    public static BufferedImage output;
    public static BufferedImage readBufferedImage(InputStream in)throws IOException {try{return ImageIO.read(in);}finally{in.close();}}
    public static int uploadTextureImage(int id,BufferedImage image){output=image;return id;}
}
