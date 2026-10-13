package net.minecraft.client.renderer;
public final class Tessellator {
    private static final Tessellator instance=new Tessellator();private final WorldRenderer renderer=new WorldRenderer();
    public static Tessellator getInstance(){return instance;}
    public WorldRenderer getWorldRenderer(){return renderer;}
    public void draw(){TileRecorder.submit(renderer.vertices.toArray(new float[0][]));}
}
