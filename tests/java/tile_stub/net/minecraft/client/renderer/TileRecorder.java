package net.minecraft.client.renderer;
import java.util.*;
public final class TileRecorder {
    public static final class Quad {public float[] matrix,uvMatrix;public float[][] vertices;public String texture;public boolean cull;}
    public static final List<Quad> draws=new ArrayList<>();
    public static final Map<Integer,List<float[][]>> lists=new HashMap<>();
    public static int compiling=-1,nextList=1;
    public static String texture="";
    public static boolean cull=true;
    public static void reset(){draws.clear();compiling=-1;texture="";cull=true;GlStateManager.reset();}
    public static void submit(float[][] vertices){if(compiling>=0)lists.get(compiling).add(vertices);else emit(vertices);}
    static void emit(float[][] vertices){Quad q=new Quad();q.matrix=GlStateManager.model.clone();q.uvMatrix=GlStateManager.texture.clone();q.vertices=vertices;q.texture=texture;q.cull=cull;draws.add(q);}
    public static void call(int id){for(float[][] vertices:lists.get(id))emit(vertices);}
}
