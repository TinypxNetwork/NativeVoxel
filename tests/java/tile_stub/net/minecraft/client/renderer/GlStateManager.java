package net.minecraft.client.renderer;
import java.util.*;
public final class GlStateManager {
    public static float[] model,texture;private static int mode=5888;
    private static final Deque<float[]> modelStack=new ArrayDeque<>(),textureStack=new ArrayDeque<>();
    private static float[] identity(){float[] m=new float[16];for(int i=0;i<4;i++)m[i*5]=1;return m;}
    public static void reset(){model=identity();texture=identity();mode=5888;modelStack.clear();textureStack.clear();}
    private static float[] current(){return mode==5888?model:texture;}
    private static void set(float[] value){if(mode==5888)model=value;else texture=value;}
    private static void multiply(float[] b){float[] a=current(),r=new float[16];for(int c=0;c<4;c++)for(int y=0;y<4;y++)for(int k=0;k<4;k++)r[c*4+y]+=a[k*4+y]*b[c*4+k];set(r);}
    public static void translate(float x,float y,float z){float[] m=identity();m[12]=x;m[13]=y;m[14]=z;multiply(m);}
    public static void scale(float x,float y,float z){float[] m=identity();m[0]=x;m[5]=y;m[10]=z;multiply(m);}
    public static void rotate(float degrees,float x,float y,float z){double radians=(double)degrees*Math.PI/180.;float c=(float)Math.cos(radians),s=(float)Math.sin(radians);float[] m=identity();if(x==1){m[5]=c;m[9]=-s;m[6]=s;m[10]=c;}else if(y==1){m[0]=c;m[8]=s;m[2]=-s;m[10]=c;}else{m[0]=c;m[4]=-s;m[1]=s;m[5]=c;}multiply(m);}
    public static void matrixMode(int value){if(value!=5888&&value!=5890)throw new IllegalArgumentException("Unsupported test matrix mode");mode=value;}
    public static void pushMatrix(){(mode==5888?modelStack:textureStack).push(current().clone());}
    public static void popMatrix(){set((mode==5888?modelStack:textureStack).pop());}
    public static void callList(int id){TileRecorder.call(id);}
    public static void color(float r,float g,float b,float a){}
    public static void enableRescaleNormal(){}
    public static void disableRescaleNormal(){}
    public static void enableDepth(){}
    public static void depthFunc(int value){}
    public static void depthMask(boolean value){}
    public static void disableCull(){TileRecorder.cull=false;}
    public static void enableCull(){TileRecorder.cull=true;}
    public static void enableAlpha(){}
}
