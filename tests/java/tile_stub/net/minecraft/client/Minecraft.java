package net.minecraft.client;
import net.minecraft.client.renderer.texture.TextureManager;
public final class Minecraft {private static final Minecraft instance=new Minecraft();private final TextureManager textures=new TextureManager();public static Minecraft getMinecraft(){return instance;}public TextureManager getTextureManager(){return textures;}}
