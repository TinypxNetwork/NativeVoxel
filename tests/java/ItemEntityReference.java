import java.io.*;
import java.lang.reflect.*;
import java.util.*;
import com.google.gson.*;
import net.minecraft.client.renderer.*;
import net.minecraft.client.renderer.entity.*;
import net.minecraft.client.renderer.block.model.*;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.client.resources.model.IBakedModel;
import net.minecraft.client.settings.GameSettings;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.*;
import net.minecraft.item.*;
import net.minecraft.util.*;
import org.lwjgl.util.vector.Vector3f;

public final class ItemEntityReference {
    static Object allocate(Class<?> type)throws Exception{Class<?> c=Class.forName("sun.misc.Unsafe");Field f=c.getDeclaredField("theUnsafe");f.setAccessible(true);return c.getMethod("allocateInstance",Class.class).invoke(f.get(null),type);}
    static final class Drop extends EntityItem {ItemStack stack;int age;Drop(){super(null);}@Override public ItemStack getEntityItem(){return stack;}@Override public int getAge(){return age;}}
    // The oracle captures RenderEntityItem at its nested-model boundary. Its
    // GROUND scale remains readable for bob and copy spacing; model transforms
    // are independently compared against original ModelBlock below.
    static final class BoundaryTransforms extends ItemCameraTransforms {
        BoundaryTransforms(float x,float y,float z){super(ItemTransformVec3f.DEFAULT,ItemTransformVec3f.DEFAULT,ItemTransformVec3f.DEFAULT,ItemTransformVec3f.DEFAULT,new ItemTransformVec3f(new Vector3f(),new Vector3f(),new Vector3f(x,y,z)),ItemTransformVec3f.DEFAULT);}
        @Override public void applyTransform(ItemCameraTransforms.TransformType type){}
    }
    static final class Model implements IBakedModel {
        boolean gui3d;ItemCameraTransforms transforms;
        public List<BakedQuad> getFaceQuads(EnumFacing face){return Collections.emptyList();}public List<BakedQuad> getGeneralQuads(){return Collections.emptyList();}
        public boolean isAmbientOcclusion(){return false;}public boolean isGui3d(){return gui3d;}public boolean isBuiltInRenderer(){return false;}public TextureAtlasSprite getParticleTexture(){return null;}public ItemCameraTransforms getItemCameraTransforms(){return transforms;}
    }
    static final class Builtin extends ModelBlock {Builtin(){super(new ArrayList<BlockPart>(),new HashMap<String,String>(),false,true,ItemCameraTransforms.DEFAULT);}}
    static final Map<ResourceLocation,ModelBlock> models=new HashMap<>();
    static ModelBlock model(File resources,ResourceLocation name)throws Exception {
        if(models.containsKey(name))return models.get(name);ModelBlock result;
        if(name.getResourcePath().startsWith("builtin/"))result=new Builtin();else try(Reader in=new InputStreamReader(new FileInputStream(new File(resources,"assets/"+name.getResourceDomain()+"/models/"+name.getResourcePath()+".json")),"UTF-8")){result=ModelBlock.deserialize(in);}
        models.put(name,result);if(result.getParentLocation()!=null){model(resources,result.getParentLocation());result.getParentFromMap(models);}return result;
    }
    static JsonArray transform(ItemTransformVec3f t){JsonArray out=new JsonArray();for(float value:new float[]{t.translation.x,t.translation.y,t.translation.z,t.rotation.x,t.rotation.y,t.rotation.z,t.scale.x,t.scale.y,t.scale.z})out.add(new JsonPrimitive(value));return out;}
    public static void main(String[] args)throws Exception {
        RenderManager manager=new RenderManager();RenderItem items=new RenderItem();RenderEntityItem drops=new RenderEntityItem(manager,items);RenderXPOrb xp=new RenderXPOrb(manager);
        Drop entity=(Drop)allocate(Drop.class);entity.stack=new ItemStack(new Item());Model model=new Model();items.mesher.model=model;
        List<String> lines=java.nio.file.Files.readAllLines(new File(args[0]).toPath());int dropCount=0,orbCount=0;for(String line:lines)if(line.startsWith("D "))dropCount++;else if(line.startsWith("X "))orbCount++;
        try(DataOutputStream out=new DataOutputStream(new FileOutputStream(args[1]))) {
            out.writeBytes("NVITENT1");out.writeInt(dropCount);
            for(String line:lines)if(line.startsWith("D ")) {
                String[] f=line.split(" ");int count=Integer.parseInt(f[1]),age=Integer.parseInt(f[2]);float partial=Float.parseFloat(f[3]),hover=Float.parseFloat(f[4]);double x=Double.parseDouble(f[5]),y=Double.parseDouble(f[6]),z=Double.parseDouble(f[7]);boolean gui3d=f[8].equals("1"),options=f[9].equals("1");float sx=Float.parseFloat(f[10]),sy=Float.parseFloat(f[11]),sz=Float.parseFloat(f[12]);
                entity.stack.stackSize=count;entity.age=age;entity.hoverStart=hover;model.gui3d=gui3d;model.transforms=new BoundaryTransforms(sx,sy,sz);manager.options=options?(GameSettings)allocate(GameSettings.class):null;
                GlStateManager.reset();items.matrices.clear();drops.doRender(entity,x,y,z,0,partial);
                out.writeInt(count);out.writeInt(age);out.writeFloat(partial);out.writeFloat(hover);out.writeDouble(x);out.writeDouble(y);out.writeDouble(z);out.writeBoolean(gui3d);out.writeBoolean(options);out.writeFloat(sx);out.writeFloat(sy);out.writeFloat(sz);out.writeInt(items.matrices.size());for(float[] m:items.matrices)for(float value:m)out.writeFloat(value);
            }
            out.writeInt(orbCount);Field value=EntityXPOrb.class.getDeclaredField("xpValue");value.setAccessible(true);
            for(String line:lines)if(line.startsWith("X ")) {
                String[] f=line.split(" ");int amount=Integer.parseInt(f[1]),ticks=Integer.parseInt(f[2]);float partial=Float.parseFloat(f[3]),yaw=Float.parseFloat(f[4]),pitch=Float.parseFloat(f[5]);double x=Double.parseDouble(f[6]),y=Double.parseDouble(f[7]),z=Double.parseDouble(f[8]);int light=Integer.parseInt(f[9]);
                EntityXPOrb orb=(EntityXPOrb)allocate(EntityXPOrb.class);value.setInt(orb,amount);orb.xpColor=ticks;Entity.ordinaryLight=light;manager.playerViewY=yaw;manager.playerViewX=pitch;
                GlStateManager.reset();xp.doRender(orb,x,y,z,0,partial);Tessellator tessellator=Tessellator.getInstance();
                out.writeInt(amount);out.writeInt(ticks);out.writeFloat(partial);out.writeFloat(yaw);out.writeFloat(pitch);out.writeDouble(x);out.writeDouble(y);out.writeDouble(z);out.writeInt(light);
                out.writeInt(orb.getTextureByXP());out.writeInt(tessellator.renderer.color);out.writeInt((int)OpenGlHelper.blockLight|((int)OpenGlHelper.skyLight<<16));for(float v:tessellator.matrix)out.writeFloat(v);for(float[] vertex:tessellator.renderer.vertices)for(float v:vertex)out.writeFloat(v);
            }
        }
        JsonArray registrations=new JsonParser().parse(new FileReader(args[2])).getAsJsonArray(),display=new JsonArray();
        for(JsonElement entry:registrations){String name=entry.getAsJsonObject().get("name").getAsString();if(name.equals("native:missing"))continue;String base=name.split("@")[0];ResourceLocation id=new ResourceLocation(base);ModelBlock resolved=model(new File(args[3]),new ResourceLocation(id.getResourceDomain(),"item/"+id.getResourcePath()));ItemCameraTransforms t=resolved.getAllTransforms();JsonObject item=new JsonObject();item.addProperty("name",name);item.add("ground",transform(t.ground));item.add("fixed",transform(t.fixed));display.add(item);}
        try(Writer out=new FileWriter(args[4])){new Gson().toJson(display,out);}
        System.out.println("Exported "+dropCount+" original dropped-item renders, "+orbCount+" XP-orb renders and "+display.size()+" display transforms");
    }
}
