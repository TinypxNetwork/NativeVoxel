import com.google.gson.*;
import java.awt.image.BufferedImage;
import java.io.*;
import java.lang.reflect.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import javax.imageio.ImageIO;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.client.renderer.ItemModelMesher;
import net.minecraft.client.renderer.ItemMeshDefinition;
import net.minecraft.client.renderer.entity.RenderItem;
import net.minecraft.client.gui.FontRenderer;
import net.minecraft.client.resources.model.*;
import net.minecraft.world.ColorizerGrass;
import net.minecraft.world.ColorizerFoliage;

/** Development-only reference exporter. Never linked into the native client. */
public class RenderItemExport {
    static void fontWidths(String assetRoot,String target) throws Exception {
        Class<?> unsafeClass=Class.forName("sun.misc.Unsafe");
        Field instance=unsafeClass.getDeclaredField("theUnsafe");instance.setAccessible(true);
        Object unsafe=instance.get(null);
        FontRenderer font=(FontRenderer)unsafeClass.getMethod("allocateInstance",Class.class).invoke(unsafe,FontRenderer.class);
        byte[] glyphs=java.nio.file.Files.readAllBytes(new File(assetRoot+"/runtime/font/glyph_sizes.bin").toPath());
        Field glyphField=FontRenderer.class.getDeclaredField("glyphWidth");glyphField.setAccessible(true);glyphField.set(font,glyphs);
        BufferedImage image=ImageIO.read(new File(assetRoot+"/resources/assets/minecraft/textures/font/ascii.png"));
        int width=image.getWidth(),height=image.getHeight(),cellW=width/16,cellH=height/16;
        int[] pixels=image.getRGB(0,0,width,height,null,0,width),widths=new int[256];
        float scale=8.0F/cellW;
        for(int i=0;i<256;i++) {
            int right;
            for(right=cellW-1;right>=0;right--) {
                boolean empty=true;
                for(int y=0;y<cellH&&empty;y++)if((pixels[(i/16*cellW+y)*width+i%16*cellW+right]>>>24)!=0)empty=false;
                if(!empty)break;
            }
            widths[i]=(int)(0.5D+(double)((float)(right+1)*scale))+1;
        }
        Field widthField=FontRenderer.class.getDeclaredField("charWidth");widthField.setAccessible(true);widthField.set(font,widths);
        try(OutputStream out=new FileOutputStream(target)) {
            for(boolean unicode:new boolean[]{false,true}) {
                font.setUnicodeFlag(unicode);
                for(int code=0;code<65536;code++)out.write(font.getCharWidth((char)code)&255);
            }
        }
    }
    static JsonArray colors(Item item,int metadata) {
        ItemStack stack=new ItemStack(item,1,metadata);
        JsonArray result=new JsonArray();
        for(int tint=0;tint<5;tint++)result.add(new JsonPrimitive(item.getColorFromItemStack(stack,tint)&0xffffff));
        return result;
    }
    static JsonObject mapping(Item item,int metadata,ModelResourceLocation model) {
        JsonObject row=new JsonObject();
        row.addProperty("id",Item.getIdFromItem(item));row.addProperty("meta",metadata);
        row.addProperty("model",model.getResourceDomain()+":"+model.getResourcePath());
        ItemStack stack=new ItemStack(item,1,metadata);
        row.addProperty("translationKey",item.getUnlocalizedName(stack)+".name");
        row.addProperty("displayName",item.getItemStackDisplayName(stack));
        row.add("colors",colors(item,metadata));return row;
    }
    @SuppressWarnings("unchecked")
    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        for(String name:new String[]{"grass","foliage"}) {
            BufferedImage image=ImageIO.read(new File(args[1]+"/resources/assets/minecraft/textures/colormap/"+name+".png"));
            int[] pixels=image.getRGB(0,0,image.getWidth(),image.getHeight(),null,0,image.getWidth());
            if(name.equals("grass"))ColorizerGrass.setGrassBiomeColorizer(pixels);
            else ColorizerFoliage.setFoliageBiomeColorizer(pixels);
        }
        ModelManager manager=new ModelManager(null) {
            @Override public IBakedModel getModel(ModelResourceLocation location){return null;}
        };
        RenderItem renderer=new RenderItem(null,manager);
        ItemModelMesher mesher=renderer.getItemModelMesher();
        Field simpleField=ItemModelMesher.class.getDeclaredField("simpleShapes");simpleField.setAccessible(true);
        Map<Integer,ModelResourceLocation> simple=(Map<Integer,ModelResourceLocation>)simpleField.get(mesher);
        Field shaperField=ItemModelMesher.class.getDeclaredField("shapers");shaperField.setAccessible(true);
        Map<Item,ItemMeshDefinition> shapers=(Map<Item,ItemMeshDefinition>)shaperField.get(mesher);
        JsonObject result=new JsonObject();JsonArray mappings=new JsonArray(),dynamic=new JsonArray(),items=new JsonArray();
        for(Map.Entry<Integer,ModelResourceLocation> entry:new TreeMap<Integer,ModelResourceLocation>(simple).entrySet()) {
            int id=entry.getKey()>>>16,meta=entry.getKey()&65535;
            mappings.add(mapping(Item.getItemById(id),meta,entry.getValue()));
        }
        TreeMap<Integer,ItemMeshDefinition> sortedShapers=new TreeMap<>();
        for(Map.Entry<Item,ItemMeshDefinition> entry:shapers.entrySet())sortedShapers.put(Item.getIdFromItem(entry.getKey()),entry.getValue());
        for(Map.Entry<Integer,ItemMeshDefinition> entry:sortedShapers.entrySet()) {
            int id=entry.getKey();Item item=Item.getItemById(id);
            JsonObject record=new JsonObject();record.addProperty("id",id);
            record.addProperty("name",Item.itemRegistry.getNameForObject(item).toString());
            record.addProperty("default",true);
            if(item==Items.potionitem) {
                record.addProperty("kind","potion");
                record.addProperty("normal",entry.getValue().getModelLocation(new ItemStack(item,1,0)).getResourcePath());
                record.addProperty("splash",entry.getValue().getModelLocation(new ItemStack(item,1,16384)).getResourcePath());
                JsonArray palette=new JsonArray();
                for(int meta=0;meta<65536;meta++)palette.add(new JsonPrimitive(item.getColorFromItemStack(new ItemStack(item,1,meta),0)&0xffffff));
                record.add("palette",palette);
            } else {
                record.addProperty("kind","generic");
                int count=item==Items.spawn_egg?256:item==Items.banner?16:1;
                for(int meta=0;meta<count;meta++)mappings.add(mapping(item,meta,entry.getValue().getModelLocation(new ItemStack(item,1,meta))));
            }
            dynamic.add(record);
        }
        for(Item item:Item.itemRegistry) {
            JsonObject row=new JsonObject();row.addProperty("id",Item.getIdFromItem(item));
            row.addProperty("name",Item.itemRegistry.getNameForObject(item).toString());
            row.addProperty("maxDamage",item.getMaxDamage());row.addProperty("hasSubtypes",item.getHasSubtypes());
            ItemStack stack=new ItemStack(item,1,0);
            row.addProperty("translationKey",item.getUnlocalizedName(stack)+".name");
            row.addProperty("displayName",item.getItemStackDisplayName(stack));
            items.add(row);
        }
        result.add("mappings",mappings);result.add("dynamic",dynamic);result.add("items",items);
        try(Writer writer=new OutputStreamWriter(new FileOutputStream(args[0]),StandardCharsets.UTF_8)) {
            new GsonBuilder().disableHtmlEscaping().create().toJson(result,writer);
        }
        fontWidths(args[1],args[2]);
    }
}
