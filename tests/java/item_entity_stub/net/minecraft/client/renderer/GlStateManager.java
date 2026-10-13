package net.minecraft.client.renderer;
import java.util.*;
/** Only OpenGL calls are replaced; entity renderer equations run unmodified. */
public final class GlStateManager {
    public static float[] matrix;
    private static final Deque<float[]> stack=new ArrayDeque<>();
    public static void reset(){matrix=identity();stack.clear();}
    private static float[] identity(){float[] m=new float[16];for(int i=0;i<4;i++)m[i*5]=1;return m;}
    private static void multiply(float[] b){float[] r=new float[16];for(int c=0;c<4;c++)for(int y=0;y<4;y++)for(int k=0;k<4;k++)r[c*4+y]+=matrix[k*4+y]*b[c*4+k];matrix=r;}
    public static void translate(float x,float y,float z){float[] m=identity();m[12]=x;m[13]=y;m[14]=z;multiply(m);}
    public static void scale(float x,float y,float z){float[] m=identity();m[0]=x;m[5]=y;m[10]=z;multiply(m);}
    public static void rotate(float degrees,float x,float y,float z){double radians=(double)degrees*Math.PI/180.;float c=(float)Math.cos(radians),s=(float)Math.sin(radians);float[] m=identity();if(x==1){m[5]=c;m[9]=-s;m[6]=s;m[10]=c;}else if(y==1){m[0]=c;m[8]=s;m[2]=-s;m[10]=c;}else{m[0]=c;m[4]=-s;m[1]=s;m[5]=c;}multiply(m);}
    public static void pushMatrix(){stack.push(matrix.clone());}
    public static void popMatrix(){matrix=stack.pop();}
    public static void color(float r,float g,float b,float a){}
    public static void enableRescaleNormal(){}
    public static void disableRescaleNormal(){}
    public static void alphaFunc(int a,float b){}
    public static void enableBlend(){}
    public static void disableBlend(){}
    public static void tryBlendFuncSeparate(int a,int b,int c,int d){}
}
