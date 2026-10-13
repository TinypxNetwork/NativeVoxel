import java.io.*;
import java.lang.reflect.Field;
import java.util.*;
import java.awt.image.BufferedImage;
import javax.imageio.ImageIO;
import com.google.gson.*;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.block.state.IBlockState;
import net.minecraft.client.multiplayer.ChunkProviderClient;
import net.minecraft.entity.SharedMonsterAttributes;
import net.minecraft.entity.ai.attributes.AttributeModifier;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Blocks;
import net.minecraft.item.*;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.BlockPos;
import net.minecraft.world.*;
import net.minecraft.world.biome.BiomeGenBase;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.*;

/** Development-only exporter of actual vanilla registry behavior. */
public final class RegistryExport {
    static class ProbeWorld extends World {
        IBlockState state;
        ProbeWorld() {
            super(new SaveHandlerMP(), new WorldInfo(new WorldSettings(0L, WorldSettings.GameType.SURVIVAL,
                false, false, WorldType.DEFAULT), "registry_probe"), WorldProvider.getProviderForDimension(0), new Profiler(), true);
            provider.registerWorld(this); chunkProvider = createChunkProvider();
        }
        protected IChunkProvider createChunkProvider() { return new ChunkProviderClient(this); }
        protected int getRenderDistanceChunks() { return 8; }
        public IBlockState getBlockState(BlockPos pos) { return state == null ? Blocks.air.getDefaultState() : state; }
    }
    static void loadColormap(String name) throws IOException {
        try (InputStream input = RegistryExport.class.getResourceAsStream("/assets/minecraft/textures/colormap/" + name + ".png")) {
            BufferedImage image = ImageIO.read(input);
            int[] pixels = image.getRGB(0, 0, 256, 256, null, 0, 256);
            if (name.equals("grass")) ColorizerGrass.setGrassBiomeColorizer(pixels);
            else ColorizerFoliage.setFoliageBiomeColorizer(pixels);
        }
    }
    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        Map<Material, String> materials = new IdentityHashMap<>();
        for (Field field : Material.class.getFields())
            if (field.getType() == Material.class) materials.put((Material)field.get(null), field.getName());
        ProbeWorld world = new ProbeWorld();
        BlockPos origin = new BlockPos(0, 64, 0);
        JsonObject root = new JsonObject();
        JsonArray blocks = new JsonArray(), items = new JsonArray(), biomes = new JsonArray();
        for (int id = 0; id < 198; ++id) {
            Block block = Block.getBlockById(id);
            JsonObject value = new JsonObject();
            value.addProperty("id", id);
            value.addProperty("name", Block.blockRegistry.getNameForObject(block).toString());
            value.addProperty("kind", block.getClass().getName().substring(block.getClass().getName().lastIndexOf('.') + 1));
            value.addProperty("material", materials.get(block.getMaterial()));
            world.state = block.getDefaultState();
            value.addProperty("hardness", block.getBlockHardness(world, origin));
            value.addProperty("toolRequired", !block.getMaterial().isToolNotRequired());
            int replaceable = 0, valid = 0;
            for (int meta = 0; meta < 16; ++meta) {
                try {
                    world.state = block.getStateFromMeta(meta);
                    valid |= 1 << meta;
                    if (block.isReplaceable(world, origin)) replaceable |= 1 << meta;
                } catch (IllegalArgumentException ignored) {}
            }
            value.addProperty("replaceableMask", replaceable);
            value.addProperty("validMetadataMask", valid);
            value.addProperty("stepSound", block.stepSound.getStepSound());
            value.addProperty("breakSound", block.stepSound.getBreakSound());
            value.addProperty("placeSound", block.stepSound.getPlaceSound());
            value.addProperty("soundVolume", block.stepSound.getVolume());
            value.addProperty("soundPitch", block.stepSound.getFrequency());
            value.addProperty("slipperiness", block.slipperiness);
            value.addProperty("lightOpacity", block.getLightOpacity());
            value.addProperty("fullCube", block.isFullCube());
            value.addProperty("normalCube", block.isNormalCube());
            value.addProperty("fullBlock", block.isFullBlock());
            value.addProperty("materialOpaque", block.getMaterial().isOpaque());
            value.addProperty("materialBlocksMovement", block.getMaterial().blocksMovement());
            value.addProperty("materialSolid", block.getMaterial().isSolid());
            blocks.add(value);
        }
        Field edibleField = ItemFood.class.getDeclaredField("alwaysEdible");
        edibleField.setAccessible(true);
        List<Item> allItems = new ArrayList<>();
        for (Item item : Item.itemRegistry) allItems.add(item);
        Collections.sort(allItems, Comparator.comparingInt(Item::getIdFromItem));
        for (Item item : allItems) {
            int id = Item.getIdFromItem(item);
            ItemStack stack = new ItemStack(item, 1, 0);
            JsonObject value = new JsonObject();
            value.addProperty("id", id);
            value.addProperty("name", Item.itemRegistry.getNameForObject(item).toString());
            value.addProperty("kind", item.getClass().getName().substring(item.getClass().getName().lastIndexOf('.') + 1));
            value.addProperty("maxStack", item.getItemStackLimit());
            value.addProperty("maxDamage", item.getMaxDamage());
            value.addProperty("useDuration", item.getMaxItemUseDuration(stack));
            value.addProperty("useAction", item.getItemUseAction(stack).ordinal());
            value.addProperty("hasSubtypes", item.getHasSubtypes());
            value.addProperty("armorSlot", item instanceof ItemArmor ? ((ItemArmor)item).armorType : -1);
            Block itemBlock = Block.getBlockFromItem(item);
            value.addProperty("blockId", itemBlock == null ? -1 : Block.getIdFromBlock(itemBlock));
            value.addProperty("food", item instanceof ItemFood);
            value.addProperty("alwaysEdible", item instanceof ItemFood && edibleField.getBoolean(item));
            value.addProperty("healAmount", item instanceof ItemFood ? ((ItemFood)item).getHealAmount(stack) : 0);
            value.addProperty("saturationModifier", item instanceof ItemFood ? ((ItemFood)item).getSaturationModifier(stack) : 0.0f);
            JsonArray foodVariants = new JsonArray();
            if (item instanceof ItemFood) for (int metadata = 0; metadata < 16; ++metadata) {
                ItemStack variant = new ItemStack(item, 1, metadata);
                JsonObject food = new JsonObject();
                food.addProperty("healAmount", ((ItemFood)item).getHealAmount(variant));
                food.addProperty("saturationModifier", ((ItemFood)item).getSaturationModifier(variant));
                foodVariants.add(food);
            }
            value.add("foodVariants", foodVariants);
            double damage = 0;
            for (AttributeModifier modifier : item.getItemAttributeModifiers().get(SharedMonsterAttributes.attackDamage.getAttributeUnlocalizedName())) {
                if (modifier.getOperation() != 0) throw new IllegalStateException("Unexpected attack modifier operation");
                damage += modifier.getAmount();
            }
            value.addProperty("attackDamage", damage);
            JsonArray strength = new JsonArray(), harvest = new JsonArray();
            for (int blockId = 0; blockId < 198; ++blockId) {
                Block block = Block.getBlockById(blockId);
                strength.add(new JsonPrimitive(item.getStrVsBlock(stack, block)));
                harvest.add(new JsonPrimitive(item.canHarvestBlock(block)));
            }
            value.add("strength", strength); value.add("harvest", harvest);
            items.add(value);
        }
        loadColormap("grass"); loadColormap("foliage");
        for (BiomeGenBase biome : BiomeGenBase.getBiomeGenArray()) if (biome != null) {
            JsonObject value = new JsonObject();
            value.addProperty("id", biome.biomeID); value.addProperty("name", biome.biomeName);
            value.addProperty("temperature", biome.getFloatTemperature(origin));
            value.addProperty("rainfall", biome.getFloatRainfall());
            value.addProperty("grassColor", biome.getGrassColorAtPos(origin) & 0xffffff);
            value.addProperty("foliageColor", biome.getFoliageColorAtPos(origin) & 0xffffff);
            value.addProperty("waterColor", biome.waterColorMultiplier & 0xffffff);
            value.addProperty("snow", biome.getEnableSnow());
            biomes.add(value);
        }
        root.add("blocks", blocks); root.add("items", items); root.add("biomes", biomes);
        try (Writer out = new OutputStreamWriter(new FileOutputStream(args[0]), "UTF-8")) {
            new GsonBuilder().setPrettyPrinting().create().toJson(root, out);
        }
    }
}
