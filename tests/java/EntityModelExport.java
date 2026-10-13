import java.io.*;
import java.lang.reflect.*;
import java.util.*;
import com.google.gson.*;
import net.minecraft.client.model.*;

/** Extracts exact original ModelRenderer/ModelBox constructor data, without GL. */
public final class EntityModelExport {
    static JsonArray vector(double... values) { JsonArray a=new JsonArray();for(double v:values)a.add(new JsonPrimitive(v));return a; }
    static void collect(ModelRenderer part, String name, IdentityHashMap<ModelRenderer,String> names) {
        if(!names.containsKey(part))names.put(part,name);
        name=names.get(part);
        if(part.childModels!=null)for(int i=0;i<part.childModels.size();++i)collect((ModelRenderer)part.childModels.get(i),name+".child"+i,names);
    }
    static JsonObject export(ModelBase model) throws Exception {
        IdentityHashMap<ModelRenderer,String> names=new IdentityHashMap<>();
        // Name fields first, so child objects keep stable vanilla field names.
        for(Class<?> type=model.getClass();type!=ModelBase.class;type=type.getSuperclass())for(Field field:type.getDeclaredFields()) {
            field.setAccessible(true);Object value=field.get(model);
            if(value instanceof ModelRenderer)names.putIfAbsent((ModelRenderer)value,field.getName());
            else if(value instanceof ModelRenderer[]) {
                ModelRenderer[] array=(ModelRenderer[])value;
                for(int i=0;i<array.length;++i)if(array[i]!=null)names.putIfAbsent(array[i],field.getName()+"["+i+"]");
            }
        }
        for(ModelRenderer part:new ArrayList<>(names.keySet()))collect(part,names.get(part),names);
        List<ModelRenderer> parts=new ArrayList<>();
        for(Object p:model.boxList)if(names.containsKey((ModelRenderer)p))parts.add((ModelRenderer)p);
        IdentityHashMap<ModelRenderer,Integer> indexes=new IdentityHashMap<>(),parents=new IdentityHashMap<>();
        for(int i=0;i<parts.size();++i)indexes.put(parts.get(i),i);
        for(int i=0;i<parts.size();++i)if(parts.get(i).childModels!=null)
            for(Object child:parts.get(i).childModels)parents.put((ModelRenderer)child,i);
        Field vertexField=ModelBox.class.getDeclaredField("vertexPositions"),quadField=ModelBox.class.getDeclaredField("quadList");
        vertexField.setAccessible(true);quadField.setAccessible(true);
        JsonObject result=new JsonObject();result.addProperty("name",model.getClass().getSimpleName());
        float width=parts.get(0).textureWidth,height=parts.get(0).textureHeight;
        for(ModelRenderer part:parts)if(part.textureWidth!=width||part.textureHeight!=height)throw new IOException("Mixed part texture sizes in "+model.getClass().getName());
        result.addProperty("textureWidth",width);result.addProperty("textureHeight",height);
        JsonArray out=new JsonArray();
        for(ModelRenderer part:parts) {
            JsonObject p=new JsonObject();p.addProperty("name",names.get(part));p.addProperty("parent",parents.getOrDefault(part,-1));
            p.add("pivot",vector(part.rotationPointX,part.rotationPointY,part.rotationPointZ));
            p.add("angle",vector(part.rotateAngleX,part.rotateAngleY,part.rotateAngleZ));
            p.add("offset",vector(part.offsetX*16,part.offsetY*16,part.offsetZ*16));
            p.addProperty("visible",part.showModel&&!part.isHidden);
            JsonArray cubes=new JsonArray();
            for(Object value:part.cubeList) {
                ModelBox box=(ModelBox)value;
                PositionTextureVertex[] vertices=(PositionTextureVertex[])vertexField.get(box);
                TexturedQuad[] quads=(TexturedQuad[])quadField.get(box);
                double x0=Double.POSITIVE_INFINITY,y0=x0,z0=x0,x1=Double.NEGATIVE_INFINITY,y1=x1,z1=x1;
                for(PositionTextureVertex v:vertices){x0=Math.min(x0,v.vector3D.xCoord);y0=Math.min(y0,v.vector3D.yCoord);z0=Math.min(z0,v.vector3D.zCoord);x1=Math.max(x1,v.vector3D.xCoord);y1=Math.max(y1,v.vector3D.yCoord);z1=Math.max(z1,v.vector3D.zCoord);}
                float u=Float.POSITIVE_INFINITY,v=Float.POSITIVE_INFINITY;
                for(PositionTextureVertex vertex:quads[1].vertexPositions)u=Math.min(u,vertex.texturePositionX*part.textureWidth);
                for(PositionTextureVertex vertex:quads[2].vertexPositions)v=Math.min(v,vertex.texturePositionY*part.textureHeight);
                JsonObject cube=new JsonObject();cube.add("pos",vector(box.posX1,box.posY1,box.posZ1));
                cube.add("size",vector(Math.round(box.posX2-box.posX1),Math.round(box.posY2-box.posY1),Math.round(box.posZ2-box.posZ1)));
                cube.add("lo",vector(x0,y0,z0));cube.add("hi",vector(x1,y1,z1));cube.addProperty("u",u);cube.addProperty("v",v);
                cube.addProperty("mirror",vertices[0].vector3D.xCoord>vertices[1].vector3D.xCoord);
                cubes.add(cube);
            }
            p.add("boxes",cubes);out.add(p);
        }
        result.add("parts",out);return result;
    }
    public static void main(String[] args) throws Exception {
        ModelBase[] models={new ModelWolf(),new ModelOcelot(),new ModelHorse(),new ModelRabbit(),new ModelWitch(0),
            new ModelGuardian(),new ModelWither(0),new ModelDragon(0),new ModelArmorStand(),new ModelBat(),new ModelBoat(),new ModelMinecart()};
        JsonArray output=new JsonArray();for(ModelBase model:models)output.add(export(model));
        JsonObject root=new JsonObject();root.add("models",output);JsonArray dyes=new JsonArray();
        for(net.minecraft.item.EnumDyeColor dye:net.minecraft.item.EnumDyeColor.values()) {
            float[] rgb=net.minecraft.entity.passive.EntitySheep.getDyeRgb(dye);dyes.add(vector(rgb[0],rgb[1],rgb[2]));
        }
        root.add("dyes",dyes);
        try(Writer writer=new FileWriter(args[0])){new GsonBuilder().setPrettyPrinting().create().toJson(root,writer);}
    }
}
