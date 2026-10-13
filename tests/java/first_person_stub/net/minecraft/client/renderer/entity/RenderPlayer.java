package net.minecraft.client.renderer.entity;
import net.minecraft.client.entity.AbstractClientPlayer;
import net.minecraft.client.renderer.GlStateManager;
public final class RenderPlayer extends Render<AbstractClientPlayer> {
    public void renderRightArm(AbstractClientPlayer p){GlStateManager.arms.add(GlStateManager.matrix.clone());}
    public void renderLeftArm(AbstractClientPlayer p){GlStateManager.arms.add(GlStateManager.matrix.clone());}
}
