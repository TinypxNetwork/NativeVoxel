package net.minecraft.client.renderer;
public final class Tessellator {
    private static final Tessellator instance=new Tessellator();
    public final WorldRenderer renderer=new WorldRenderer();
    public float[] matrix;
    public static Tessellator getInstance(){return instance;}
    public WorldRenderer getWorldRenderer(){return renderer;}
    public void draw(){matrix=GlStateManager.matrix.clone();}
}
