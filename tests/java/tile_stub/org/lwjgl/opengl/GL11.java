package org.lwjgl.opengl;
import java.util.*;
import net.minecraft.client.renderer.TileRecorder;
public final class GL11 {
    public static int glGenLists(int count){int value=TileRecorder.nextList;TileRecorder.nextList+=count;return value;}
    public static void glNewList(int list,int mode){TileRecorder.compiling=list;TileRecorder.lists.put(list,new ArrayList<float[][]>());}
    public static void glEndList(){TileRecorder.compiling=-1;}
    public static int glGetError(){return 0;}
    public static void glDeleteLists(int list,int count){for(int i=0;i<count;i++)TileRecorder.lists.remove(list+i);}
}
