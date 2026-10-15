#include "mc/simulation.hpp"
#include "mc/drops.hpp"
#include "mc/registry.hpp"
#include "mc/projectiles.hpp"
#include "mc/item_nbt.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <tuple>

namespace mc {
namespace {
constexpr std::uint64_t Multiplier = 0x5deece66dULL;
void offline(const Game &game) {
    if (game.online)
        throw std::logic_error("Singleplayer entities cannot mutate an online Game");
}
worldgen::JavaRandom restored(std::uint64_t state) {
    return worldgen::JavaRandom(static_cast<std::int64_t>(state ^ Multiplier));
}
void addEvent(std::deque<SimulationEvent> &events, SimulationEvent event) {
    events.push_back(std::move(event));
    if (events.size() > 4096)
        events.pop_front();
}
bool kind(unsigned id, const char *type) { return std::strcmp(registry::block(id).kind, type) == 0; }
bool canDestroy(const ItemStack &stack, unsigned id) {
    if (stack.empty())
        return false;
    const auto tag = nbt::parse(stack.nbt);
    const auto *blocks = tag.get("CanDestroy");
    if (!blocks || blocks->type != 9 || blocks->subtype != 8)
        return false;
    const std::string name = registry::block(id).name;
    const auto colon = name.find(':');
    const auto shortName = colon == std::string::npos ? name : name.substr(colon + 1);
    for (const auto &value : blocks->list)
        if (value.string == name || value.string == shortName || value.string == std::to_string(id))
            return true;
    return false;
}
bool tileDependent(unsigned id) {
    switch (id) {
    case 25:
    case 84:
    case 116:
    case 137:
    case 138:
    case 140:
    case 144:
    case 149:
    case 150:
    case 176:
    case 177:
        return true;
    default:
        return false;
    }
}
int containerSlots(unsigned id) {
    switch (id) {
    case 54: case 146: return 27;
    case 23: case 158: return 9;
    case 154: return 5;
    case 61: case 62: return 3;
    case 117: return 4;
    default: return 0; // Ender chest contents belong to the player, not the block.
    }
}
void readContainerItems(const nbt::Tag &tile, std::vector<ItemStack> &slots) {
    if (tile.type != 10)
        throw net::ProtocolError("Invalid container tile compound");
    const auto *items = tile.get("Items");
    if (!items) return;
    if (items->type != 9 || (items->subtype != 10 && !(items->subtype == 0 && items->list.empty())))
        throw net::ProtocolError("Invalid container Items list");
    std::vector<bool> occupied(slots.size());
    for (const auto &value : items->list) {
        const auto *index = value.get("Slot"), *count = value.get("Count"), *damage = value.get("Damage"), *extra = value.get("tag");
        if (value.type != 10 || !index || index->type < 1 || index->type > 4 || index->integer < 0 ||
            static_cast<std::size_t>(index->integer) >= slots.size() || occupied[index->integer] ||
            !count || count->type < 1 || count->type > 4 || count->integer <= 0 || count->integer > 127 ||
            (damage && (damage->type < 1 || damage->type > 4 || damage->integer < 0 || damage->integer > 32767)) ||
            (extra && extra->type != 10))
            throw net::ProtocolError("Invalid or duplicated container inventory slot");
        const auto *id = value.get("id");
        if (!id || (id->type != 8 && (id->type < 1 || id->type > 4 || id->integer < 0 || id->integer > 32767)))
            throw net::ProtocolError("Invalid container inventory item ID");
        auto stack = item_nbt::read(value);
        if (stack.empty())
            throw net::ProtocolError("Unknown container inventory item");
        occupied[index->integer] = true;
        slots[index->integer] = std::move(stack);
    }
}
bool complexLifecycle(BlockState state) {
    const auto id = blockId(state);
    if (tileDependent(id) || kind(id, "BlockDoor") || id == 26 || id == 175 || id == 34 || id == 36 || id == 97)
        return true;
    if ((id == 29 || id == 33) && (blockMeta(state) & 8))
        return true;
    if (id == 46 && (blockMeta(state) & 1))
        return true;
    return false;
}
bool rainAt(const Game &game, const drop_entities::State &state) {
    if (!game.raining || game.dimension != 0)
        return false;
    const int x = floorBlock(state.x), z = floorBlock(state.z);
    const auto *chunk = game.world.chunk(floorDiv16(x), floorDiv16(z));
    if (!chunk)
        return false;
    const auto &biome = registry::biome(game.world.getBiome(x, z));
    if (biome.snow || biome.rainfall == 0 || biome.temperature < .15f)
        return false;
    const int height = chunk->heightMap[static_cast<std::size_t>((z & 15) * 16 + (x & 15))];
    return floorBlock(state.y) >= height || floorBlock(state.y + state.height) >= height;
}
using Cell = std::tuple<int, int, int>;
Cell cell(const drop_entities::State &value) {
    return {floorBlock(value.x / 2), floorBlock(value.y / 2), floorBlock(value.z / 2)};
}
} // namespace

void Simulation::publishDroppedEntity(Game &game, const drop_entities::State &state) const {
    if (state.dead) {
        game.entities.erase(state.id);
        return;
    }
    auto &entity = game.entities[state.id];
    entity.id = state.id;
    entity.kind = state.orb ? EntityKind::ExperienceOrb : EntityKind::Object;
    entity.type = state.orb ? state.value : 2;
    entity.xpValue = state.orb ? state.value : 0;
    entity.x = state.x;
    entity.y = state.y;
    entity.z = state.z;
    entity.previousX = state.previousX;
    entity.previousY = state.previousY;
    entity.previousZ = state.previousZ;
    entity.yaw = state.yaw;
    entity.motionX = state.motionX;
    entity.motionY = state.motionY;
    entity.motionZ = state.motionZ;
    entity.onGround = state.onGround;
    entity.interpolationTicks = 0;
    entity.ageTicks = state.orb ? state.color : state.ticks;
    entity.hoverStart = state.hoverStart;
    if (!state.orb) {
        net::Writer bytes;
        Game::writeItem(bytes, state.item);
        MetadataValue value;
        value.type = 5;
        value.raw = std::move(bytes.data);
        entity.metadata[10] = std::move(value);
    }
}
void Simulation::publishDroppedEntities(Game &game) const {
    for (const auto &pair : droppedEntities_)
        publishDroppedEntity(game, pair.second);
}
int Simulation::spawnItem(Game &game, const ItemStack &stack, double x, double y, double z, int pickupDelay) {
    offline(game);
    if (!running_)
        throw std::logic_error("Simulation has not been started");
    if (stack.empty() || !registry::item(stack.id).valid)
        return -1;
    if (!stack.nbt.empty()) {
        net::Writer check;
        Game::writeItem(check, stack);
    }
    if (droppedEntities_.size() >= 65536)
        throw net::ProtocolError("Native dropped entity limit exceeded");
    while (game.entities.count(nextEntityId_) || droppedEntities_.count(nextEntityId_))
        ++nextEntityId_;
    auto random = restored(mathRandomState_), seeds = restored(entitySeedState_);
    const int id = nextEntityId_++;
    auto state = drop_entities::item(id, stack, x, y, z, random, seeds.nextLong());
    state.pickupDelay = pickupDelay;
    mathRandomState_ = random.internalSeed();
    entitySeedState_ = seeds.internalSeed();
    droppedEntities_.emplace(id, std::move(state));
    publishDroppedEntity(game, droppedEntities_.at(id));
    return id;
}
int Simulation::useProjectile(Game& game,const ItemStack& stack,float charge) {
    offline(game);
    if(!running_||settings.gameMode==3||stack.empty()||game.health<=0)return -1;
    if(stack.id==346){const int hook=projectiles::fishingHook(game,game.entityId);if(hook>=0){
        auto& entity=game.entities.at(hook);int cost=0;
        if(entity.projectile){const auto& state=*entity.projectile;const auto caught=game.entities.find(state.caughtEntityId);
            if(caught!=game.entities.end()){const double x=game.player.x-entity.x,y=game.player.y-entity.y,z=game.player.z-entity.z;const double distance=double(float(std::sqrt(x*x+y*y+z*z)));caught->second.motionX+=x*.1;caught->second.motionY+=y*.1+double(float(std::sqrt(distance)))*.08;caught->second.motionZ+=z*.1;cost=3;}
            if(state.inGround)cost=2;
        }
        game.entities.erase(hook);return cost;
    }}
    if(game.entities.size()>=65536)return -1;
    switch(stack.id){case 261:case 332:case 344:case 368:case 384:case 346:break;case 373:if(!(stack.damage&16384))return -1;break;default:return -1;}
    while(game.entities.count(nextEntityId_)||droppedEntities_.count(nextEntityId_))++nextEntityId_;
    auto random=restored(entitySeedState_);const auto seed=random.nextLong();
    auto entity=projectiles::launch(game,nextEntityId_,stack,charge,static_cast<std::uint64_t>(seed));
    entitySeedState_=random.internalSeed();game.entities.emplace(nextEntityId_++,std::move(entity));return 0;
}
int Simulation::spawnExperience(Game &game, int amount, double x, double y, double z) {
    offline(game);
    if (!running_)
        throw std::logic_error("Simulation has not been started");
    if (amount <= 0)
        return -1;
    if (amount > 32767)
        throw net::ProtocolError("XP orb value exceeds vanilla signed short");
    if (droppedEntities_.size() >= 65536)
        throw net::ProtocolError("Native dropped entity limit exceeded");
    while (game.entities.count(nextEntityId_) || droppedEntities_.count(nextEntityId_))
        ++nextEntityId_;
    auto random = restored(mathRandomState_), seeds = restored(entitySeedState_);
    const int id = nextEntityId_++;
    auto state = drop_entities::experience(id, amount, x, y, z, random, seeds.nextLong());
    mathRandomState_ = random.internalSeed();
    entitySeedState_ = seeds.internalSeed();
    droppedEntities_.emplace(id, std::move(state));
    publishDroppedEntity(game, droppedEntities_.at(id));
    return id;
}
int Simulation::dropFromPlayer(Game &game, const ItemStack &stack, bool trace, bool around) {
    offline(game);
    if (settings.gameMode == 3 || stack.empty())
        return -1;
    if (!std::isfinite(game.player.yaw) || !std::isfinite(game.player.pitch))
        throw net::ProtocolError("Invalid player drop rotation");
    drop_entities::State probe;
    probe.setPosition(game.player.x,
                      game.player.y - .30000001192092896 + static_cast<double>(game.player.sneaking ? 1.54f : 1.62f),
                      game.player.z);
    const int id = spawnItem(game, stack, game.player.x, game.player.y, game.player.z, 40);
    if (id < 0)
        return id;
    auto random = restored(playerRandomState_);
    auto &state = droppedEntities_.at(id);
    drop_entities::throwFromPlayer(state, game.player, around, trace, settings.playerName, random);
    playerRandomState_ = random.internalSeed();
    publishDroppedEntity(game, state);
    SimulationEvent event;
    event.kind = SimulationEventKind::ItemDropped;
    event.x = floorBlock(state.x);
    event.y = floorBlock(state.y);
    event.z = floorBlock(state.z);
    event.entityId = id;
    event.item = stack;
    addEvent(events, event);
    return id;
}
bool Simulation::dropFromPlayerBatch(Game &game, const std::vector<ItemStack> &stacks, bool trace) {
    offline(game);
    if (!running_ || settings.gameMode == 3 || stacks.size() > 65536 - droppedEntities_.size())
        return false;
    if (!std::isfinite(game.player.x) || !std::isfinite(game.player.y) || !std::isfinite(game.player.z) ||
        !std::isfinite(game.player.yaw) || !std::isfinite(game.player.pitch))
        return false;
    try {
        drop_entities::State probe;
        probe.setPosition(game.player.x,
                          game.player.y - .30000001192092896 +
                              static_cast<double>(game.player.sneaking ? 1.54f : 1.62f),
                          game.player.z);
        for (const auto &stack : stacks) {
            if (stack.empty() || !registry::item(stack.id).valid)
                return false;
            if (!stack.nbt.empty()) {
                net::Writer check;
                Game::writeItem(check, stack);
            }
        }
    } catch (const std::exception &) {
        return false;
    }
    for (const auto &stack : stacks)
        dropFromPlayer(game, stack, trace);
    return true;
}
bool Simulation::dropHeldItem(Game &game, bool all) {
    offline(game);
    if (!running_ || settings.gameMode == 3)
        return false;
    const auto slot = 36 + static_cast<std::size_t>(std::clamp(game.selectedSlot, 0, 8));
    auto &held = game.inventory[slot];
    if (held.empty())
        return false;
    auto thrown = held;
    if (!all)
        thrown.count = 1;
    if (dropFromPlayer(game, thrown, true) < 0)
        return false;
    held.count = static_cast<std::int8_t>(held.count - thrown.count);
    if (held.count <= 0)
        held = ItemStack{};
    ++game.inventorySlotRevision[slot];
    if (game.window.open && game.window.slots.size() >= 36)
        game.window.slots[game.window.slots.size() - 36 + slot - 9] = held;
    return true;
}
bool Simulation::creativeDrop(Game &game, const ItemStack &stack) {
    offline(game);
    lastInteractionError.clear();
    if (!running_ || settings.gameMode != 1 || creativeDropThreshold_ >= 200)
        return false;
    // A null C10 stack consumes the original handler's limiter but creates no entity.
    if (stack.id < 0) {
        creativeDropThreshold_ += 20;
        return true;
    }
    if (!registry::item(stack.id).valid || stack.damage < 0 || stack.count <= 0 || stack.count > 64)
        return false;
    auto dropped = stack;
    if (!prepareCreativeItem(game, dropped))
        return false;
    const int id = dropFromPlayer(game, dropped, false, false);
    if (id < 0)
        return false;
    droppedEntities_.at(id).age = 4800;
    creativeDropThreshold_ += 20;
    return true;
}
void Simulation::beforeContainerClose(Game &game) {
    offline(game);
    if (!running_)
        return;
    commitContainer(game);
    std::vector<ItemStack> drops;
    if (!game.cursor.empty())
        drops.push_back(game.cursor);
    std::vector<int> clearSlots;
    if (!game.window.open) {
        for (int slot = 1; slot <= 4; ++slot) {
            clearSlots.push_back(slot);
            if (!game.inventory[static_cast<std::size_t>(slot)].empty())
                drops.push_back(game.inventory[static_cast<std::size_t>(slot)]);
        }
    } else {
        int count = 0;
        const auto &type = game.window.type;
        if (type == "minecraft:crafting_table")
            count = 9;
        else if (type == "minecraft:anvil" || type == "minecraft:enchanting_table" || type == "minecraft:villager")
            count = 2;
        const int begin = type == "minecraft:crafting_table" ? 1 : 0;
        for (int slot = begin; slot < begin + count && static_cast<std::size_t>(slot) < game.window.slots.size();
             ++slot) {
            clearSlots.push_back(slot);
            if (!game.window.slots[static_cast<std::size_t>(slot)].empty())
                drops.push_back(game.window.slots[static_cast<std::size_t>(slot)]);
        }
    }
    if (settings.gameMode == 3 && !drops.empty())
        throw net::ProtocolError("Spectator cannot discard offline container contents");
    if (droppedEntities_.size() + drops.size() > 65536)
        throw net::ProtocolError("Too many entities to close container without losing items");
    if (!drops.empty() && !dropFromPlayerBatch(game, drops))
        throw net::ProtocolError("Cannot close container without losing rejected items");
    game.cursor = ItemStack{};
    if (!game.window.open) {
        for (int slot : clearSlots) {
            game.inventory[static_cast<std::size_t>(slot)] = ItemStack{};
            ++game.inventorySlotRevision[static_cast<std::size_t>(slot)];
        }
        game.inventory[0] = ItemStack{};
    } else
        for (int slot : clearSlots)
            game.window.slots[static_cast<std::size_t>(slot)] = ItemStack{};
    furnaceOpen_ = brewingOpen_ = false;
    closeStorage(game);
}
bool Simulation::breakBlock(Game &game, int x, int y, int z, int face) {
    offline(game);
    lastInteractionError.clear();
    if (!running_)
        return false;
    if (y < 0 || y > 255 || x < -30000000 || x >= 30000000 || z < -30000000 || z >= 30000000 || face < 0 || face > 5 ||
        !game.world.hasChunk(floorDiv16(x), floorDiv16(z)))
        return false;
    const auto before = game.world.getBlock(x, y, z);
    const auto id = blockId(before);
    auto &held = game.inventory[36 + static_cast<std::size_t>(std::clamp(game.selectedSlot, 0, 8))];
    const bool creative = settings.gameMode == 1;
    if (id == 0 || settings.gameMode == 3 || (settings.gameMode == 2 && !canDestroy(held, id)) ||
        (creative && !held.empty() && std::strcmp(registry::item(held.id).kind, "ItemSword") == 0) ||
        (!creative && registry::block(id).hardness < 0))
        return false;
    if (!drops::knownState(before) || complexLifecycle(before)) {
        lastInteractionError = "This block requires an unported tile or paired-block lifecycle";
        return false;
    }
    const auto packed = key(x, y, z);
    std::vector<ItemStack> inventory(static_cast<std::size_t>(containerSlots(id)));
    bool closeWindow = game.window.open &&
        ((furnaceOpen_ && openFurnace_ == packed) || (brewingOpen_ && openBrewing_ == packed));
    std::size_t storageOffset = 0;
    for (const auto &part : openStorage_) {
        if (part.x == x && part.y == y && part.z == z && game.window.open) {
            closeWindow = true;
            break;
        }
        storageOffset += static_cast<std::size_t>(part.slots);
    }
    // Read the authoritative inventory without committing the window. A rejected
    // break must preserve even uncommitted UI edits, machine state and tile NBT.
    try {
        if (closeWindow) {
            std::size_t expectedSlots = 36;
            if (furnaceOpen_) expectedSlots += 3;
            else if (brewingOpen_) expectedSlots += 4;
            else for (const auto &part : openStorage_) expectedSlots += static_cast<std::size_t>(part.slots);
            const char *expectedType = furnaceOpen_ ? "minecraft:furnace" : brewingOpen_ ? "minecraft:brewing_stand" :
                id == 23 ? "minecraft:dispenser" : id == 158 ? "minecraft:dropper" : id == 154 ? "minecraft:hopper" : "minecraft:chest";
            if (game.window.slots.size() != expectedSlots || game.window.type != expectedType)
                throw net::ProtocolError("Invalid complete container window");
        }
        if (!inventory.empty()) {
            if (closeWindow) {
                const std::size_t offset = furnaceOpen_ || brewingOpen_ ? 0 : storageOffset;
                if (offset + inventory.size() + 36 > game.window.slots.size())
                    throw net::ProtocolError("Invalid open container size");
                std::copy_n(game.window.slots.begin() + offset, inventory.size(), inventory.begin());
            } else if ((id == 61 || id == 62) && furnaces_.count(packed)) {
                const auto &slots = furnaces_.at(packed).slots;
                std::copy(slots.begin(), slots.end(), inventory.begin());
            } else if (id == 117 && brewingStands_.count(packed)) {
                const auto &slots = brewingStands_.at(packed).slots;
                std::copy(slots.begin(), slots.end(), inventory.begin());
            } else if (const auto tile = game.tileEntities.find(packed); tile != game.tileEntities.end()) {
                readContainerItems(nbt::parse(tile->second.nbt), inventory);
            }
        }
        auto valid = [](const ItemStack &stack) {
            if (stack.empty()) return;
            if (!registry::item(stack.id).valid)
                throw net::ProtocolError("Invalid container item ID");
            net::Writer check;
            Game::writeItem(check, stack);
        };
        for (const auto &stack : inventory) valid(stack);
        if (closeWindow) {
            // beforeContainerClose commits both halves of a double chest.
            for (const auto &stack : game.window.slots) valid(stack);
            valid(game.cursor);
            if (!game.cursor.empty()) {
                drop_entities::State probe;
                probe.setPosition(game.player.x, game.player.eyeY() - .30000001192092896, game.player.z);
                if (!std::isfinite(game.player.yaw) || !std::isfinite(game.player.pitch))
                    throw net::ProtocolError("Invalid player orientation");
            }
            for (const auto &part : openStorage_) {
                if (blockId(game.world.getBlock(part.x, part.y, part.z)) != unsigned(part.blockId))
                    throw net::ProtocolError("Open container no longer matches its blocks");
                // Check metadata before commitStorage's NBT parsing/writing.
                if (const auto tile = game.tileEntities.find(key(part.x, part.y, part.z)); tile != game.tileEntities.end())
                    nbt::parse(tile->second.nbt);
            }
        }
    } catch (const std::exception &error) {
        lastInteractionError = std::string("Cannot break container: ") + error.what();
        return false;
    }
    if (id == 58 && game.window.open && game.window.type == "minecraft:crafting_table") {
        lastInteractionError = "Close the crafting window before breaking the workbench";
        return false;
    }
    const bool harvestable = drops::canHarvest(before, held);
    auto nextHeld = held;
    auto worldRandom = restored(randomState_), playerRandom = restored(playerRandomState_);
    if (!creative)
        drops::damageOnBreak(before, nextHeld, false, worldRandom, playerRandom);
    drops::Context context;
    context.state = before;
    context.tileDrops = settings.tileDrops;
    context.waterVaporizes = game.dimension == -1;
    const auto below = blockId(game.world.getBlock(x, y - 1, z));
    context.supportedBelow = registry::block(below).materialBlocksMovement || (below >= 8 && below <= 11);
    drops::Result result;
    result.supported = true;
    if (!creative && harvestable)
        result = drops::harvest(context, nextHeld, worldRandom);
    if (!result.supported) {
        lastInteractionError = "This block's drop rule requires unported tile data";
        return false;
    }
    auto inventoryRandom = inventoryDropRandom_;
    const auto inventoryDrops = inventory_drops::split(inventory, inventoryRandom);
    const std::size_t closingDrops = closeWindow && !game.cursor.empty() ? 1 : 0;
    const std::size_t spawning = result.items.size() + result.experience.size() + inventoryDrops.size() + closingDrops;
    if (droppedEntities_.size() + spawning > 65536) {
        lastInteractionError = "Too many dropped entities in the loaded world";
        return false;
    }
    // The native save format bounds NextEntityId at 2,000,000,000. Include IDs
    // occupied by other entity types and the cursor drop performed when closing.
    std::int64_t nextId = nextEntityId_;
    for (std::size_t index = 0; index < spawning; ++index) {
        while (nextId < 2000000000 && (game.entities.count(static_cast<int>(nextId)) || droppedEntities_.count(static_cast<int>(nextId))))
            ++nextId;
        if (nextId >= 2000000000) {
            lastInteractionError = "No native entity IDs remain for the container contents";
            return false;
        }
        ++nextId;
    }
    // Validation and capacity checks precede every mutation. Close while the
    // block still exists so the surviving half and ender inventory are committed.
    randomState_ = worldRandom.internalSeed();
    playerRandomState_ = playerRandom.internalSeed();
    if (closeWindow) {
        beforeContainerClose(game);
        game.window = {};
        ++game.windowRevision;
    }
    if (!game.world.setBlock(x, y, z, 0))
        return false;
    game.invalidateTile(x, y, z);
    held = std::move(nextHeld);
    ++game.inventorySlotRevision[36 + static_cast<std::size_t>(std::clamp(game.selectedSlot, 0, 8))];
    inventoryDropRandom_ = inventoryRandom;
    const auto &info = registry::block(id);
    game.soundEvents.push_back(
        {info.breakSound, x + .5, y + .5, z + .5, (info.soundVolume + 1.f) / 2.f, info.soundPitch * .8f});
    if (game.soundEvents.size() > 256)
        game.soundEvents.pop_front();
    SimulationEvent changed;
    changed.kind = SimulationEventKind::BlockChanged;
    changed.x = x;
    changed.y = y;
    changed.z = z;
    changed.before = before;
    addEvent(events, changed);
    if (result.replacement) {
        game.world.setBlock(x, y, z, result.replacement);
        changed.before = 0;
        changed.after = result.replacement;
        addEvent(events, changed);
    }
    if (!game.invulnerable)
        addExhaustion(result.exhaustion);
    furnaces_.erase(packed);
    brewingStands_.erase(packed);
    game.signs.erase(packed);
    // Log/leaf removal marks the nearby leaves for their later random decay tick.
    if (id == 17 || id == 162 || id == 18 || id == 161) {
        const int radius = (id == 17 || id == 162) ? 4 : 1, loadRadius = radius + 1;
        bool areaLoaded = true;
        for (int cx = floorDiv16(x - loadRadius); cx <= floorDiv16(x + loadRadius); ++cx)
            for (int cz = floorDiv16(z - loadRadius); cz <= floorDiv16(z + loadRadius); ++cz)
                areaLoaded = areaLoaded && game.world.hasChunk(cx, cz);
        if (areaLoaded)
            for (int dx = -radius; dx <= radius; ++dx)
                for (int dy = -radius; dy <= radius; ++dy)
                    for (int dz = -radius; dz <= radius; ++dz) {
                        const auto leaf = game.world.getBlock(x + dx, y + dy, z + dz);
                        const auto leafId = blockId(leaf);
                        if ((leafId == 18 || leafId == 161) && !(blockMeta(leaf) & 8))
                            game.world.setBlock(x + dx, y + dy, z + dz, static_cast<BlockState>(leaf | 8));
                    }
    }
    // InventoryHelper spawns contents before harvestBlock spawns the block item.
    // It does not consult doTileDrops/creative mode or setDefaultPickupDelay.
    for (const auto &drop : inventoryDrops) {
        const int entity = spawnItem(game, drop.item, x + drop.x, y + drop.y, z + drop.z, 0);
        auto &state = droppedEntities_.at(entity);
        state.motionX = drop.motionX;
        state.motionY = drop.motionY;
        state.motionZ = drop.motionZ;
        publishDroppedEntity(game, state);
        SimulationEvent event;
        event.kind = SimulationEventKind::ItemDropped;
        event.x = x;
        event.y = y;
        event.z = z;
        event.item = drop.item;
        event.entityId = entity;
        addEvent(events, event);
    }
    for (const auto &drop : result.items) {
        const int entity = spawnItem(game, drop.item, x + drop.x, y + drop.y, z + drop.z);
        SimulationEvent event;
        event.kind = SimulationEventKind::ItemDropped;
        event.x = x;
        event.y = y;
        event.z = z;
        event.item = drop.item;
        event.entityId = entity;
        addEvent(events, event);
    }
    for (int amount : result.experience) {
        const int entity = spawnExperience(game, amount, x + .5, y + .5, z + .5);
        SimulationEvent event;
        event.kind = SimulationEventKind::ExperienceDropped;
        event.x = x;
        event.y = y;
        event.z = z;
        event.amount = static_cast<float>(amount);
        event.entityId = entity;
        addEvent(events, event);
    }
    return true;
}
void Simulation::tickDroppedEntities(Game &game) {
    if (xpCooldown_ > 0)
        --xpCooldown_;
    const auto pickup = game.player.bounds().expand(1, .5, 1);
    const auto previousInventory = game.inventory;
    for (auto &pair : droppedEntities_) {
        auto &entity = pair.second;
        if (entity.dead || !pickup.intersects(entity.bounds))
            continue;
        const int before = entity.item.count;
        const auto item = entity.item;
        if (drop_entities::collect(entity, game, xpCooldown_, settings.playerName)) {
            // Mirror S0D before publishDroppedEntity erases the world entity.
            // The published snapshot still contains the pre-pickup stack/NBT.
            game.queuePickup(entity.id,game.entityId);
            SimulationEvent event;
            event.kind = SimulationEventKind::ItemCollected;
            event.entityId = entity.id;
            event.item = item;
            event.amount =
                entity.orb ? static_cast<float>(entity.value) : static_cast<float>(before - entity.item.count);
            addEvent(events, event);
        }
    }
    for (std::size_t slot = 9; slot < 45; ++slot)
        if (previousInventory[slot].count != game.inventory[slot].count ||
            previousInventory[slot].id != game.inventory[slot].id ||
            previousInventory[slot].damage != game.inventory[slot].damage ||
            previousInventory[slot].nbt != game.inventory[slot].nbt) {
            ++game.inventorySlotRevision[slot];
            if (game.window.open && game.window.slots.size() >= 36)
                game.window.slots[game.window.slots.size() - 36 + slot - 9] = game.inventory[slot];
        }
    // A two-block spatial grid bounds merge searches independently of item count.
    std::map<Cell, std::set<int>> cells;
    for (const auto &pair : droppedEntities_)
        if (!pair.second.dead && !pair.second.orb)
            cells[cell(pair.second)].insert(pair.first);
    for (auto &pair : droppedEntities_) {
        auto &entity = pair.second;
        const auto previousCell = cell(entity);
        drop_entities::tick(entity, game.world, game.health > 0 ? &game.player : nullptr, rainAt(game, entity),
                            [&](drop_entities::State &current) {
                                const auto nextCell = cell(current);
                                if (previousCell != nextCell) {
                                    cells[previousCell].erase(current.id);
                                    cells[nextCell].insert(current.id);
                                }
                                if (!current.dead) {
                                    const auto near = current.bounds.expand(.5, 0, .5);
                                    std::set<int> candidates;
                                    for (int x = floorBlock(near.minX / 2); x <= floorBlock(near.maxX / 2); ++x)
                                        for (int y = floorBlock(near.minY / 2); y <= floorBlock(near.maxY / 2); ++y)
                                            for (int z = floorBlock(near.minZ / 2); z <= floorBlock(near.maxZ / 2);
                                                 ++z) {
                                                const auto found = cells.find({x, y, z});
                                                if (found != cells.end())
                                                    candidates.insert(found->second.begin(), found->second.end());
                                            }
                                    for (int id : candidates) {
                                        auto &other = droppedEntities_.at(id);
                                        if (near.intersects(other.bounds))
                                            drop_entities::combine(current, other);
                                    }
                                }
                            });
    }
    for (auto iterator = droppedEntities_.begin(); iterator != droppedEntities_.end();) {
        publishDroppedEntity(game, iterator->second);
        if (iterator->second.dead)
            iterator = droppedEntities_.erase(iterator);
        else
            ++iterator;
    }
}

} // namespace mc
