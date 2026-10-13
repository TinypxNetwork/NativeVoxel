import java.io.*;
import java.lang.reflect.*;
import java.util.*;
import com.google.gson.*;
import net.minecraft.client.renderer.block.model.*;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.client.resources.model.ModelRotation;
import net.minecraft.util.EnumFacing;

/** Actual ItemModelGenerator + FaceBakery reference, without an OpenGL context. */
public final class GeneratedItemReference {
    static final class Sprite extends TextureAtlasSprite { Sprite(String name){super(name);} }
    static JsonArray vector(float...values){JsonArray out=new JsonArray();for(float value:values)out.add(new JsonPrimitive(value));return out;}
    public static void main(String[] args)throws Exception {
        JsonArray result=new JsonArray();ItemModelGenerator generator=new ItemModelGenerator();FaceBakery bakery=new FaceBakery();
        Method method=ItemModelGenerator.class.getDeclaredMethod("func_178394_a",int.class,String.class,TextureAtlasSprite.class);method.setAccessible(true);
        try(DataInputStream in=new DataInputStream(new FileInputStream(args[0]))) {
            byte[] magic=new byte[8];in.readFully(magic);if(!new String(magic,"US-ASCII").equals("NVSPRIT1"))throw new IOException("Fixture format");
            int count=in.readInt();
            for(int sample=0;sample<count;sample++) {
                byte[] nameBytes=new byte[in.readInt()];in.readFully(nameBytes);String name=new String(nameBytes,"UTF-8");int width=in.readInt(),height=in.readInt(),frameCount=in.readInt();
                Sprite sprite=new Sprite(name);sprite.setIconWidth(width);sprite.setIconHeight(height);List<int[][]> frames=new ArrayList<>();
                for(int frame=0;frame<frameCount;frame++){int[] pixels=new int[width*height];for(int i=0;i<pixels.length;i++)pixels[i]=in.readInt();frames.add(new int[][]{pixels});}
                sprite.setFramesTextureData(frames);sprite.initSprite(1024,1024,16,32,false);
                @SuppressWarnings("unchecked") List<BlockPart> parts=(List<BlockPart>)method.invoke(generator,0,name,sprite);
                JsonObject item=new JsonObject();item.addProperty("name",name);JsonArray elements=new JsonArray();
                for(BlockPart part:parts) {
                    JsonObject element=new JsonObject();element.add("from",vector(part.positionFrom.x,part.positionFrom.y,part.positionFrom.z));element.add("to",vector(part.positionTo.x,part.positionTo.y,part.positionTo.z));JsonObject faces=new JsonObject();
                    for(Map.Entry<EnumFacing,BlockPartFace> entry:part.mapFaces.entrySet()) {
                        JsonObject face=new JsonObject();face.add("uv",vector(entry.getValue().blockFaceUV.uvs));face.addProperty("tintindex",entry.getValue().tintIndex);
                        BakedQuad quad=bakery.makeBakedQuad(part.positionFrom,part.positionTo,entry.getValue(),sprite,entry.getKey(),ModelRotation.X0_Y0,part.partRotation,false,part.shade);
                        face.addProperty("bakedFace",quad.getFace().getIndex());JsonArray vertices=new JsonArray();int[] data=quad.getVertexData();
                        for(int v=0;v<4;v++){int offset=v*7;vertices.add(vector(Float.intBitsToFloat(data[offset]),Float.intBitsToFloat(data[offset+1]),Float.intBitsToFloat(data[offset+2]),Float.intBitsToFloat(data[offset+4]),Float.intBitsToFloat(data[offset+5])));}
                        face.add("vertices",vertices);faces.add(entry.getKey().getName(),face);
                    }
                    element.add("faces",faces);elements.add(element);
                }
                item.add("elements",elements);result.add(item);
            }
        }
        try(Writer out=new FileWriter(args[1])){new Gson().toJson(result,out);}
        System.out.println("Exported "+result.size()+" original ItemModelGenerator/FaceBakery sprite fixtures");
    }
}
