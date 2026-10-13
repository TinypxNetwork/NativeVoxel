import java.io.*;
import java.lang.reflect.*;
import java.util.*;
import net.minecraft.tileentity.*;
import net.minecraft.client.renderer.*;
import net.minecraft.client.renderer.tileentity.*;
import net.minecraft.init.Blocks;
import net.minecraft.world.World;
import net.minecraft.util.BlockPos;

/** Unmodified models, ModelRenderer/display lists, TexturedQuad and tile renderers. */
public final class TileGeometryReference {
    static final class Chest extends TileEntityChest {@Override public void checkForAdjacentChests(){} }
    static void string(DataOutputStream out,String value)throws Exception{byte[] b=value.getBytes("UTF-8");out.writeInt(b.length);out.write(b);}
    public static void main(String[] args)throws Exception {
        TileEntityChestRenderer chestRenderer=new TileEntityChestRenderer();TileEntityEnderChestRenderer enderRenderer=new TileEntityEnderChestRenderer();TileEntitySkullRenderer skullRenderer=new TileEntitySkullRenderer();TileEntityBannerRenderer bannerRenderer=new TileEntityBannerRenderer();
        Field christmas=TileEntityChestRenderer.class.getDeclaredField("isChristmas");christmas.setAccessible(true);List<String> lines=java.nio.file.Files.readAllLines(new File(args[0]).toPath());
        try(DataOutputStream out=new DataOutputStream(new FileOutputStream(args[1]))) {
            out.writeBytes("NVTILREF");out.writeInt(lines.size());
            for(String line:lines){String[] f=line.split(" ");int id=Integer.parseInt(f[0]),meta=Integer.parseInt(f[1]),neighbors=Integer.parseInt(f[2]),holiday=Integer.parseInt(f[3]),stage=Integer.parseInt(f[4]),skullType=Integer.parseInt(f[5]),rotation=Integer.parseInt(f[6]);
                int x=Integer.parseInt(f[7]),y=Integer.parseInt(f[8]),z=Integer.parseInt(f[9]);long time=Long.parseLong(f[10]);float previous=Float.parseFloat(f[11]),current=Float.parseFloat(f[12]),partial=Float.parseFloat(f[13]);World world=new World();world.time=time;BlockPos pos=new BlockPos(x,y,z);
                TileRecorder.reset();
                if(id==54||id==146){Chest chest=new Chest();chest.worldObj=world;chest.pos=pos;chest.metadata=meta;chest.block=id==146?Blocks.trapped_chest:Blocks.chest;chest.prevLidAngle=previous;chest.lidAngle=current;
                    if((neighbors&1)!=0)chest.adjacentChestXPos=new Chest();if((neighbors&2)!=0)chest.adjacentChestZPos=new Chest();if((neighbors&4)!=0)chest.adjacentChestXNeg=new Chest();if((neighbors&8)!=0)chest.adjacentChestZNeg=new Chest();christmas.setBoolean(chestRenderer,holiday!=0);chestRenderer.renderTileEntityAt(chest,2.5,-.75,11.25,partial,stage);}
                else if(id==130){TileEntityEnderChest chest=new TileEntityEnderChest();chest.worldObj=world;chest.pos=pos;chest.metadata=meta;chest.block=Blocks.ender_chest;chest.prevLidAngle=previous;chest.lidAngle=current;enderRenderer.renderTileEntityAt(chest,2.5,-.75,11.25,partial,stage);}
                else if(id==144){TileEntitySkull skull=new TileEntitySkull();skull.worldObj=world;skull.pos=pos;skull.metadata=meta;skull.type=skullType;skull.rotation=rotation;skullRenderer.renderTileEntityAt(skull,2.5,-.75,11.25,partial,stage);}
                else{TileEntityBanner banner=new TileEntityBanner();banner.worldObj=world;banner.pos=pos;banner.metadata=meta;banner.block=id==176?Blocks.standing_banner:Blocks.wall_banner;bannerRenderer.renderTileEntityAt(banner,2.5,-.75,11.25,partial,stage);}
                for(int value:new int[]{id,meta,neighbors,holiday,stage,skullType,rotation,x,y,z})out.writeInt(value);out.writeLong(time);out.writeFloat(previous);out.writeFloat(current);out.writeFloat(partial);out.writeInt(TileRecorder.draws.size());
                for(TileRecorder.Quad q:TileRecorder.draws){string(out,q.texture);out.writeBoolean(q.cull);for(float v:q.matrix)out.writeFloat(v);for(float v:q.uvMatrix)out.writeFloat(v);for(float[] vertex:q.vertices)for(float v:vertex)out.writeFloat(v);}
            }
            Chest animated=new Chest();animated.worldObj=new World();animated.pos=new BlockPos(-3,64,5);out.writeInt(80);
            for(int tick=0;tick<80;tick++){int action=0,parameter=0;if(tick==0){action=1;parameter=3;}if(tick==12||tick==57){action=1;parameter=0;}if(tick==25){action=1;parameter=255;}if(tick==36){action=2;parameter=0;}animated.receiveClientEvent(action,parameter);animated.update();out.writeInt(action);out.writeInt(parameter);out.writeInt(animated.numPlayersUsing);out.writeFloat(animated.prevLidAngle);out.writeFloat(animated.lidAngle);}
        }
        System.out.println("Exported "+lines.size()+" original tile render cases and 80 original chest update ticks");
    }
}
