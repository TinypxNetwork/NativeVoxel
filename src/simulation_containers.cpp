#include "mc/simulation.hpp"
#include "mc/crafting.hpp"
#include "mc/item_nbt.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace mc {
namespace {
int signedBits(std::uint64_t value, int bits) {
    const auto mask = (1ull << bits) - 1;
    value &= mask;
    return static_cast<int>((value & (1ull << (bits - 1))) ? static_cast<std::int64_t>(value) - (1ll << bits) : value);
}
const char *tileName(unsigned id) {
    switch (id) {
    case 54:
    case 146:
        return "Chest";
    case 130:
        return "EnderChest";
    case 23:
        return "Trap";
    case 158:
        return "Dropper";
    case 154:
        return "Hopper";
    case 61:
    case 62:
        return "Furnace";
    case 117:
        return "Cauldron";
    default:
        return "";
    }
}
nbt::Tag tileTag(const Game &game, std::uint64_t packed, unsigned id, int x, int y, int z) {
    nbt::Tag tag;
    if (const auto it = game.tileEntities.find(packed); it != game.tileEntities.end())
        tag = nbt::parse(it->second.nbt);
    if (tag.type != 10) {
        tag = {};
        tag.type = 10;
    }
    tag.compound["id"] = nbt::text(tileName(id));
    tag.compound["x"] = nbt::integer(3, x);
    tag.compound["y"] = nbt::integer(3, y);
    tag.compound["z"] = nbt::integer(3, z);
    return tag;
}
std::string textTag(const nbt::Tag &tag, const char *name) {
    const auto *value = tag.get(name);
    return value && value->type == 8 ? value->string : "";
}
bool unlocked(const Game &game, const nbt::Tag &tag) {
    const auto lock = textTag(tag, "Lock");
    if (lock.empty() || game.gameMode == 3)
        return true;
    const auto &held = game.inventory[36 + static_cast<std::size_t>(std::clamp(game.selectedSlot, 0, 8))];
    if (held.empty())
        return false;
    const auto data = nbt::parse(held.nbt);
    const auto *display = data.get("display");
    return display && textTag(*display, "Name") == lock;
}
bool chestBlocked(const Game &game, int x, int y, int z, bool ender) {
    if (registry::block(blockId(game.world.getBlock(x, y + 1, z))).normalCube)
        return true;
    if (ender)
        return false;
    for (const auto &entry : game.entities) {
        const auto &entity = entry.second;
        if (entity.kind != EntityKind::Mob || entity.type != 98)
            continue;
        const auto flags = entity.metadata.find(16);
        if (flags == entity.metadata.end() || !(flags->second.integer & 1))
            continue;
        // EntityOcelot.setSize(.6F, .7F), including Java float promotion.
        const double half = static_cast<double>(.6f) / 2, height = .7f;
        if (entity.x + half > x && entity.x - half < x + 1 && entity.z + half > z && entity.z - half < z + 1 &&
            entity.y + height > y + 1 && entity.y < y + 2)
            return true;
    }
    return false;
}
void blockAction(Game &game, int x, int y, int z, int id, bool open) {
    if (id == 54 || id == 146 || id == 130) {
        game.queueBlockAction({x, y, z, id, 1, static_cast<std::uint8_t>(open), game.ticks});
        if (game.blockActions.size() > 1024)
            game.blockActions.pop_front();
    }
}
} // namespace

void Simulation::importTileEntity(const Game &game, const TileEntityData &tile) {
    if (game.online)
        throw std::logic_error("Cannot import local tile data into an online game");
    const auto id = blockId(game.world.getBlock(tile.x, tile.y, tile.z));
    if (id != 61 && id != 62 && id != 117)
        return;
    const auto packed = key(tile.x, tile.y, tile.z);
    const auto tag = nbt::parse(tile.nbt);
    if (id == 117) {
        if (brewingStands_.count(packed))
            return;
        BrewingState stand;
        item_nbt::readSlots(tag, stand.slots);
        stand.brewTime = static_cast<std::int16_t>(item_nbt::integer(tag, "BrewTime"));
        brewingStands_.emplace(packed, std::move(stand));
    } else {
        if (furnaces_.count(packed))
            return;
        FurnaceState furnace;
        item_nbt::readSlots(tag, furnace.slots);
        furnace.burnTime = static_cast<std::int16_t>(item_nbt::integer(tag, "BurnTime"));
        furnace.cookTime = static_cast<std::int16_t>(item_nbt::integer(tag, "CookTime"));
        furnace.totalCookTime = static_cast<std::int16_t>(item_nbt::integer(tag, "CookTimeTotal"));
        furnace.currentItemBurnTime = crafting::fuelBurnTime(furnace.slots[1]);
        furnaces_.emplace(packed, std::move(furnace));
    }
}

void Simulation::publishTileEntities(Game &game) const {
    if (game.online)
        throw std::logic_error("Cannot publish local tile data into an online game");
    for (const auto &entry : furnaces_) {
        const int x = signedBits(entry.first >> 38, 26), y = signedBits(entry.first >> 26, 12),
                  z = signedBits(entry.first, 26);
        const auto id = blockId(game.world.getBlock(x, y, z));
        if (id != 61 && id != 62)
            continue;
        auto tag = tileTag(game, entry.first, id, x, y, z);
        const auto &furnace = entry.second;
        tag.compound["BurnTime"] = nbt::integer(2, furnace.burnTime);
        tag.compound["CookTime"] = nbt::integer(2, furnace.cookTime);
        tag.compound["CookTimeTotal"] = nbt::integer(2, furnace.totalCookTime);
        tag.compound["Items"] = item_nbt::writeSlots(furnace.slots);
        game.tileEntities[entry.first] = {x, y, z, 0, nbt::encode(tag)};
    }
    for (const auto &entry : brewingStands_) {
        const int x = signedBits(entry.first >> 38, 26), y = signedBits(entry.first >> 26, 12),
                  z = signedBits(entry.first, 26);
        if (blockId(game.world.getBlock(x, y, z)) != 117)
            continue;
        auto tag = tileTag(game, entry.first, 117, x, y, z);
        tag.compound["BrewTime"] = nbt::integer(2, entry.second.brewTime);
        tag.compound["Items"] = item_nbt::writeSlots(entry.second.slots);
        game.tileEntities[entry.first] = {x, y, z, 0, nbt::encode(tag)};
    }
}

bool Simulation::interactStorage(Game &game, int x, int y, int z) {
    const auto id = blockId(game.world.getBlock(x, y, z));
    const bool chest = id == 54 || id == 146, ender = id == 130;
    if (!chest && !ender && id != 23 && id != 158 && id != 154)
        return false;
    lastInteractionError.clear();
    const int size = chest || ender ? 27 : id == 154 ? 5 : 9;
    std::vector<StoragePart> parts{{x, y, z, int(id), size}};
    if (chest) {
        // EnumFacing.Plane.HORIZONTAL: NORTH, EAST, SOUTH, WEST. West/north
        // are prepended by InventoryLargeChest, irrespective of the clicked half.
        constexpr int dx[] = {0, 1, 0, -1}, dz[] = {-1, 0, 1, 0};
        for (int face = 0; face < 4; ++face) {
            if (blockId(game.world.getBlock(x + dx[face], y, z + dz[face])) != id)
                continue;
            StoragePart other{x + dx[face], y, z + dz[face], int(id), 27};
            if (face == 0 || face == 3)
                parts.insert(parts.begin(), other);
            else
                parts.push_back(other);
        }
    }
    std::vector<ItemStack> inventory;
    std::string customName;
    for (const auto &part : parts) {
        if ((chest || ender) && chestBlocked(game, part.x, part.y, part.z, ender))
            return true;
        const auto packed = key(part.x, part.y, part.z);
        const auto tag = tileTag(game, packed, id, part.x, part.y, part.z);
        if (!unlocked(game, tag)) {
            lastInteractionError = "Container is locked";
            return true;
        }
        if (customName.empty())
            customName = textTag(tag, "CustomName");
        std::vector<ItemStack> slots(part.slots);
        if (ender)
            std::copy(enderItems.begin(), enderItems.end(), slots.begin());
        else
            item_nbt::readSlots(tag, slots);
        inventory.insert(inventory.end(), slots.begin(), slots.end());
    }
    if (game.window.open) {
        beforeContainerClose(game);
        game.window = {};
    }
    openStorage_ = std::move(parts);
    enderOpen_ = ender;
    furnaceOpen_ = brewingOpen_ = false;
    ContainerWindow window;
    window.open = true;
    window.id = nextWindowId_;
    nextWindowId_ = nextWindowId_ % 100 + 1;
    window.type = id == 23    ? "minecraft:dispenser"
                  : id == 158 ? "minecraft:dropper"
                  : id == 154 ? "minecraft:hopper"
                              : "minecraft:chest";
    window.title = !customName.empty() ? customName
                   : ender             ? "Ender Chest"
                   : chest             ? (openStorage_.size() > 1 ? "Large Chest" : "Chest")
                   : id == 23          ? "Dispenser"
                   : id == 158         ? "Dropper"
                                       : "Hopper";
    window.slots = std::move(inventory);
    window.slots.insert(window.slots.end(), game.inventory.begin() + 9, game.inventory.end());
    game.window = std::move(window);
    ++game.windowRevision;
    for (const auto &part : openStorage_)
        blockAction(game, part.x, part.y, part.z, part.blockId, true);
    commitStorage(game);
    return true;
}

void Simulation::commitStorage(Game &game) {
    if (openStorage_.empty() || !game.window.open)
        return;
    const auto count = game.window.slots.size() >= 36 ? game.window.slots.size() - 36 : 0;
    if (count == storageSnapshot_.size() &&
        std::equal(storageSnapshot_.begin(), storageSnapshot_.end(), game.window.slots.begin(),
                   [](const ItemStack &a, const ItemStack &b) {
                       return a.id == b.id && a.damage == b.damage && a.count == b.count && a.nbt == b.nbt;
                   }))
        return;
    std::size_t offset = 0;
    for (const auto &part : openStorage_) {
        if (offset + part.slots + 36 > game.window.slots.size())
            return;
        if (blockId(game.world.getBlock(part.x, part.y, part.z)) != unsigned(part.blockId))
            return;
        const auto begin = game.window.slots.begin() + offset;
        std::vector<ItemStack> slots(begin, begin + part.slots);
        if (enderOpen_)
            std::copy(slots.begin(), slots.end(), enderItems.begin());
        else {
            const auto packed = key(part.x, part.y, part.z);
            auto tag = tileTag(game, packed, part.blockId, part.x, part.y, part.z);
            tag.compound["Items"] = item_nbt::writeSlots(slots);
            game.tileEntities[packed] = {part.x, part.y, part.z, 0, nbt::encode(tag)};
        }
        offset += part.slots;
    }
    storageSnapshot_.assign(game.window.slots.begin(), game.window.slots.begin() + offset);
}

void Simulation::closeStorage(Game &game) {
    for (const auto &part : openStorage_)
        blockAction(game, part.x, part.y, part.z, part.blockId, false);
    openStorage_.clear();
    storageSnapshot_.clear();
    enderOpen_ = false;
}
} // namespace mc
