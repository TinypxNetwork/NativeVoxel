import java.awt.image.BufferedImage;
import java.io.*;
import java.util.*;
import javax.imageio.ImageIO;
import com.google.gson.*;
import net.minecraft.client.resources.*;
import net.minecraft.client.renderer.texture.*;
import net.minecraft.item.EnumDyeColor;
import net.minecraft.util.ResourceLocation;

/** Executes original LayeredColorMaskTexture.loadTexture; GPU upload is captured. */
public final class BannerReference {
    static final class Resources implements IResourceManager {
        final Map<String,byte[]> encoded=new LinkedHashMap<>();
        public IResource getResource(ResourceLocation location)throws IOException {final byte[] data=encoded.get(location.toString());if(data==null)throw new IOException(location.toString());return ()->new ByteArrayInputStream(data);}
        void file(String name,File file)throws Exception{encoded.put(name,java.nio.file.Files.readAllBytes(file.toPath()));}
        void image(String name,BufferedImage image)throws Exception{ByteArrayOutputStream out=new ByteArrayOutputStream();ImageIO.write(image,"png",out);encoded.put(name,out.toByteArray());}
        BufferedImage image(String name)throws Exception{return ImageIO.read(new ByteArrayInputStream(encoded.get(name)));}
    }
    static final class Case {
        String name,base;List<String> masks=new ArrayList<>();List<EnumDyeColor> colors=new ArrayList<>();
        Case(String name,String base){this.name=name;this.base=base;}
        Case add(String mask,int color){masks.add(mask);colors.add(EnumDyeColor.byDyeDamage(color));return this;}
    }
    static void string(DataOutputStream out,String value)throws Exception{byte[] bytes=value.getBytes("UTF-8");out.writeInt(bytes.length);out.write(bytes);}
    static void pixels(DataOutputStream out,BufferedImage image)throws Exception{for(int y=0;y<image.getHeight();y++)for(int x=0;x<image.getWidth();x++){int c=image.getRGB(x,y);out.writeByte(c>>16);out.writeByte(c>>8);out.writeByte(c);out.writeByte(c>>>24);}}
    public static void main(String[] args)throws Exception {
        Resources resources=new Resources();String base="minecraft:textures/entity/banner_base.png";resources.file(base,new File(args[0],"resources/assets/minecraft/textures/entity/banner_base.png"));
        JsonArray patterns=new JsonParser().parse(new FileReader(new File(args[0],"runtime/tiles/patterns.json"))).getAsJsonObject().getAsJsonArray("patterns");List<String> names=new ArrayList<>();
        for(JsonElement element:patterns){String name="minecraft:textures/entity/banner/"+element.getAsJsonObject().get("name").getAsString()+".png";resources.file(name,new File(args[0],"resources/assets/"+name.replace(':','/')));names.add(name);}
        List<Case> cases=new ArrayList<>();String baseMask=names.get(0);
        for(int color=0;color<16;color++)cases.add(new Case("base_"+color,base).add(baseMask,color));
        for(int i=0;i<names.size();i++)cases.add(new Case("pattern_"+i,base).add(baseMask,i%16).add(names.get(i),(i*7+3)%16));
        for(int count:new int[]{2,5,16,17,18,30}){Case c=new Case("ordered_"+count,base).add(baseMask,15);for(int i=1;i<count;i++)c.add(names.get((i*7)%names.size()),i%16);cases.add(c);}
        cases.add(new Case("forward_order",base).add(baseMask,15).add(names.get(5),1).add(names.get(26),4));cases.add(new Case("reverse_order",base).add(baseMask,15).add(names.get(26),4).add(names.get(5),1));
        for(int index=0;index<4;index++){int width=index==3?32:64,height=index==3?16:64,type=index==1?BufferedImage.TYPE_3BYTE_BGR:BufferedImage.TYPE_4BYTE_ABGR;BufferedImage image=new BufferedImage(width,height,type);
            for(int y=0;y<height;y++)for(int x=0;x<width;x++){int r=(x*17+y*11)&255,g=(x*7+y*29)&255,b=(x*13+y*3)&255,a=index==1?255:index==2?((x+y)%4==0?0:37):(x*37+y*61)&255;image.setRGB(x,y,(a<<24)|(r<<16)|(g<<8)|b);}
            resources.image("native:synthetic_"+index,image);}
        for(int color=0;color<16;color++){cases.add(new Case("alpha_"+color,"native:synthetic_0").add("native:synthetic_2",color).add(baseMask,15-color));cases.add(new Case("rgb_"+color,"native:synthetic_1").add("native:synthetic_2",color));}
        cases.add(new Case("non_rgba_mask",base).add("native:synthetic_1",4));cases.add(new Case("wrong_size_mask",base).add("native:synthetic_3",4));
        List<String> images=new ArrayList<>(resources.encoded.keySet());
        try(DataOutputStream out=new DataOutputStream(new FileOutputStream(args[1]))){out.writeBytes("NVBANNR1");out.writeInt(images.size());out.writeInt(cases.size());
            for(String name:images){BufferedImage image=resources.image(name);string(out,name);out.writeInt(image.getWidth());out.writeInt(image.getHeight());out.writeInt(image.getType()==6?1:0);pixels(out,image);}
            for(Case sample:cases){string(out,sample.name);out.writeInt(images.indexOf(sample.base));out.writeInt(sample.masks.size());for(int i=0;i<sample.masks.size();i++){out.writeInt(images.indexOf(sample.masks.get(i)));out.writeInt(sample.colors.get(i).getMapColor().colorValue);}
                TextureUtil.output=null;new LayeredColorMaskTexture(new ResourceLocation(sample.base),sample.masks,sample.colors).loadTexture(resources);if(TextureUtil.output==null)throw new IOException("No composite for "+sample.name);pixels(out,TextureUtil.output);}
        }
        System.out.println("Exported "+cases.size()+" original Java banner composites from "+images.size()+" images");
    }
}
