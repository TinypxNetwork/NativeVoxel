package net.minecraft.client.renderer;
import java.util.*;
import net.minecraft.client.renderer.vertex.VertexFormat;
public final class WorldRenderer {
    public final List<float[]> vertices=new ArrayList<>();
    public int color;
    private float[] next;
    public void begin(int mode,VertexFormat format){vertices.clear();}
    public WorldRenderer pos(double x,double y,double z){next=new float[8];next[0]=(float)x;next[1]=(float)y;next[2]=(float)z;return this;}
    public WorldRenderer tex(double u,double v){next[3]=(float)u;next[4]=(float)v;return this;}
    public WorldRenderer color(int r,int g,int b,int a){color=(a<<24)|(r<<16)|(g<<8)|b;return this;}
    public WorldRenderer normal(float x,float y,float z){next[5]=x;next[6]=y;next[7]=z;return this;}
    public void endVertex(){vertices.add(next);}
}
