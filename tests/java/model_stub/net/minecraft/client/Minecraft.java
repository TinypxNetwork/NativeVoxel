package net.minecraft.client;
import net.minecraft.entity.Entity;
/** Only the view-entity lookup used by ModelGuardian, for headless pose tests. */
public final class Minecraft {
    private static final Minecraft instance=new Minecraft();
    public static Minecraft getMinecraft(){return instance;}
    public Entity getRenderViewEntity(){return null;}
}
