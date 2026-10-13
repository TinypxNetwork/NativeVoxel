package net.minecraft.client.renderer.tileentity;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.util.ResourceLocation;
import net.minecraft.client.renderer.TileRecorder;
public abstract class TileEntitySpecialRenderer<T extends TileEntity> {
    protected static final ResourceLocation[] DESTROY_STAGES=new ResourceLocation[10];static{for(int i=0;i<10;i++)DESTROY_STAGES[i]=new ResourceLocation("textures/blocks/destroy_stage_"+i+".png");}
    protected void bindTexture(ResourceLocation texture){TileRecorder.texture=texture.toString();}
    public void setRendererDispatcher(TileEntityRendererDispatcher dispatcher){}
}
