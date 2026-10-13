import java.io.*;
import com.google.gson.*;
import net.minecraft.enchantment.Enchantment;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Items;

public final class WorldgenLootExport {
    public static void main(String[] args)throws Exception{
        Bootstrap.register();JsonObject root=new JsonObject();root.addProperty("bookEnchantability",Items.book.getItemEnchantability());JsonArray rules=new JsonArray();
        for(Enchantment enchantment:Enchantment.enchantmentsBookList)if(enchantment!=null){JsonObject rule=new JsonObject();rule.addProperty("id",enchantment.effectId);JsonArray levels=new JsonArray();
            for(int level=enchantment.getMinLevel();level<=enchantment.getMaxLevel();++level){JsonArray values=new JsonArray();values.add(new JsonPrimitive(level));values.add(new JsonPrimitive(enchantment.getMinEnchantability(level)));values.add(new JsonPrimitive(enchantment.getMaxEnchantability(level)));levels.add(values);}
            rule.add("levels",levels);rules.add(rule);
        }
        root.add("rules",rules);try(Writer out=new FileWriter(args[0])){new GsonBuilder().setPrettyPrinting().create().toJson(root,out);}
    }
}
