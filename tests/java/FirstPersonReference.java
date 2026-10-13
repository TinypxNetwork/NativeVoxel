import java.io.*;
import java.lang.reflect.*;
import java.util.*;
import net.minecraft.client.Minecraft;
import net.minecraft.client.entity.*;
import net.minecraft.client.renderer.*;
import net.minecraft.client.renderer.entity.RenderManager;
import net.minecraft.client.model.*;
import net.minecraft.item.*;
import net.minecraft.util.ResourceLocation;

/** Invokes unmodified original ItemRenderer helpers with a GL matrix recorder. */
public final class FirstPersonReference {
    static class Player extends EntityPlayerSP {
        int remaining;
        Player(){super(null,null,null,null);}
        @Override public int getItemInUseCount(){return remaining;}
        @Override public boolean isInvisible(){return false;}
        @Override public boolean isSneaking(){return false;}
        @Override public ResourceLocation getLocationSkin(){return new ResourceLocation("test");}
    }
    static class HeldItem extends Item {int duration;@Override public int getMaxItemUseDuration(ItemStack stack){return duration;}}
    static Object allocate(Class<?> type)throws Exception {
        Class<?> cls=Class.forName("sun.misc.Unsafe");Field field=cls.getDeclaredField("theUnsafe");field.setAccessible(true);
        return cls.getMethod("allocateInstance",Class.class).invoke(field.get(null),type);
    }
    static void field(Object target,String name,Object value)throws Exception {Field f=target.getClass().getDeclaredField(name);f.setAccessible(true);f.set(target,value);}
    static void invoke(ItemRenderer renderer,String name,Class<?>[] types,Object...args)throws Exception {
        Method method=ItemRenderer.class.getDeclaredMethod(name,types);method.setAccessible(true);
        try{method.invoke(renderer,args);}catch(InvocationTargetException e){if(!(e.getCause() instanceof GlStateManager.Stop))throw e;}
    }
    static final Class<?>[] FLOAT={float.class},TWO_FLOAT={float.class,float.class},PLAYER_FLOAT={AbstractClientPlayer.class,float.class};
    static void pose(DataOutputStream out,int kind,float[] p,int use,int remaining,int duration,float[] matrix)throws Exception {
        out.writeInt(kind);for(float f:p)out.writeFloat(f);out.writeInt(use);out.writeInt(remaining);out.writeInt(duration);for(float f:matrix)out.writeFloat(f);
    }
    public static void main(String[] args)throws Exception {
        ItemRenderer renderer=(ItemRenderer)allocate(ItemRenderer.class);Player player=(Player)allocate(Player.class);
        HeldItem item=new HeldItem();ItemStack stack=new ItemStack(item);Minecraft minecraft=new Minecraft();minecraft.thePlayer=player;
        field(renderer,"mc",minecraft);field(renderer,"renderManager",new RenderManager());field(renderer,"itemToRender",stack);
        List<String> lines=java.nio.file.Files.readAllLines(new File(args[0]).toPath());
        ByteArrayOutputStream bytes=new ByteArrayOutputStream();DataOutputStream data=new DataOutputStream(bytes);int count=0;
        for(String line:lines){if(line.isEmpty()||line.startsWith("#"))continue;String[] fields=line.split(" ");int mode=Integer.parseInt(fields[0]);float[] p=new float[7];for(int i=0;i<7;i++)p[i]=Float.parseFloat(fields[i+1]);
            int use=Integer.parseInt(fields[8]),remaining=Integer.parseInt(fields[9]),duration=Integer.parseInt(fields[10]);player.remaining=remaining;item.duration=duration;
            player.rotationPitch=p[2];player.rotationYaw=p[3];player.prevRenderArmPitch=player.renderArmPitch=p[4];player.prevRenderArmYaw=player.renderArmYaw=p[5];
            GlStateManager.reset();invoke(renderer,"rotateWithPlayerRotations",new Class<?>[]{EntityPlayerSP.class,float.class},player,p[6]);
            if(mode==1){invoke(renderer,"renderPlayerArm",new Class<?>[]{AbstractClientPlayer.class,float.class,float.class},player,p[1],p[0]);pose(data,1,p,use,remaining,duration,GlStateManager.matrix);count++;}
            else if(mode==2){GlStateManager.stopAtMapScale=true;invoke(renderer,"renderItemMap",new Class<?>[]{AbstractClientPlayer.class,float.class,float.class,float.class},player,p[2],p[1],p[0]);
                pose(data,2,p,use,remaining,duration,GlStateManager.matrix);count++;for(int i=0;i<2;i++){pose(data,3+i,p,use,remaining,duration,GlStateManager.arms.get(i));count++;}}
            else {
                if(use<0){invoke(renderer,"doItemUsedTransformations",FLOAT,p[0]);invoke(renderer,"transformFirstPersonItem",TWO_FLOAT,p[1],p[0]);}
                else {if(use==1||use==2)invoke(renderer,"performDrinking",PLAYER_FLOAT,player,p[6]);invoke(renderer,"transformFirstPersonItem",TWO_FLOAT,p[1],0.f);
                    if(use==3)invoke(renderer,"doBlockTransformations",new Class<?>[0]);if(use==4)invoke(renderer,"doBowTransformations",new Class<?>[]{float.class,AbstractClientPlayer.class},p[6],player);}
                pose(data,0,p,use,remaining,duration,GlStateManager.matrix);count++;
            }
        }
        try(DataOutputStream out=new DataOutputStream(new FileOutputStream(args[1]))){out.writeBytes("NVFPREF1");out.writeInt(count);out.write(bytes.toByteArray());}
        // ModelRenderer geometry + ModelPlayer.setRotationAngles, independently
        // extracted for both arms, both skin models, both layers and map poses.
        try(DataOutputStream out=new DataOutputStream(new FileOutputStream(args[2]))) {
            out.writeBytes("NVARM001");out.writeInt(16);Field quads=ModelBox.class.getDeclaredField("quadList");quads.setAccessible(true);
            for(int i=0;i<16;i++) {
                boolean slim=(i&1)!=0,left=(i&2)!=0,sleeve=(i&4)!=0,map=(i&8)!=0;ModelPlayer model=new ModelPlayer(0,slim);model.heldItemRight=map?1:0;model.isChild=false;model.swingProgress=0;model.isSneak=false;
                model.setRotationAngles(0,0,0,0,0,.0625f,player);ModelRenderer part=left?(sleeve?model.bipedLeftArmwear:model.bipedLeftArm):(sleeve?model.bipedRightArmwear:model.bipedRightArm);
                GlStateManager.reset();GlStateManager.translate(part.rotationPointX*.0625f,part.rotationPointY*.0625f,part.rotationPointZ*.0625f);
                if(part.rotateAngleZ!=0)GlStateManager.rotate(part.rotateAngleZ*180.f/(float)Math.PI,0,0,1);
                if(part.rotateAngleY!=0)GlStateManager.rotate(part.rotateAngleY*180.f/(float)Math.PI,0,1,0);
                if(part.rotateAngleX!=0)GlStateManager.rotate(part.rotateAngleX*180.f/(float)Math.PI,1,0,0);
                out.writeInt(i);out.writeInt(part.cubeList.size()*36);
                for(Object obj:part.cubeList)for(TexturedQuad quad:(TexturedQuad[])quads.get(obj))for(int vertex:new int[]{0,1,2,0,2,3}) {
                    PositionTextureVertex v=quad.vertexPositions[vertex];float x=(float)v.vector3D.xCoord*.0625f,y=(float)v.vector3D.yCoord*.0625f,z=(float)v.vector3D.zCoord*.0625f;float[] m=GlStateManager.matrix;
                    out.writeFloat(m[0]*x+m[4]*y+m[8]*z+m[12]);out.writeFloat(m[1]*x+m[5]*y+m[9]*z+m[13]);out.writeFloat(m[2]*x+m[6]*y+m[10]*z+m[14]);out.writeFloat(v.texturePositionX);out.writeFloat(v.texturePositionY);
                }
            }
        }
        System.out.println("Exported "+count+" original ItemRenderer matrices and 16 ModelPlayer arm meshes");
    }
}
