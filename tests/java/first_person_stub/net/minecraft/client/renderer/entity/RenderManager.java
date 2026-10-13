package net.minecraft.client.renderer.entity;
import net.minecraft.entity.Entity;
public final class RenderManager {
    @SuppressWarnings("unchecked") public <T extends Entity> Render<T> getEntityRenderObject(Entity p){return (Render<T>)new RenderPlayer();}
}
