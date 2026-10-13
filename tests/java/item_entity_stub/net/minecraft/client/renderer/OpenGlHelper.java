package net.minecraft.client.renderer;
public final class OpenGlHelper {
    public static int lightmapTexUnit;
    public static float blockLight,skyLight;
    public static void setLightmapTextureCoords(int unit,float block,float sky){blockLight=block;skyLight=sky;}
}
