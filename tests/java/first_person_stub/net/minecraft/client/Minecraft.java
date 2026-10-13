package net.minecraft.client;
import net.minecraft.client.entity.EntityPlayerSP;
import net.minecraft.client.renderer.texture.TextureManager;
public final class Minecraft {
    public EntityPlayerSP thePlayer;
    private final TextureManager textures=new TextureManager();
    public TextureManager getTextureManager(){return textures;}
}
