#!/usr/bin/env python3
"""Record the unedited GuiMainMenu panorama methods for the GPU regression."""
import hashlib
import json
import subprocess
from pathlib import Path


def method(source, name):
    start = source.index("    private void " + name + "(")
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


STUB = r'''
import java.io.*;
import java.util.*;
public class PanoramaReference {
    int width, height, panoramaTimer;
    float zLevel;
    MC mc=new MC();
    static final int[] titlePanoramaPaths={0,1,2,3,4,5};
    int backgroundTexture=6;
    static PrintWriter out;
    static class Matrix {
        float[] v=new float[16];
        Matrix(){v[0]=v[5]=v[10]=v[15]=1;}
        Matrix times(Matrix b){Matrix c=new Matrix();Arrays.fill(c.v,0);
            for(int j=0;j<4;j++)for(int i=0;i<4;i++)for(int k=0;k<4;k++)c.v[j*4+i]+=v[k*4+i]*b.v[j*4+k];return c;}
    }
    static class GlStateManager {
        static Matrix projection=new Matrix(),model=new Matrix();static int mode=5888;
        static ArrayDeque<Matrix> ps=new ArrayDeque<>(),ms=new ArrayDeque<>();
        static Matrix current(){return mode==5889?projection:model;}
        static void set(Matrix m){if(mode==5889)projection=m;else model=m;}
        static void multiply(Matrix m){set(current().times(m));}
        static void matrixMode(int m){mode=m;}
        static void pushMatrix(){(mode==5889?ps:ms).push(current());}
        static void popMatrix(){set((mode==5889?ps:ms).pop());}
        static void loadIdentity(){set(new Matrix());}
        static void translate(float x,float y,float z){Matrix m=new Matrix();m.v[12]=x;m.v[13]=y;m.v[14]=z;multiply(m);}
        static void rotate(float angle,float x,float y,float z){Matrix m=new Matrix();double radians=angle*Math.PI/180;
            float c=(float)Math.cos(radians),s=(float)Math.sin(radians);
            if(x!=0){m.v[5]=c;m.v[6]=s;m.v[9]=-s;m.v[10]=c;}
            if(y!=0){m.v[0]=c;m.v[2]=-s;m.v[8]=s;m.v[10]=c;}
            if(z!=0){m.v[0]=c;m.v[1]=s;m.v[4]=-s;m.v[5]=c;}multiply(m);}
        static void viewport(int x,int y,int w,int h){out.println("V "+w+" "+h);}
        static void color(float r,float g,float b,float a){}
        static void colorMask(boolean r,boolean g,boolean b,boolean a){}
        static void enableBlend(){}static void disableAlpha(){}static void enableAlpha(){}
        static void disableCull(){}static void enableCull(){}static void enableDepth(){}
        static void depthMask(boolean b){}static void tryBlendFuncSeparate(int a,int b,int c,int d){}
    }
    static class Project {
        static void gluPerspective(float fov,float aspect,float near,float far){Matrix m=new Matrix();Arrays.fill(m.v,0);
            double half=fov*Math.PI/360;float cot=(float)(Math.cos(half)/Math.sin(half));
            m.v[0]=cot/aspect;m.v[5]=cot;m.v[10]=-(far+near)/(far-near);m.v[11]=-1;m.v[14]=-2*far*near/(far-near);GlStateManager.multiply(m);}
    }
    static class MathHelper {
        static final float[] SIN_TABLE=new float[65536];static {for(int i=0;i<65536;i++)SIN_TABLE[i]=(float)Math.sin((double)i*Math.PI*2/65536);}
        static float sin(float value){return SIN_TABLE[(int)(value*10430.378F)&65535];}
    }
    static class GL11 {
        static final int GL_TEXTURE_2D=3553,GL_TEXTURE_MIN_FILTER=10241,GL_TEXTURE_MAG_FILTER=10240,GL_LINEAR=9729;
        static void glTexParameteri(int a,int b,int c){out.println("F "+b+" "+c);}
        static void glCopyTexSubImage2D(int a,int b,int c,int d,int e,int f,int g,int h){out.println("C");}
    }
    static class DefaultVertexFormats {static int POSITION_TEX_COLOR;}
    static class MC {
        int displayWidth,displayHeight;TextureManager texture=new TextureManager();Framebuffer framebuffer=new Framebuffer();
        TextureManager getTextureManager(){return texture;}Framebuffer getFramebuffer(){return framebuffer;}
    }
    static class TextureManager {void bindTexture(int texture){out.println("B "+texture);}}
    static class Framebuffer {void unbindFramebuffer(){}void bindFramebuffer(boolean b){}}
    static class WorldRenderer {
        ArrayList<float[]> vertices=new ArrayList<>();double x,y,z,u,v;int r,g,b,a;
        void begin(int mode,int format){vertices.clear();}
        WorldRenderer pos(double xx,double yy,double zz){x=xx;y=yy;z=zz;return this;}
        WorldRenderer tex(double uu,double vv){u=uu;v=vv;return this;}
        WorldRenderer color(int rr,int gg,int bb,int aa){r=rr;g=gg;b=bb;a=aa;return this;}
        WorldRenderer color(float r,float g,float b,float a){return color((int)(r*255),(int)(g*255),(int)(b*255),(int)(a*255));}
        void endVertex(){float[] p={(float)x,(float)y,(float)z,1},q=new float[10];
            Matrix m=GlStateManager.projection.times(GlStateManager.model);
            for(int i=0;i<4;i++)for(int k=0;k<4;k++)q[i]+=m.v[k*4+i]*p[k];
            q[4]=(float)u;q[5]=(float)v;q[6]=r;q[7]=g;q[8]=b;q[9]=a;vertices.add(q);}
        void setTranslation(double x,double y,double z){}
    }
    static class Tessellator {
        static Tessellator instance=new Tessellator();WorldRenderer renderer=new WorldRenderer();
        static Tessellator getInstance(){return instance;}WorldRenderer getWorldRenderer(){return renderer;}
        void draw(){out.println("D "+renderer.vertices.size());for(float[] v:renderer.vertices){for(float f:v)out.print(Float.toString(f)+" ");out.println();}}
    }
    public static void main(String[] args)throws Exception {
        PanoramaReference menu=new PanoramaReference();menu.width=Integer.parseInt(args[1]);menu.height=Integer.parseInt(args[2]);
        float ticks=Float.parseFloat(args[3])*20;menu.panoramaTimer=(int)ticks;
        menu.mc.displayWidth=menu.width;menu.mc.displayHeight=menu.height;
        // EntityRenderer.setupOverlayRendering: ortho(0,w,h,0,1000,3000),
        // followed by model-view translation (0,0,-2000).
        Matrix projection=new Matrix();projection.v[0]=2.f/menu.width;projection.v[5]=-2.f/menu.height;projection.v[10]=-.001f;
        projection.v[12]=-1;projection.v[13]=1;projection.v[14]=-2;GlStateManager.projection=projection;
        GlStateManager.model.v[14]=-2000;
        try(PrintWriter file=new PrintWriter(args[0],"UTF-8")){out=file;out.println("P "+menu.width+" "+menu.height+" "+args[3]);menu.renderSkybox(0,0,ticks-menu.panoramaTimer);}
    }
    // ORIGINAL_METHODS
}
'''


def main():
    root = Path(__file__).resolve().parents[1]
    original = root.parent / "MCP-919-main/src/minecraft/net/minecraft/client/gui/GuiMainMenu.java"
    source = original.read_text(encoding="utf-8")
    methods = "\n".join(method(source, name) for name in ("drawPanorama", "rotateAndBlurSkybox", "renderSkybox"))
    build = root / "build/panorama_reference"
    build.mkdir(parents=True, exist_ok=True)
    harness = build / "PanoramaReference.java"
    harness.write_text(STUB.replace("    // ORIGINAL_METHODS", methods), encoding="utf-8")
    subprocess.run(["javac", "--release", "8", "-encoding", "UTF-8", "-d", str(build), str(harness)], check=True)
    cases = [(1280, 720, 0), (1280, 720, 23.75), (640, 640, 123.125), (320, 640, 7.375)]
    for i, (width, height, seconds) in enumerate(cases):
        subprocess.run(["java", "-cp", str(build), "PanoramaReference", str(build / f"case{i}.trace"),
                        str(width), str(height), str(seconds)], check=True)
    report = {"java_source": str(original), "java_sha256": hashlib.sha256(original.read_bytes()).hexdigest(),
              "methods": ["drawPanorama", "rotateAndBlurSkybox", "renderSkybox"], "cases": cases,
              "note": "Replay generated commands with tests/render_smoke.cpp for GPU pixel comparison."}
    (build / "source.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Recorded {len(cases)} panorama command streams from unchanged Java method bodies")


if __name__ == "__main__":
    main()
