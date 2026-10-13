import java.io.*;
import net.minecraft.init.Bootstrap;
import net.minecraft.nbt.CompressedStreamTools;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.chunk.storage.AnvilChunkLoader;
import net.minecraft.world.chunk.storage.RegionFileCache;
import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.util.BlockPos;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.tileentity.TileEntityChest;
import net.minecraft.tileentity.TileEntityMobSpawner;

/** Opens a C++-generated save through the original 1.8.9 Anvil loader. */
public final class WorldIOReference {
    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        boolean tileFixture=args.length>2&&args[2].equals("--tiles");
        File directory = new File(args[0]);
        NBTTagCompound root;
        try (InputStream input = new FileInputStream(new File(directory, "level.dat"))) {
            root = CompressedStreamTools.readCompressed(input);
        }
        NBTTagCompound data = root.getCompoundTag("Data");
        if (data.getInteger("version") != 19133) throw new IOException("wrong Anvil version");
        if (data.getCompoundTag("Player").getTagList("Pos", 6).getDoubleAt(1) != 64.0) throw new IOException("wrong saved player Y");
        if(tileFixture){
            if(!data.getString("generatorName").equals("amplified")||data.getInteger("generatorVersion")!=0||data.getInteger("GameType")!=0||data.getBoolean("MapFeatures")||data.getBoolean("allowCommands")||data.getLong("DayTime")!=0)throw new IOException("wrong native generator metadata");
            NBTTagCompound player=data.getCompoundTag("Player"),abilities=player.getCompoundTag("abilities");if(player.getInteger("playerGameType")!=0||abilities.getBoolean("mayfly")||abilities.getBoolean("instabuild")||abilities.getBoolean("invulnerable")||!abilities.getBoolean("mayBuild"))throw new IOException("wrong survival abilities");
        }
        PhysicsReference.FixtureWorld world = new PhysicsReference.FixtureWorld();
        AnvilChunkLoader loader = new AnvilChunkLoader(directory);
        int chunks = 0, sections = 0, nonAir = 0,tiles=0;
        try {
            for (int z = -1; z <= 1; ++z) for (int x = -1; x <= 1; ++x) {
                Chunk chunk = loader.loadChunk(world, x, z);
                if (chunk == null) throw new IOException("missing exported chunk");
                ++chunks;
                if(tileFixture&&chunk.isTerrainPopulated())throw new IOException("unfinished native generation was marked populated");
                for (net.minecraft.world.chunk.storage.ExtendedBlockStorage section : chunk.getBlockStorageArray()) if (section != null) {
                    ++sections;
                    for (char state : section.getData()) if (state != 0) ++nonAir;
                }
                if (x == 0 && z == 0) {
                    IBlockState state = chunk.getBlockState(new BlockPos(0, 63, 0));
                    if (Block.getIdFromBlock(state.getBlock()) != 98) throw new IOException("native stone brick path decoded incorrectly");
                    if(tileFixture){
                        Object chestValue=chunk.getTileEntityMap().get(new BlockPos(0,64,0)),spawnerValue=chunk.getTileEntityMap().get(new BlockPos(1,64,0));
                        if(!(chestValue instanceof TileEntityChest)||!(spawnerValue instanceof TileEntityMobSpawner)||chunk.getTileEntityMap().containsKey(new BlockPos(2,64,0)))throw new IOException("generated/deleted tile entities loaded incorrectly");
                        TileEntityChest chest=(TileEntityChest)chestValue;if(!chest.getName().equals("\u4e16\u754c"+(char)0+"\ud83d\ude00"))throw new IOException("tile modified UTF-8 was not preserved");
                        ItemStack book=chest.getStackInSlot(0);if(book==null||Item.getIdFromItem(book.getItem())!=403||book.stackSize!=1)throw new IOException("enchanted book lost in export");
                        NBTTagCompound enchantment=book.getTagCompound().getTagList("StoredEnchantments",10).getCompoundTagAt(0);if(enchantment.getTag("id").getId()!=2||enchantment.getShort("id")!=16||enchantment.getTag("lvl").getId()!=2||enchantment.getShort("lvl")!=5)throw new IOException("book typed enchantment changed");
                        NBTTagCompound spawner=new NBTTagCompound();((TileEntityMobSpawner)spawnerValue).writeToNBT(spawner);if(!spawner.getString("EntityId").equals("Zombie")||spawner.getShort("Delay")!=20||spawner.getShort("MaxSpawnDelay")!=800)throw new IOException("spawner state changed");tiles=2;
                    }
                }
            }
        } finally { RegionFileCache.clearRegionFileReferences(); }
        try (PrintWriter out = new PrintWriter(new FileWriter(args[1]))) {
            out.printf("{\"chunks\":%d,\"sections\":%d,\"non_air_blocks\":%d,\"typed_tiles_loaded\":%d,\"vanilla_anvil_load\":true}%n", chunks, sections, nonAir,tiles);
        }
    }
}
