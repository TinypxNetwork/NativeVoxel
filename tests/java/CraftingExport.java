import java.io.*;
import java.lang.reflect.Field;
import java.util.*;
import com.google.gson.*;
import net.minecraft.init.Bootstrap;
import net.minecraft.item.*;
import net.minecraft.item.crafting.*;
import net.minecraft.nbt.CompressedStreamTools;
import net.minecraft.tileentity.TileEntityFurnace;
import net.minecraft.enchantment.Enchantment;

/** Development exporter; production recipe matching is native C++. */
public final class CraftingExport {
    static Object field(Object object,Class<?> owner,String name)throws Exception {
        Field field=owner.getDeclaredField(name);field.setAccessible(true);return field.get(object);
    }
    static JsonObject stack(ItemStack stack)throws Exception {
        JsonObject result=new JsonObject();
        result.addProperty("id",stack==null?-1:Item.getIdFromItem(stack.getItem()));
        result.addProperty("count",stack==null?0:stack.stackSize);
        result.addProperty("damage",stack==null?0:stack.getMetadata());
        StringBuilder hex=new StringBuilder();
        if(stack!=null&&stack.hasTagCompound()) {
            ByteArrayOutputStream output=new ByteArrayOutputStream();
            CompressedStreamTools.write(stack.getTagCompound(),new DataOutputStream(output));
            for(byte b:output.toByteArray())hex.append(String.format(Locale.ROOT,"%02x",b&255));
        }
        result.addProperty("nbt",hex.toString());return result;
    }
    public static void main(String[] args)throws Exception {
        Bootstrap.register();
        JsonObject result=new JsonObject();JsonArray recipes=new JsonArray(),smelting=new JsonArray(),items=new JsonArray(),enchantments=new JsonArray();
        int index=0;
        for(IRecipe recipe:CraftingManager.getInstance().getRecipeList()) {
            JsonObject value=new JsonObject();value.addProperty("index",index++);
            value.addProperty("kind",recipe.getClass().getSimpleName());
            JsonArray inputs=new JsonArray();int width=0,height=0;boolean copyNbt=false;
            if(recipe.getClass()==ShapedRecipes.class) {
                width=(Integer)field(recipe,ShapedRecipes.class,"recipeWidth");height=(Integer)field(recipe,ShapedRecipes.class,"recipeHeight");
                for(ItemStack item:(ItemStack[])field(recipe,ShapedRecipes.class,"recipeItems"))inputs.add(stack(item));
                copyNbt=(Boolean)field(recipe,ShapedRecipes.class,"copyIngredientNBT");
            } else if(recipe.getClass()==ShapelessRecipes.class) {
                for(Object item:(List<?>)field(recipe,ShapelessRecipes.class,"recipeItems"))inputs.add(stack((ItemStack)item));
            }
            value.addProperty("width",width);value.addProperty("height",height);value.addProperty("copy_nbt",copyNbt);
            value.add("inputs",inputs);value.add("output",stack(recipe.getRecipeOutput()));recipes.add(value);
        }
        for(Map.Entry<ItemStack,ItemStack> entry:FurnaceRecipes.instance().getSmeltingList().entrySet()) {
            JsonObject value=new JsonObject();value.add("input",stack(entry.getKey()));value.add("output",stack(entry.getValue()));
            value.addProperty("experience",FurnaceRecipes.instance().getSmeltingExperience(entry.getValue()));smelting.add(value);
        }
        List<Item> registered=new ArrayList<>();for(Item item:Item.itemRegistry)registered.add(item);
        Collections.sort(registered,new Comparator<Item>(){public int compare(Item a,Item b){return Integer.compare(Item.getIdFromItem(a),Item.getIdFromItem(b));}});
        for(Item item:registered) {
            JsonObject value=new JsonObject();value.addProperty("id",Item.getIdFromItem(item));
            value.addProperty("container",item.hasContainerItem()?Item.getIdFromItem(item.getContainerItem()):-1);
            value.addProperty("enchantability",item.getItemEnchantability());
            JsonArray fuel=new JsonArray(),repairs=new JsonArray();int potion=0;
            for(int meta=0;meta<16;++meta) {
                ItemStack stack=new ItemStack(item,1,meta);fuel.add(new JsonPrimitive(TileEntityFurnace.getItemBurnTime(stack)));
                if(item.isPotionIngredient(stack))potion|=1<<meta;
            }
            for(Item material:registered)if(item.getIsRepairable(new ItemStack(item),new ItemStack(material)))repairs.add(new JsonPrimitive(Item.getIdFromItem(material)));
            value.add("fuel",fuel);value.add("repairs",repairs);value.addProperty("potion_mask",potion);items.add(value);
        }
        for(int id=0;id<256;++id){Enchantment enchantment=Enchantment.getEnchantmentById(id);if(enchantment==null)continue;
            JsonObject value=new JsonObject();value.addProperty("id",id);value.addProperty("max_level",enchantment.getMaxLevel());value.addProperty("weight",enchantment.getWeight());
            JsonArray applies=new JsonArray(),compatible=new JsonArray();
            for(Item item:registered)if(enchantment.canApply(new ItemStack(item)))applies.add(new JsonPrimitive(Item.getIdFromItem(item)));
            for(int other=0;other<256;++other){Enchantment candidate=Enchantment.getEnchantmentById(other);if(candidate==null||enchantment.canApplyTogether(candidate))compatible.add(new JsonPrimitive(other));}
            value.add("applies",applies);value.add("compatible",compatible);enchantments.add(value);}
        result.add("recipes",recipes);result.add("smelting",smelting);result.add("items",items);result.add("enchantments",enchantments);
        try(Writer writer=new OutputStreamWriter(new FileOutputStream(args[0]),"UTF-8")){new GsonBuilder().setPrettyPrinting().create().toJson(result,writer);}
    }
}
