package net.minecraft.client.renderer;
import net.minecraft.item.ItemStack;
import net.minecraft.client.resources.model.IBakedModel;
public final class ItemModelMesher {
    public IBakedModel model;
    public IBakedModel getItemModel(ItemStack stack){return model;}
}
