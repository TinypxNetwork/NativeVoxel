import com.google.gson.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import net.minecraft.creativetab.CreativeTabs;
import net.minecraft.init.Bootstrap;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.CompressedStreamTools;

/** Original CreativeTabs/subitems snapshot; no renderer or game server is started. */
public class CreativeCatalogExport {
    static void string(DataOutputStream out,String value) throws Exception {
        byte[] bytes=value.getBytes(StandardCharsets.UTF_8);if(bytes.length>65535)throw new IOException("String too long");out.writeShort(bytes.length);out.write(bytes);
    }
    static void stack(DataOutputStream out,ItemStack stack) throws Exception {
        if(stack==null){out.writeShort(-1);return;}
        out.writeShort(Item.getIdFromItem(stack.getItem()));out.writeByte(stack.stackSize);out.writeShort(stack.getMetadata());
        if(stack.getTagCompound()==null)out.writeByte(0);else CompressedStreamTools.write(stack.getTagCompound(),out);
    }
    public static void main(String[] args) throws Exception {
        Bootstrap.register();JsonArray report=new JsonArray();
        try(DataOutputStream out=new DataOutputStream(new FileOutputStream(args[0]))) {
            out.writeBytes("NVCREAT1");out.writeInt(CreativeTabs.creativeTabArray.length);
            for(CreativeTabs tab:CreativeTabs.creativeTabArray) {
                List<ItemStack> entries=new ArrayList<>();
                if(tab==CreativeTabs.tabAllSearch) {
                    for(Item item:Item.itemRegistry)if(item!=null&&item.getCreativeTab()!=null)item.getSubItems(item,null,entries);
                } else if(tab!=CreativeTabs.tabInventory)tab.displayAllReleventItems(entries);
                out.writeInt(tab.getTabIndex());string(out,tab.getTranslatedTabLabel());string(out,tab.getBackgroundImageName());
                out.writeBoolean(tab.drawInForegroundOfTab());out.writeBoolean(tab.shouldHidePlayerInventory());stack(out,tab.getIconItemStack());
                out.writeInt(entries.size());
                for(ItemStack item:entries) {
                    stack(out,item);string(out,item.getItem().getUnlocalizedName(item)+".name");string(out,item.getDisplayName());
                    String searchable=item.getDisplayName();
                    try{for(String line:item.getTooltip(null,false))searchable+=" "+line;}catch(Exception ignored){}
                    string(out,searchable);
                }
                JsonObject row=new JsonObject();row.addProperty("id",tab.getTabIndex());row.addProperty("key",tab.getTranslatedTabLabel());
                row.addProperty("background",tab.getBackgroundImageName());row.addProperty("items",entries.size());report.add(row);
            }
        }
        try(Writer out=new OutputStreamWriter(new FileOutputStream(args[1]),StandardCharsets.UTF_8)){new GsonBuilder().setPrettyPrinting().create().toJson(report,out);}
    }
}
