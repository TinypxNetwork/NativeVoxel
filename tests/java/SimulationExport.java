import java.io.*;
import java.util.*;
import com.google.gson.*;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Items;
import net.minecraft.item.*;
import net.minecraft.potion.*;

/** Static vanilla potion rules exported for a native runtime. */
public final class SimulationExport {
    public static void main(String[] args)throws Exception {
        Bootstrap.register();JsonObject root=new JsonObject();JsonArray ingredients=new JsonArray(),effects=new JsonArray();
        List<Item> items=new ArrayList<>();for(Item item:Item.itemRegistry)items.add(item);items.sort((a,b)->Integer.compare(Item.getIdFromItem(a),Item.getIdFromItem(b)));
        for(Item item:items){Map<String,Integer> formulas=new LinkedHashMap<>();for(int meta=0;meta<16;++meta){ItemStack stack=new ItemStack(item,1,meta);if(item.isPotionIngredient(stack)){String formula=item.getPotionEffect(stack);formulas.put(formula,formulas.getOrDefault(formula,0)|(1<<meta));}}
            for(Map.Entry<String,Integer> formula:formulas.entrySet()){JsonObject value=new JsonObject();value.addProperty("item",Item.getIdFromItem(item));value.addProperty("mask",formula.getValue());value.addProperty("formula",formula.getKey());ingredients.add(value);}}
        Map<List<PotionEffect>,Integer> groups=new HashMap<>();
        for(int meta=0;meta<32768;++meta){List<PotionEffect> list=Items.potionitem.getEffects(meta);int id=0;if(list!=null){Integer existing=groups.get(list);if(existing==null){existing=groups.size()+1;groups.put(list,existing);}id=existing;}effects.add(new JsonPrimitive(id));}
        root.add("ingredients",ingredients);root.add("effect_groups",effects);root.addProperty("unique_nonempty_effect_groups",groups.size());
        try(Writer writer=new OutputStreamWriter(new FileOutputStream(args[0]),"UTF-8")){new GsonBuilder().setPrettyPrinting().create().toJson(root,writer);}
    }
}
