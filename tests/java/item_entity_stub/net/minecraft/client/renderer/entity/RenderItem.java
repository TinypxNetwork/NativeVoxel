package net.minecraft.client.renderer.entity;
import java.util.*;
import net.minecraft.client.renderer.*;
import net.minecraft.item.ItemStack;
import net.minecraft.client.resources.model.IBakedModel;
public final class RenderItem {
    public final ItemModelMesher mesher=new ItemModelMesher();
    public final List<float[]> matrices=new ArrayList<>();
    public ItemModelMesher getItemModelMesher(){return mesher;}
    public void renderItem(ItemStack stack,IBakedModel model){matrices.add(GlStateManager.matrix.clone());}
}
