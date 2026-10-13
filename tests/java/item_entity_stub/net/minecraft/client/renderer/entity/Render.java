package net.minecraft.client.renderer.entity;
import net.minecraft.entity.Entity;
public class Render<T extends Entity> {
    protected RenderManager renderManager;
    protected float shadowSize,shadowOpaque;
    public Render(RenderManager manager){renderManager=manager;}
    protected boolean bindEntityTexture(T entity){return false;}
    public void doRender(T entity,double x,double y,double z,float yaw,float partialTicks){}
}
