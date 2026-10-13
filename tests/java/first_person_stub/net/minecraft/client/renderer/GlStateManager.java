package net.minecraft.client.renderer;
import java.util.*;
/** Headless OpenGL matrix recorder. ItemRenderer itself is the original source. */
public final class GlStateManager {
    public static float[] matrix;
    public static boolean stopAtMapScale;
    public static final List<float[]> arms=new ArrayList<>();
    private static final Deque<float[]> stack=new ArrayDeque<>();
    public static final class Stop extends RuntimeException {}
    public static void reset(){matrix=new float[16];for(int i=0;i<4;i++)matrix[i*5]=1;stack.clear();arms.clear();stopAtMapScale=false;}
    private static void multiply(float[] b){float[] r=new float[16];for(int c=0;c<4;c++)for(int y=0;y<4;y++)for(int k=0;k<4;k++)r[c*4+y]+=matrix[k*4+y]*b[c*4+k];matrix=r;}
    private static float[] identity(){float[] m=new float[16];for(int i=0;i<4;i++)m[i*5]=1;return m;}
    public static void translate(float x,float y,float z){float[] m=identity();m[12]=x;m[13]=y;m[14]=z;multiply(m);}
    public static void translate(double x,double y,double z){translate((float)x,(float)y,(float)z);}
    public static void scale(float x,float y,float z){float[] m=identity();m[0]=x;m[5]=y;m[10]=z;multiply(m);if(stopAtMapScale&&x==.015625f&&y==x&&z==x)throw new Stop();}
    public static void scale(double x,double y,double z){scale((float)x,(float)y,(float)z);}
    public static void rotate(float degrees,float x,float y,float z){double radians=(double)degrees*Math.PI/180.0;float c=(float)Math.cos(radians),s=(float)Math.sin(radians);float[] m=identity();
        if(x==1){m[5]=c;m[9]=-s;m[6]=s;m[10]=c;}else if(y==1){m[0]=c;m[8]=s;m[2]=-s;m[10]=c;}else if(z==1){m[0]=c;m[4]=-s;m[1]=s;m[5]=c;}else throw new IllegalArgumentException("Non-axis rotation");multiply(m);}
    public static void pushMatrix(){stack.push(matrix.clone());}
    public static void popMatrix(){matrix=stack.pop();}
    public static void disableCull(){}
    public static void enableCull(){}
}
