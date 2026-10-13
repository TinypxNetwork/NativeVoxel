#include "mc/local_session.hpp"
#include "mc/controller.hpp"
#include "mc/item_nbt.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
using namespace mc;
int checks = 0;
void require(bool value, const std::string &message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
ItemStack item(int id, int count = 1, int meta = 0) {
    ItemStack stack;
    stack.id = static_cast<std::int16_t>(id);
    stack.count = static_cast<std::int8_t>(count);
    stack.damage = static_cast<std::int16_t>(meta);
    return stack;
}
std::uint64_t key(int x, int y, int z) {
    return (std::uint64_t(std::uint32_t(x) & 0x3ffffff) << 38) | (std::uint64_t(std::uint32_t(y) & 4095) << 26) |
           (std::uint32_t(z) & 0x3ffffff);
}
nbt::Tag tile(const worldio::SaveInfo &info, int x, int y, int z) {
    for (auto it = info.blockEntityUpdates.rbegin(); it != info.blockEntityUpdates.rend(); ++it)
        if (it->x == x && it->y == y && it->z == z)
            return it->data;
    return {};
}
void floor(Game &game) {
    for (int z = -8; z <= 8; ++z)
        for (int x = -8; x <= 8; ++x)
            game.world.setBlock(x, 63, z, blockState(1));
    game.player.teleport(.5, 64, .5);
    game.player.onGround = true;
}
void copyFixture(const fs::path &source, const fs::path &destination) {
    fs::create_directory(destination);
    for (const auto &entry : fs::recursive_directory_iterator(source)) {
        const auto target = destination / fs::relative(entry.path(), source);
        if (entry.is_directory())
            fs::create_directory(target);
        else {
            // This GCC 8 Windows runtime's filesystem::copy_file uses text
            // translation. Fixtures, like production NBT IO, use binary streams.
            std::ifstream input(entry.path(), std::ios::binary);
            std::ofstream output(target, std::ios::binary);
            output << input.rdbuf();
            output.close();
            require(input.good() && output.good() && fs::file_size(target) == entry.file_size(), "binary fixture copy");
        }
    }
}
void exportVisual(const std::string &directory, int selected) {
    Game game;
    for (int z = -24; z <= 24; ++z)
        for (int x = -24; x <= 24; ++x)
            game.world.setBlock(x, 63, z, blockState(2));
    game.player.teleport(.5, 64, -7.5);
    game.player.pitch = 8;
    game.player.onGround = true;
    LocalSession local;
    local.attachPreview(game, 1, "VisualTest");
    local.simulation.weather.dayTime = game.worldTime = 6000;
    local.simulation.settings.weatherCycle = false;
    const int ids[] = {276, 95, 261, 322, 54, 397, 425, 373, 0};
    for (int slot = 0; slot < 8; ++slot)
        game.inventory[36 + slot] = item(ids[slot], 1, slot == 1 ? 14 : 0);
    game.selectedSlot = std::clamp(selected, 0, 8);
    for (const auto p : {std::pair<int, int>{0, 54}, {1, 54}, {3, 130}, {5, 61}})
        game.world.setBlock(p.first, 64, 3, blockState(p.second, 2));
    game.world.setBlock(-2, 64, 3, blockState(176, 8));
    game.world.setBlock(-4, 64, 3, blockState(144, 1));
    game.world.setBlock(-6, 64, 3, blockState(63, 8));
    auto put = [&](int x, const char *id) {
        nbt::Tag tag;
        tag.type = 10;
        tag.compound["id"] = nbt::text(id);
        tag.compound["x"] = nbt::integer(3, x);
        tag.compound["y"] = nbt::integer(3, 64);
        tag.compound["z"] = nbt::integer(3, 3);
        return tag;
    };
    auto banner = put(-2, "Banner");
    banner.compound["Base"] = nbt::integer(3, 2);
    nbt::Tag patterns;
    patterns.type = 9;
    patterns.subtype = 10;
    for (const auto &p : {std::pair<const char *, int>{"cre", 0}, {"bo", 15}}) {
        nbt::Tag design;
        design.type = 10;
        design.compound["Pattern"] = nbt::text(p.first);
        design.compound["Color"] = nbt::integer(3, p.second);
        patterns.list.push_back(std::move(design));
    }
    banner.compound["Patterns"] = std::move(patterns);
    game.tileEntities[key(-2, 64, 3)] = {-2, 64, 3, 0, nbt::encode(banner)};
    auto skull = put(-4, "Skull");
    skull.compound["SkullType"] = nbt::integer(1, 2);
    skull.compound["Rot"] = nbt::integer(1, 8);
    game.tileEntities[key(-4, 64, 3)] = {-4, 64, 3, 0, nbt::encode(skull)};
    auto sign = put(-6, "Sign");
    for (int line = 1; line <= 4; ++line)
        sign.compound["Text" + std::to_string(line)] =
            nbt::text(line == 1 ? "{\"text\":\"Native 1.8.9\"}" : "{\"text\":\"\"}");
    game.tileEntities[key(-6, 64, 3)] = {-6, 64, 3, 0, nbt::encode(sign)};
    local.simulation.furnaceAt(5, 64, 3).slots[0] = item(15, 5);
    local.simulation.furnaceAt(5, 64, 3).slots[1] = item(263, 8);
    local.simulation.spawnItem(game, item(264, 50), -1.2, 64.8, -.5, 32767);
    local.simulation.spawnItem(game, item(95, 3, 11), 2, 64.8, -.5, 32767);
    local.simulation.spawnExperience(game, 7, .5, 64.5, -.5);
    for (const auto &column : game.world.chunks())
        for (const auto &section : column.second.sections)
            if (section)
                section->skyLight.fill(0xff);
    std::string error;
    require(local.saveNew(game, directory, error), error);
    std::cout << "Visual fixture: " << directory << '\n';
}
void creation() {
    Game game;
    floor(game);
    LocalSession local;
    local.attachPreview(game, 0, "LocalTest");
    game.inventory[36] = item(264, 3);
    const auto before = game.world.revision();
    worldgen::Settings settings;
    settings.mapFeatures = false;
    std::string error;
    require(!local.create(game, 189, settings, 2, 4, "LocalTest", error), "invalid mode rejected");
    require(local.active() && game.world.revision() == before && game.inventory[36].count == 3,
            "invalid creation leaves the current session attached");
    int progress = 0;
    require(!local.create(game, 189, settings, 2, 0, "LocalTest", error,
                          [&](int done, int total) {
                              require(done == 1 && total == 25, "creation reports completed columns");
                              ++progress;
                              return false;
                          }),
            "creation cancellation reported");
    require(progress == 1 && local.active() && game.world.revision() == before && game.inventory[36].id == 264,
            "cancelled generation does not replace the active world");
    for (const int dimension : {0, -1, 1}) {
        settings.dimension = worldgen::Dimension(dimension);
        require(local.create(game, 189, settings, 2, 1, "LocalTest", error), error);
        require(local.active() && game.dimension == dimension && game.world.chunks().size() == 25 && game.creativeMode,
                "new dimension is fully installed with native authority");
        require(game.world.collisions(game.player.bounds()).empty(), "spawn has player headroom");
        const auto ticks = game.ticks, playerTicks = game.player.ticks;
        local.tick(game, {}, 2);
        require(game.ticks == ticks + 1 && game.player.ticks == playerTicks + 1 && local.simulation.ticks() == 1,
                "LocalSession owns precisely one player tick");
        require(game.worldAge == 1 && local.saveInfo.generation && !local.saveInfo.generation->terrainPopulated,
                "clock advances and base terrain is not claimed as populated");
    }
    local.detach(game);
    require(!local.active() && !game.offlineBlockInteraction && !game.offlineBlockBreak && !game.offlineItemDrop &&
                !game.offlineItemDrops && !game.offlineHeldItemDrop && !game.offlineCreativeDrop &&
                !game.beforeOfflineContainerClose,
            "all local callbacks detach before an online connection");
    const auto ticks = game.ticks;
    game.online = true;
    bool rejected = false;
    try {
        local.tick(game, {}, 2);
    } catch (const std::logic_error &) {
        rejected = true;
    }
    require(rejected && game.ticks == ticks, "detached authority cannot tick an online game");
    game.online = false;
}
void persistence(const fs::path &base) {
    std::string error;
    Game game;
    floor(game);
    LocalSession local;
    local.attachPreview(game, 0, "LocalTest");
    local.simulation.settings.weatherCycle = false;
    game.inventory[36] = item(278);
    game.world.setBlock(1, 64, 0, blockState(1));
    require(game.offlineBlockBreak(1, 64, 0, 4), "local break routed to authority");
    require(game.world.getBlock(1, 64, 0) == 0 && game.inventory[36].damage == 1 &&
                local.simulation.droppedEntities().size() == 1,
            "break damages the tool once and creates one item entity");
    for (int i = 0; i < 35; ++i)
        local.tick(game, {}, 2);
    require(local.simulation.droppedEntities().empty() && game.inventory[37].id == 4,
            "block drop enters inventory once");

    game.world.setBlock(2, 64, 0, blockState(61, 2));
    require(game.offlineBlockInteraction(2, 64, 0) && game.window.type == "minecraft:furnace",
            "furnace GUI uses authority");
    game.window.slots[0] = item(15);
    game.window.slots[1] = item(263);
    game.closeContainer();
    require(local.simulation.furnaceAt(2, 64, 0).slots[0].id == 15, "close commits input before the next tick");
    for (int i = 0; i < 200; ++i)
        local.tick(game, {}, 2);
    require(local.simulation.furnaceAt(2, 64, 0).slots[2].id == 265, "closed furnace continues smelting");

    game.world.setBlock(3, 64, 0, blockState(117));
    require(game.offlineBlockInteraction(3, 64, 0), "brewing GUI opens");
    game.window.slots[0] = item(373);
    game.window.slots[3] = item(372);
    game.closeContainer();
    for (int i = 0; i < 401; ++i)
        local.tick(game, {}, 2);
    require(local.simulation.brewingAt(3, 64, 0).slots[0].damage == 16, "closed stand brews awkward potion");

    game.world.setBlock(4, 64, 0, blockState(54, 2));
    game.world.setBlock(5, 64, 0, blockState(54, 2));
    require(game.offlineBlockInteraction(5, 64, 0) && game.window.slots.size() == 90,
            "double chest opens from east half");
    game.window.slots[0] = item(264, 11);
    game.window.slots[27] = item(388, 5);
    game.closeContainer();
    require(game.offlineBlockInteraction(4, 64, 0) && game.window.slots[0].id == 264 && game.window.slots[27].id == 388,
            "either half exposes the same vanilla west/east slot order");
    game.closeContainer();
    require(game.blockActions.back().parameter == 0, "closing publishes a chest lid action");
    game.world.setBlock(4, 65, 0, blockState(1));
    require(game.offlineBlockInteraction(5, 64, 0) && !game.window.open, "solid block over either half blocks opening");
    game.world.setBlock(4, 65, 0, 0);

    game.world.setBlock(-2, 64, 0, blockState(54, 3));
    game.offlineBlockInteraction(-2, 64, 0);
    game.closeContainer();
    auto locked = nbt::parse(game.tileEntities.at(key(-2, 64, 0)).nbt);
    locked.compound["Lock"] = nbt::text("test-key");
    game.tileEntities.at(key(-2, 64, 0)).nbt = nbt::encode(locked);
    require(game.offlineBlockInteraction(-2, 64, 0) && !game.window.open, "locked chest refuses an unnamed held item");
    auto named = item(280);
    nbt::Tag tag;
    tag.type = 10;
    tag.compound["display"].type = 10;
    tag.compound["display"].compound["Name"] = nbt::text("test-key");
    named.nbt = nbt::encode(tag);
    game.inventory[36] = named;
    require(game.offlineBlockInteraction(-2, 64, 0) && game.window.open, "matching display name opens locked chest");
    game.closeContainer();

    for (int x : {-4, -6})
        game.world.setBlock(x, 64, 0, blockState(130, 3));
    game.offlineBlockInteraction(-4, 64, 0);
    game.window.slots[4] = item(340, 7);
    game.closeContainer();
    game.offlineBlockInteraction(-6, 64, 0);
    require(game.window.slots[4].id == 340 && game.window.slots[4].count == 7, "ender chest shares personal storage");
    game.closeContainer();
    for (const auto pair : {std::pair<int, int>{23, 9}, {158, 9}, {154, 5}}) {
        game.world.setBlock(0, 64, pair.first == 23 ? 2 : pair.first == 158 ? 3 : 4, blockState(pair.first));
        const int z = pair.first == 23 ? 2 : pair.first == 158 ? 3 : 4;
        game.offlineBlockInteraction(0, 64, z);
        require(game.window.slots.size() == std::size_t(pair.second + 36), "storage block uses original slot count");
        game.window.slots[pair.second - 1] = item(4, 12);
        game.closeContainer();
        game.offlineBlockInteraction(0, 64, z);
        require(game.window.slots[pair.second - 1].count == 12, "storage survives container close/reopen");
        game.closeContainer();
    }

    game.inventory[5] = item(310);
    game.inventory[8] = item(313);
    game.inventory[9] = item(17, 21);
    game.inventory[44] = item(264, 3);
    game.selectedSlot = 8;
    game.health = 17;
    game.food = 14;
    game.saturation = 2;
    game.experienceLevel = 3;
    game.totalExperience = 28;
    game.experienceProgress = .25f;
    local.simulation.weather.totalTime = game.worldAge = 123456;
    local.simulation.weather.dayTime = game.worldTime = 23456;
    local.simulation.enderItems[8] = item(276);
    game.world.setWorldBorder(4, -2, 256);
    game.border.centerX = 4;
    game.border.centerZ = -2;
    game.border.diameter = game.border.targetDiameter = 256;

    // This saved column is outside the initial load radius. A resumed generated
    // world must read it before considering native terrain generation.
    const int farX = 64 * 16 + 3;
    game.world.setBlock(farX, 80, 3, blockState(57));
    game.world.setBlock(farX + 1, 80, 3, blockState(61));
    local.simulation.furnaceAt(farX + 1, 80, 3).slots[0] = item(15, 6);
    local.saveInfo.generation = worldio::GenerationInfo{};
    local.saveInfo.generation->mapFeatures = false;
    game.cursor = item(266, 2);
    const auto save = base / "native";
    require(local.saveNew(game, save.u8string(), error), error);
    require(game.cursor.empty() && local.simulation.droppedEntities().size() == 1,
            "save preserves cursor as an actual dropped stack");
    require(fs::exists(save / "native_simulation.dat") && fs::exists(save / "level.dat"),
            "snapshot publishes terrain and sidecar together");
    require(!local.saveNew(game, save.u8string(), error), "existing save overwrite rejected");

    World exported;
    worldio::SaveInfo info;
    require(worldio::loadWorld(save.u8string(), exported, info, 1, &error), error);
    require(info.gameType == 0 && info.playerData && info.levelUpdates,
            "standard level.dat contains native player/clock data");
    std::array<ItemStack, 104> savedSlots;
    item_nbt::readSlots(*info.playerData, savedSlots, "Inventory");
    require(savedSlots[0].id == 280 && savedSlots[8].id == 264 && savedSlots[9].count == 21 &&
                savedSlots[103].id == 310 && savedSlots[100].id == 313,
            "native inventory maps to vanilla hotbar, main and armor slot IDs");
    require(info.playerData->get("foodLevel")->integer == 14 &&
                info.playerData->get("abilities")->get("mayfly")->integer == 0,
            "survival food/abilities are not overwritten by preview defaults");
    std::array<ItemStack, 3> furnace;
    item_nbt::readSlots(tile(info, 2, 64, 0), furnace);
    require(furnace[2].id == 265, "furnace result is exported as standard TileEntities NBT");
    std::array<ItemStack, 4> brewing;
    item_nbt::readSlots(tile(info, 3, 64, 0), brewing);
    require(brewing[0].id == 373 && brewing[0].damage == 16, "brewing result is standard NBT");
    std::array<ItemStack, 27> chest;
    item_nbt::readSlots(tile(info, 4, 64, 0), chest);
    require(chest[0].id == 264 && chest[0].count == 11, "double chest exports separate 27-slot inventories");

    Game resumed;
    LocalSession resumedLocal;
    require(resumedLocal.load(resumed, save.u8string(), 1, "LocalTest", error), error);
    require(resumed.inventory[44].id == 264 && resumed.health == 17 && resumed.food == 14 &&
                resumed.selectedSlot == 8 && resumed.worldAge == 123456 &&
                resumedLocal.simulation.enderItems[8].id == 276,
            "native session restores player inventory, ender storage, stats and clocks");
    require(resumedLocal.simulation.droppedEntities().size() == 1, "cursor drop persists across native load");
    resumedLocal.tick(resumed, {}, 2);
    require(resumed.world.border().minX == -124 && resumed.world.border().maxZ == 126,
            "first simulation tick preserves imported world border");
    require(!resumed.world.hasChunk(64, 0), "far saved column is initially unloaded");
    resumed.player.teleport(farX + .5, 82, 3.5);
    resumedLocal.tick(resumed, {}, 2);
    require(resumed.world.getBlock(farX, 80, 3) == blockState(57),
            "streaming loads edited far Anvil column before generation");
    require(resumedLocal.simulation.furnaceAt(farX + 1, 80, 3).slots[0].count == 6,
            "streamed machine keeps authoritative contents");

    const auto vanilla = base / "vanilla";
    copyFixture(save, vanilla);
    fs::remove(vanilla / "native_simulation.dat");
    Game imported;
    LocalSession importedLocal;
    require(importedLocal.load(imported, vanilla.u8string(), 1, "LocalTest", error), error);
    require(imported.inventory[5].id == 310 && imported.inventory[8].id == 313 && imported.inventory[44].id == 264 &&
                imported.food == 14 && importedLocal.simulation.enderItems[4].count == 7,
            "standard player NBT imports without a native sidecar");
    require(importedLocal.simulation.furnaceAt(2, 64, 0).slots[2].id == 265 &&
                importedLocal.simulation.brewingAt(3, 64, 0).slots[0].damage == 16,
            "standard Anvil machines enter active simulation");
    imported.offlineBlockInteraction(5, 64, 0);
    require(imported.window.slots[0].count == 11 && imported.window.slots[27].count == 5,
            "standard chest contents open correctly after import");
    imported.closeContainer();
    imported.invalidateTile(4, 64, 0);
    imported.world.setBlock(4, 64, 0, 0);
    const auto removed = base / "removed-tile";
    require(importedLocal.saveNew(imported, removed.u8string(), error), error);
    World afterRemoval;
    worldio::SaveInfo removedInfo;
    require(worldio::loadWorld(removed.u8string(), afterRemoval, removedInfo, 1, &error), error);
    require(tile(removedInfo, 4, 64, 0).type == 0 && tile(removedInfo, 5, 64, 0).type == 10,
            "removed tile is not resurrected from retained Anvil metadata");

    const auto malformed = base / "malformed";
    copyFixture(save, malformed);
    {
        std::ofstream stream(malformed / "native_simulation.dat", std::ios::binary | std::ios::trunc);
        stream << "broken";
    }
    const auto revision = imported.world.revision(), ticks = imported.ticks;
    require(!importedLocal.load(imported, malformed.u8string(), 1, "LocalTest", error), "corrupt sidecar rejected");
    require(importedLocal.active() && imported.world.revision() == revision && imported.ticks == ticks &&
                imported.inventory[44].id == 264,
            "failed sidecar load leaves the active world, player and callbacks intact");
    require(static_cast<bool>(imported.offlineBlockInteraction),
            "failed load retains live local interaction callbacks");
}
} // namespace

int main(int argc, char **argv) {
    fs::path base;
    try {
        if (argc >= 3 && std::string(argv[1]) == "--export-visual") {
            exportVisual(argv[2], argc > 3 ? std::stoi(argv[3]) : 0);
            return 0;
        }
        const auto parent = fs::weakly_canonical(fs::temp_directory_path());
        base = parent /
               ("native-local-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(fs::create_directory(base), "exclusive temporary fixture directory");
        creation();
        persistence(base);
        require(fs::weakly_canonical(base).parent_path() == parent &&
                    base.filename().u8string().find("native-local-test-") == 0,
                "test cleanup stays within its explicitly created fixture directory");
        fs::remove_all(base);
        std::cout << "local session tests passed (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << "\nFixtures: " << base.u8string() << '\n';
        return 1;
    }
}
