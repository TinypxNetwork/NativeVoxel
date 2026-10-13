#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "mc/simulation.hpp"
#include "mc/crafting.hpp"
#include "mc/registry.hpp"
#include "mc/item_nbt.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <zlib.h>
#ifdef _WIN32
#include <windows.h>
#endif

namespace mc {
namespace {
using net::ProtocolError;
namespace fs = std::filesystem;
constexpr std::uint64_t RandomMask = (1ull << 48) - 1;
constexpr std::size_t MaxStateBytes = net::MaxPacketSize;
void offline(const Game &game) {
    if (game.online)
        throw std::logic_error("Singleplayer simulation cannot modify an online Game");
}
void finite(double value, const char *what) {
    if (!std::isfinite(value))
        throw ProtocolError(std::string("Non-finite simulation ") + what);
}
void normalize(ItemStack &stack) {
    if (stack.count <= 0 || stack.id < 0)
        stack = ItemStack{};
}
bool sameItem(const ItemStack &a, const ItemStack &b) {
    return a.empty() && b.empty() ? true
                                  : (!a.empty() && !b.empty() && a.id == b.id && a.damage == b.damage &&
                                     nbt::parse(a.nbt) == nbt::parse(b.nbt));
}
nbt::Tag real(int type, double value) {
    nbt::Tag tag;
    tag.type = type;
    tag.number = value;
    return tag;
}
nbt::Tag compound() {
    nbt::Tag tag;
    tag.type = 10;
    return tag;
}
nbt::Tag list(int subtype) {
    nbt::Tag tag;
    tag.type = 9;
    tag.subtype = subtype;
    return tag;
}
std::int64_t integer(const nbt::Tag &tag, const char *key, std::int64_t fallback = 0) {
    const auto *value = tag.get(key);
    return value ? value->integer : fallback;
}
double number(const nbt::Tag &tag, const char *key, double fallback = 0) {
    const auto *value = tag.get(key);
    return !value ? fallback : (value->type >= 5 ? value->number : static_cast<double>(value->integer));
}
const nbt::Tag &child(const nbt::Tag &tag, const char *key, int type) {
    const auto *value = tag.get(key);
    if (!value || value->type != type)
        throw ProtocolError(std::string("Missing/invalid simulation tag ") + key);
    return *value;
}
int signExtend(std::uint64_t value, int bits) {
    const auto mask = (1ull << bits) - 1;
    value &= mask;
    return static_cast<int>((value & (1ull << (bits - 1)))
                                ? static_cast<std::int64_t>(value) - static_cast<std::int64_t>(mask + 1)
                                : value);
}
nbt::Tag itemTag(const ItemStack &stack, int slot) {
    auto tag = compound();
    tag.compound["Slot"] = nbt::integer(1, slot);
    tag.compound["id"] = nbt::text(registry::item(stack.id).name);
    tag.compound["Count"] = nbt::integer(1, stack.count);
    tag.compound["Damage"] = nbt::integer(2, stack.damage);
    if (!stack.nbt.empty() && stack.nbt[0])
        tag.compound["tag"] = nbt::parse(stack.nbt);
    return tag;
}
ItemStack readItem(const nbt::Tag &tag) {
    ItemStack stack;
    const auto *id = tag.get("id");
    if (!id)
        throw ProtocolError("Simulation item has no ID");
    if (id->type == 8) {
        for (int i = 0; i <= 2267; ++i)
            if (registry::item(i).valid && id->string == registry::item(i).name) {
                stack.id = static_cast<std::int16_t>(i);
                break;
            }
    } else
        stack.id = static_cast<std::int16_t>(id->integer);
    if (!registry::item(stack.id).valid)
        throw ProtocolError("Unknown simulation item ID");
    const auto count = integer(tag, "Count"), damage = integer(tag, "Damage");
    if (count < 1 || count > 127 || damage < 0 || damage > 32767)
        throw ProtocolError("Invalid saved item count/damage");
    stack.count = static_cast<std::int8_t>(count);
    stack.damage = static_cast<std::int16_t>(damage);
    if (const auto *nbt = tag.get("tag"))
        stack.nbt = nbt::encode(*nbt);
    return stack;
}
bool canSmelt(const FurnaceState &furnace) {
    if (furnace.slots[0].empty())
        return false;
    const auto result = crafting::smeltingResult(furnace.slots[0]);
    if (result.empty())
        return false;
    const auto &output = furnace.slots[2];
    if (output.empty())
        return true;
    if (output.id != result.id || output.damage != result.damage)
        return false;
    return (output.count < 64 && output.count < registry::item(output.id).maxStack) ||
           output.count < registry::item(result.id).maxStack;
}
bool headInWater(const Game &game) {
    const double eye = game.player.eyeY();
    const int y = floorBlock(eye);
    const auto state = game.world.getBlock(floorBlock(game.player.x), y, floorBlock(game.player.z));
    if (blockId(state) != 8 && blockId(state) != 9)
        return false;
    unsigned meta = blockMeta(state);
    if (meta >= 8)
        meta = 0;
    const float offset = static_cast<float>(meta + 1) / 9.0f - .11111111f;
    return eye < static_cast<double>(static_cast<float>(y + 1) - offset);
}
int distanceCentimeters(double squared) {
    const float length = static_cast<float>(std::sqrt(squared));
    const float scaled = length * 100.0f;
    if (std::isnan(scaled))
        return 0;
    if (scaled >= static_cast<float>(std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();
    return static_cast<int>(std::floor(static_cast<double>(scaled) + .5));
}
void addEvent(std::deque<SimulationEvent> &events, SimulationEvent event) {
    events.push_back(std::move(event));
    if (events.size() > 4096)
        events.pop_front();
}
} // namespace

std::uint64_t Simulation::key(int x, int y, int z) {
    if (x < -30000000 || x > 30000000 || z < -30000000 || z > 30000000 || y < 0 || y >= 256)
        throw ProtocolError("Simulation block position outside world bounds");
    return ((static_cast<std::uint64_t>(x) & 0x3ffffffull) << 38) | ((static_cast<std::uint64_t>(y) & 0xfffull) << 26) |
           (static_cast<std::uint64_t>(z) & 0x3ffffffull);
}
int Simulation::nextInt(int bound) {
    if (bound <= 0)
        throw std::invalid_argument("Random bound must be positive");
    auto next = [&] {
        randomState_ = (randomState_ * 0x5deece66dull + 11) & RandomMask;
        return static_cast<std::uint32_t>(randomState_ >> 17);
    };
    if ((bound & -bound) == bound)
        return static_cast<int>((static_cast<std::uint64_t>(bound) * next()) >> 31);
    for (;;) {
        const auto bits = next(), value = bits % static_cast<unsigned>(bound);
        if (static_cast<std::uint64_t>(bits) - value + static_cast<unsigned>(bound - 1) < 0x80000000ull)
            return static_cast<int>(value);
    }
}
SimulationCoverage Simulation::coverage() const {
    SimulationCoverage value;
    value.food = value.time = value.weather = value.furnace = value.brewing = true;
    value.simpleBlockDrops = value.itemEntities = value.experience = true;
    return value;
}
void Simulation::start(Game &game, std::uint64_t seed, bool resetState) {
    offline(game);
    if (settings.difficulty < 0 || settings.difficulty > 3 || settings.gameMode < 0 || settings.gameMode > 3)
        throw ProtocolError("Invalid simulation difficulty/game mode");
    seed_ = seed;
    randomState_ = (seed ^ 0x5deece66dull) & RandomMask;
    running_ = true;
    ticks_ = 0;
    events.clear();
    furnaceOpen_ = brewingOpen_ = false;
    openStorage_.clear();
    storageSnapshot_.clear();
    enderOpen_ = false;
    playerRandomState_ = (seed ^ 0x8e1d189ull ^ 0x5deece66dull) & RandomMask;
    mathRandomState_ = (seed ^ 0x4d415448ull ^ 0x5deece66dull) & RandomMask;
    entitySeedState_ = (seed ^ 0x454e54495459ull ^ 0x5deece66dull) & RandomMask;
    xpCooldown_ = creativeDropThreshold_ = 0;
    lastInteractionError.clear();
    if (resetState) {
        for (const auto &entity : droppedEntities_)
            game.entities.erase(entity.first);
        droppedEntities_.clear();
        nextEntityId_ = 1000000;
    }
    if (resetState) {
        food = FoodState{};
        weather = TimeWeatherState{};
        furnaces_.clear();
        brewingStands_.clear();
        enderItems = {};
        game.health = game.maxHealth = 20;
        game.food = 20;
        game.saturation = 5;
    } else {
        weather.totalTime = game.worldAge;
        weather.dayTime = game.worldTime;
        weather.raining = game.raining;
        weather.rainStrength = game.rainStrength;
        weather.thunderStrength = game.thunderStrength;
    }
    game.joined = game.spawned = true;
    game.status = "Native singleplayer simulation";
    syncGame(game);
}
void Simulation::syncGame(Game &game) {
    game.difficulty = settings.difficulty;
    game.gameMode = settings.gameMode;
    game.creativeMode = settings.gameMode == 1;
    game.player.spectator = settings.gameMode == 3;
    game.invulnerable = game.creativeMode || game.player.spectator;
    game.player.allowFlying = game.invulnerable;
    game.player.noClip = game.player.spectator;
    if (game.player.spectator)
        game.player.flying = true;
    else if (!game.creativeMode)
        game.player.flying = false;
    game.worldAge = weather.totalTime;
    game.worldTime = weather.dayTime;
    game.raining = weather.raining;
    game.rainStrength = weather.rainStrength;
    game.thunderStrength = weather.thunderStrength;
    game.player.foodLevel = game.food;
    game.player.dead = game.health <= 0;
}
void Simulation::addExhaustion(float amount) {
    finite(amount, "exhaustion");
    food.exhaustion = std::min(food.exhaustion + amount, 40.0f);
}
void Simulation::tickFood(Game &game) {
    offline(game);
    food.previousFoodLevel = game.food;
    if (food.exhaustion > 4.0f) {
        food.exhaustion -= 4.0f;
        if (game.saturation > 0)
            game.saturation = std::max(game.saturation - 1.0f, 0.0f);
        else if (settings.difficulty != 0)
            game.food = std::max(game.food - 1, 0);
    }
    if (settings.naturalRegeneration && game.food >= 18 && game.health > 0 && game.health < game.maxHealth) {
        ++food.timer;
        if (food.timer >= 80) {
            const float before = game.health;
            game.health = std::min(game.health + 1.0f, game.maxHealth);
            addExhaustion(3.0f);
            food.timer = 0;
            SimulationEvent event;
            event.kind = SimulationEventKind::PlayerHealed;
            event.amount = game.health - before;
            addEvent(events, event);
        }
    } else if (game.food <= 0) {
        ++food.timer;
        if (food.timer >= 80) {
            if ((game.health > 10.0f || settings.difficulty == 3 || (game.health > 1.0f && settings.difficulty == 2)) &&
                !game.invulnerable && game.health > 0) {
                const float before = game.health;
                game.health = std::max(game.health - 1.0f, 0.0f);
                SimulationEvent event;
                event.kind = SimulationEventKind::PlayerDamaged;
                event.amount = before - game.health;
                addEvent(events, event);
            }
            food.timer = 0;
        }
    } else
        food.timer = 0;
    game.player.foodLevel = game.food;
    game.player.dead = game.health <= 0;
}
void Simulation::tickTimeWeather(Game &game) {
    offline(game);
    ++weather.totalTime;
    if (settings.daylightCycle)
        ++weather.dayTime;
    if (game.dimension == 0 && settings.weatherCycle) {
        if (weather.cleanWeatherTime > 0) {
            --weather.cleanWeatherTime;
            weather.thunderTime = weather.thundering ? 1 : 2;
            weather.rainTime = weather.raining ? 1 : 2;
        }
        if (weather.thunderTime <= 0)
            weather.thunderTime = weather.thundering ? nextInt(12000) + 3600 : nextInt(168000) + 12000;
        else if (--weather.thunderTime <= 0)
            weather.thundering = !weather.thundering;
        weather.previousThunderStrength = weather.thunderStrength;
        weather.thunderStrength = std::clamp(
            static_cast<float>(static_cast<double>(weather.thunderStrength) + (weather.thundering ? .01 : -.01)), 0.0f,
            1.0f);
        if (weather.rainTime <= 0)
            weather.rainTime = weather.raining ? nextInt(12000) + 12000 : nextInt(168000) + 12000;
        else if (--weather.rainTime <= 0)
            weather.raining = !weather.raining;
        weather.previousRainStrength = weather.rainStrength;
        weather.rainStrength = std::clamp(
            static_cast<float>(static_cast<double>(weather.rainStrength) + (weather.raining ? .01 : -.01)), 0.0f, 1.0f);
    }
    game.worldAge = weather.totalTime;
    game.worldTime = weather.dayTime;
    game.raining = weather.raining;
    game.rainStrength = weather.rainStrength;
    game.thunderStrength = weather.thunderStrength;
}
void Simulation::tickFurnace(FurnaceState &furnace) {
    const bool burning = furnace.burnTime > 0;
    bool changed = false;
    if (burning)
        --furnace.burnTime;
    if (furnace.burnTime > 0 || (!furnace.slots[1].empty() && !furnace.slots[0].empty())) {
        if (furnace.burnTime <= 0 && canSmelt(furnace)) {
            furnace.currentItemBurnTime = furnace.burnTime = crafting::fuelBurnTime(furnace.slots[1]);
            if (furnace.burnTime > 0) {
                changed = true;
                if (!furnace.slots[1].empty()) {
                    const auto container = crafting::containerItem(furnace.slots[1]);
                    --furnace.slots[1].count;
                    if (furnace.slots[1].count == 0)
                        furnace.slots[1] = container;
                }
            }
        }
        if (furnace.burnTime > 0 && canSmelt(furnace)) {
            ++furnace.cookTime;
            if (furnace.cookTime == furnace.totalCookTime) {
                furnace.cookTime = 0;
                furnace.totalCookTime = 200;
                const auto result = crafting::smeltingResult(furnace.slots[0]);
                if (furnace.slots[2].empty())
                    furnace.slots[2] = result;
                else if (furnace.slots[2].id == result.id)
                    ++furnace.slots[2].count;
                if (furnace.slots[0].id == 19 && furnace.slots[0].damage == 1 && !furnace.slots[1].empty() &&
                    furnace.slots[1].id == 325) {
                    furnace.slots[1] = ItemStack{};
                    furnace.slots[1].id = 326;
                    furnace.slots[1].count = 1;
                }
                --furnace.slots[0].count;
                normalize(furnace.slots[0]);
                changed = true;
            }
        } else
            furnace.cookTime = 0;
    } else if (furnace.burnTime <= 0 && furnace.cookTime > 0)
        furnace.cookTime = std::clamp(furnace.cookTime - 2, 0, furnace.totalCookTime);
    if (burning != (furnace.burnTime > 0))
        changed = true;
    furnace.dirty = furnace.dirty || changed;
}
FurnaceState &Simulation::furnaceAt(int x, int y, int z) { return furnaces_[key(x, y, z)]; }
BrewingState &Simulation::brewingAt(int x, int y, int z) { return brewingStands_[key(x, y, z)]; }
void Simulation::commitContainer(Game &game) {
    offline(game);
    commitStorage(game);
    if (brewingOpen_ && game.window.open && game.window.type == "minecraft:brewing_stand" &&
        game.window.slots.size() >= 4) {
        auto &stand = brewingStands_[openBrewing_];
        for (std::size_t i = 0; i < 4; ++i) {
            if (!sameItem(stand.slots[i], game.window.slots[i]) || stand.slots[i].count != game.window.slots[i].count)
                stand.dirty = true;
            stand.slots[i] = game.window.slots[i];
        }
    }
    if (!furnaceOpen_ || !game.window.open || game.window.type != "minecraft:furnace" || game.window.slots.size() < 3)
        return;
    auto &furnace = furnaces_[openFurnace_];
    for (std::size_t i = 0; i < 3; ++i) {
        auto stack = game.window.slots[i];
        if (stack.count > 64)
            stack.count = 64;
        const bool equivalent = sameItem(stack, furnace.slots[i]);
        if (!equivalent || stack.count != furnace.slots[i].count)
            furnace.dirty = true;
        furnace.slots[i] = std::move(stack);
        if (i == 0 && !equivalent) {
            furnace.totalCookTime = 200;
            furnace.cookTime = 0;
        }
    }
}
bool Simulation::interactBlock(Game &game, int x, int y, int z) {
    offline(game);
    if (!running_)
        return false;
    if (interactStorage(game, x, y, z))
        return true;
    const auto id = blockId(game.world.getBlock(x, y, z));
    if (id != 61 && id != 62 && id != 58 && id != 117)
        return false;
    commitContainer(game);
    if (game.window.open) {
        beforeContainerClose(game);
        game.window = {};
    }
    if (const auto tile = game.tileEntities.find(key(x, y, z)); tile != game.tileEntities.end())
        importTileEntity(game, tile->second);
    ContainerWindow window;
    window.open = true;
    window.id = nextWindowId_;
    nextWindowId_ = nextWindowId_ % 100 + 1;
    furnaceOpen_ = id == 61 || id == 62;
    brewingOpen_ = id == 117;
    if (id == 58) {
        window.type = "minecraft:crafting_table";
        window.title = "Crafting";
        window.slots.resize(46);
    } else if (id == 117) {
        openBrewing_ = key(x, y, z);
        auto &stand = brewingStands_[openBrewing_];
        window.type = "minecraft:brewing_stand";
        window.title = "Brewing Stand";
        window.slots.resize(40);
        std::copy(stand.slots.begin(), stand.slots.end(), window.slots.begin());
        window.properties = {{0, stand.brewTime}};
    } else {
        openFurnace_ = key(x, y, z);
        auto &furnace = furnaces_[openFurnace_];
        window.type = "minecraft:furnace";
        window.title = "Furnace";
        window.slots.resize(39);
        std::copy(furnace.slots.begin(), furnace.slots.end(), window.slots.begin());
        window.properties = {
            {0, furnace.burnTime}, {1, furnace.currentItemBurnTime}, {2, furnace.cookTime}, {3, furnace.totalCookTime}};
    }
    for (std::size_t i = 0; i < 36; ++i)
        window.slots[window.slots.size() - 36 + i] = game.inventory[9 + i];
    game.window = std::move(window);
    ++game.windowRevision;
    return true;
}
bool Simulation::prepareCreativeItem(Game &game, ItemStack &stack) {
    auto tag = nbt::parse(stack.nbt);
    const auto *reference = tag.get("BlockEntityTag");
    if (!reference || reference->type != 10 || !reference->get("x") || !reference->get("y") || !reference->get("z"))
        return true;
    auto coordinate = [](const nbt::Tag &value) {
        if (value.type >= 1 && value.type <= 4) {
            const auto raw = static_cast<std::uint32_t>(value.integer);
            std::int32_t out;
            std::memcpy(&out, &raw, 4);
            return out;
        }
        if (value.type == 5 || value.type == 6) {
            if (std::isnan(value.number))
                return 0;
            if (value.number <= std::numeric_limits<int>::min())
                return std::numeric_limits<int>::min();
            if (value.number >= std::numeric_limits<int>::max())
                return std::numeric_limits<int>::max();
            return static_cast<int>(std::floor(value.number));
        }
        return 0;
    };
    const int x = coordinate(*reference->get("x")), y = coordinate(*reference->get("y")),
              z = coordinate(*reference->get("z"));
    if (x < -30000000 || x >= 30000000 || z < -30000000 || z >= 30000000 || y < 0 || y >= 256)
        return true;
    const auto id = blockId(game.world.getBlock(x, y, z));
    bool tile = false;
    switch (id) {
    case 23:
    case 25:
    case 36:
    case 52:
    case 54:
    case 61:
    case 62:
    case 63:
    case 68:
    case 84:
    case 116:
    case 117:
    case 119:
    case 130:
    case 137:
    case 138:
    case 140:
    case 144:
    case 146:
    case 149:
    case 150:
    case 151:
    case 154:
    case 158:
    case 176:
    case 177:
    case 178:
        tile = true;
        break;
    default:
        break;
    }
    if (!tile)
        return true; // Original getTileEntity returns null; the supplied tag is unchanged.
    commitContainer(game);
    const auto packed = key(x, y, z);
    nbt::Tag snapshot;
    const auto furnace = furnaces_.find(packed), unused = furnaces_.end();
    if ((id == 61 || id == 62) && furnace != unused) {
        const auto &machine = furnace->second;
        snapshot = compound();
        snapshot.compound["id"] = nbt::text("Furnace");
        snapshot.compound["BurnTime"] = nbt::integer(2, machine.burnTime);
        snapshot.compound["CookTime"] = nbt::integer(2, machine.cookTime);
        snapshot.compound["CookTimeTotal"] = nbt::integer(2, machine.totalCookTime);
        auto items = list(10);
        for (int slot = 0; slot < 3; ++slot)
            if (!machine.slots[static_cast<std::size_t>(slot)].empty())
                items.list.push_back(itemTag(machine.slots[static_cast<std::size_t>(slot)], slot));
        snapshot.compound["Items"] = std::move(items);
    } else if (id == 117 && brewingStands_.count(packed)) {
        const auto &machine = brewingStands_.at(packed);
        snapshot = compound();
        snapshot.compound["id"] = nbt::text("Cauldron");
        snapshot.compound["BrewTime"] = nbt::integer(2, machine.brewTime);
        auto items = list(10);
        for (int slot = 0; slot < 4; ++slot)
            if (!machine.slots[static_cast<std::size_t>(slot)].empty())
                items.list.push_back(itemTag(machine.slots[static_cast<std::size_t>(slot)], slot));
        snapshot.compound["Items"] = std::move(items);
    } else if (const auto found = game.tileEntities.find(packed); found != game.tileEntities.end()) {
        snapshot = nbt::parse(found->second.nbt);
    }
    if (snapshot.type != 10 || !snapshot.get("id")) {
        lastInteractionError = "Authoritative tile data is unavailable for this creative copy";
        return false;
    }
    snapshot.compound.erase("x");
    snapshot.compound.erase("y");
    snapshot.compound.erase("z");
    tag.compound["BlockEntityTag"] = std::move(snapshot);
    stack.nbt = nbt::encode(tag);
    return true;
}
void Simulation::tick(Game &game, const Input &input) {
    offline(game);
    if (!running_)
        throw std::logic_error("Simulation has not been started");
    syncGame(game);
    commitContainer(game);
    if (creativeDropThreshold_ > 0)
        --creativeDropThreshold_;
    const double x = game.player.x, y = game.player.y, z = game.player.z;
    game.tick(input);
    ++ticks_;
    tickDroppedEntities(game);
    if (!game.invulnerable && game.vehicleId < 0) {
        if (game.player.jumpedLastTick)
            addExhaustion(game.player.sprinting ? .8f : .2f);
        const double dx = game.player.x - x, dy = game.player.y - y, dz = game.player.z - z;
        int distance = 0;
        if (headInWater(game)) {
            distance = distanceCentimeters(dx * dx + dy * dy + dz * dz);
            if (distance > 0)
                addExhaustion(.015f * static_cast<float>(distance) * .01f);
        } else if (game.player.inWater) {
            distance = distanceCentimeters(dx * dx + dz * dz);
            if (distance > 0)
                addExhaustion(.015f * static_cast<float>(distance) * .01f);
        } else {
            const auto block = blockId(game.world.getBlock(
                floorBlock(game.player.x), floorBlock(game.player.bounds().minY), floorBlock(game.player.z)));
            if (block != 65 && block != 106 && game.player.onGround) {
                distance = distanceCentimeters(dx * dx + dz * dz);
                if (distance > 0)
                    addExhaustion((game.player.sprinting ? .099999994f : .01f) * static_cast<float>(distance) * .01f);
            }
        }
    }
    if (settings.difficulty == 0 && settings.naturalRegeneration) {
        if (game.health > 0 && game.health < game.maxHealth && game.player.ticks % 20 == 0)
            game.health = std::min(game.health + 1.0f, game.maxHealth);
        if (game.food < 20 && game.player.ticks % 10 == 0)
            ++game.food;
    }
    tickFood(game);
    tickTimeWeather(game);
    for (auto &pair : furnaces_) {
        const int bx = signExtend(pair.first >> 38, 26), by = signExtend(pair.first >> 26, 12),
                  bz = signExtend(pair.first, 26);
        const auto before = game.world.getBlock(bx, by, bz);
        if ((blockId(before) != 61 && blockId(before) != 62) || !game.world.hasChunk(floorDiv16(bx), floorDiv16(bz)))
            continue;
        tickFurnace(pair.second);
        const auto after = blockState(pair.second.burnTime > 0 ? 62 : 61, blockMeta(before));
        if (after != before) {
            game.world.setBlock(bx, by, bz, after);
            SimulationEvent event;
            event.kind = SimulationEventKind::BlockChanged;
            event.x = bx;
            event.y = by;
            event.z = bz;
            event.before = before;
            event.after = after;
            addEvent(events, event);
        }
    }
    for (auto &pair : brewingStands_) {
        const int bx = signExtend(pair.first >> 38, 26), by = signExtend(pair.first >> 26, 12),
                  bz = signExtend(pair.first, 26);
        const auto before = game.world.getBlock(bx, by, bz);
        if (blockId(before) != 117 || !game.world.hasChunk(floorDiv16(bx), floorDiv16(bz)))
            continue;
        brewing::tick(pair.second);
        const auto after = blockState(117, static_cast<unsigned>(pair.second.filledMask));
        if (after != before) {
            game.world.setBlock(bx, by, bz, after);
            SimulationEvent event;
            event.kind = SimulationEventKind::BlockChanged;
            event.x = bx;
            event.y = by;
            event.z = bz;
            event.before = before;
            event.after = after;
            addEvent(events, event);
        }
    }
    if (furnaceOpen_ && game.window.open && game.window.type == "minecraft:furnace" && game.window.slots.size() >= 3) {
        const auto &furnace = furnaces_.at(openFurnace_);
        std::copy(furnace.slots.begin(), furnace.slots.end(), game.window.slots.begin());
        game.window.properties = {
            {0, furnace.burnTime}, {1, furnace.currentItemBurnTime}, {2, furnace.cookTime}, {3, furnace.totalCookTime}};
    }
    if (brewingOpen_ && game.window.open && game.window.type == "minecraft:brewing_stand" &&
        game.window.slots.size() >= 4) {
        const auto &stand = brewingStands_.at(openBrewing_);
        std::copy(stand.slots.begin(), stand.slots.end(), game.window.slots.begin());
        game.window.properties = {{0, stand.brewTime}};
    }
    syncGame(game);
}

nbt::Tag Simulation::saveState(const Game &game) const {
    offline(game);
    auto root = compound();
    root.compound["NativeVoxelSimulation"] = nbt::integer(3, 1);
    root.compound["Seed"] = nbt::integer(4, static_cast<std::int64_t>(seed_));
    root.compound["Ticks"] = nbt::integer(4, static_cast<std::int64_t>(ticks_));
    root.compound["RandomState"] = nbt::integer(4, static_cast<std::int64_t>(randomState_));
    root.compound["PlayerRandomState"] = nbt::integer(4, static_cast<std::int64_t>(playerRandomState_));
    root.compound["MathRandomState"] = nbt::integer(4, static_cast<std::int64_t>(mathRandomState_));
    root.compound["EntitySeedState"] = nbt::integer(4, static_cast<std::int64_t>(entitySeedState_));
    root.compound["NextEntityId"] = nbt::integer(3, nextEntityId_);
    root.compound["XpCooldown"] = nbt::integer(3, xpCooldown_);
    auto options = compound();
    options.compound["Difficulty"] = nbt::integer(1, settings.difficulty);
    options.compound["GameMode"] = nbt::integer(1, settings.gameMode);
    options.compound["naturalRegeneration"] = nbt::integer(1, settings.naturalRegeneration);
    options.compound["doDaylightCycle"] = nbt::integer(1, settings.daylightCycle);
    options.compound["WeatherCycle"] = nbt::integer(1, settings.weatherCycle);
    root.compound["Settings"] = std::move(options);
    root.compound["Settings"].compound["doTileDrops"] = nbt::integer(1, settings.tileDrops);
    root.compound["Settings"].compound["PlayerName"] = nbt::text(settings.playerName);
    auto time = compound();
    time.compound["Time"] = nbt::integer(4, weather.totalTime);
    time.compound["DayTime"] = nbt::integer(4, weather.dayTime);
    time.compound["clearWeatherTime"] = nbt::integer(3, weather.cleanWeatherTime);
    time.compound["rainTime"] = nbt::integer(3, weather.rainTime);
    time.compound["thunderTime"] = nbt::integer(3, weather.thunderTime);
    time.compound["raining"] = nbt::integer(1, weather.raining);
    time.compound["thundering"] = nbt::integer(1, weather.thundering);
    time.compound["RainStrength"] = real(5, weather.rainStrength);
    time.compound["ThunderStrength"] = real(5, weather.thunderStrength);
    root.compound["Data"] = std::move(time);
    auto player = compound();
    player.compound["Health"] = real(5, game.health);
    player.compound["MaxHealth"] = real(5, game.maxHealth);
    player.compound["foodLevel"] = nbt::integer(3, game.food);
    player.compound["foodTickTimer"] = nbt::integer(3, food.timer);
    player.compound["foodSaturationLevel"] = real(5, game.saturation);
    player.compound["foodExhaustionLevel"] = real(5, food.exhaustion);
    player.compound["PreviousFoodLevel"] = nbt::integer(3, food.previousFoodLevel);
    player.compound["Dimension"] = nbt::integer(3, game.dimension);
    player.compound["SelectedItemSlot"] = nbt::integer(3, game.selectedSlot);
    player.compound["XpLevel"] = nbt::integer(3, game.experienceLevel);
    player.compound["XpTotal"] = nbt::integer(3, game.totalExperience);
    player.compound["XpP"] = real(5, game.experienceProgress);
    auto position = list(6);
    for (double value : {game.player.x, game.player.y, game.player.z})
        position.list.push_back(real(6, value));
    player.compound["Pos"] = std::move(position);
    auto motion = list(6);
    for (double value : {game.player.motionX, game.player.motionY, game.player.motionZ})
        motion.list.push_back(real(6, value));
    player.compound["Motion"] = std::move(motion);
    auto rotation = list(5);
    rotation.list.push_back(real(5, game.player.yaw));
    rotation.list.push_back(real(5, game.player.pitch));
    player.compound["Rotation"] = std::move(rotation);
    player.compound["OnGround"] = nbt::integer(1, game.player.onGround);
    auto inventory = list(10);
    for (std::size_t i = 0; i < game.inventory.size(); ++i)
        if (!game.inventory[i].empty())
            inventory.list.push_back(itemTag(game.inventory[i], static_cast<int>(i)));
    player.compound["Inventory"] = std::move(inventory);
    player.compound["EnderItems"] = item_nbt::writeSlots(enderItems);
    player.compound["Flying"] = nbt::integer(1, game.player.flying);
    root.compound["Player"] = std::move(player);
    auto furnaces = list(10);
    for (const auto &pair : furnaces_) {
        auto value = compound();
        value.compound["id"] = nbt::text("Furnace");
        value.compound["x"] = nbt::integer(3, signExtend(pair.first >> 38, 26));
        value.compound["y"] = nbt::integer(3, signExtend(pair.first >> 26, 12));
        value.compound["z"] = nbt::integer(3, signExtend(pair.first, 26));
        value.compound["BurnTime"] = nbt::integer(2, pair.second.burnTime);
        value.compound["CookTime"] = nbt::integer(2, pair.second.cookTime);
        value.compound["CookTimeTotal"] = nbt::integer(2, pair.second.totalCookTime);
        auto items = list(10);
        for (int i = 0; i < 3; ++i)
            if (!pair.second.slots[static_cast<std::size_t>(i)].empty())
                items.list.push_back(itemTag(pair.second.slots[static_cast<std::size_t>(i)], i));
        value.compound["Items"] = std::move(items);
        furnaces.list.push_back(std::move(value));
    }
    root.compound["Furnaces"] = std::move(furnaces);
    auto stands = list(10);
    for (const auto &pair : brewingStands_) {
        auto value = compound();
        value.compound["id"] = nbt::text("Cauldron");
        value.compound["x"] = nbt::integer(3, signExtend(pair.first >> 38, 26));
        value.compound["y"] = nbt::integer(3, signExtend(pair.first >> 26, 12));
        value.compound["z"] = nbt::integer(3, signExtend(pair.first, 26));
        value.compound["BrewTime"] = nbt::integer(2, pair.second.brewTime);
        auto items = list(10);
        for (int i = 0; i < 4; ++i)
            if (!pair.second.slots[static_cast<std::size_t>(i)].empty())
                items.list.push_back(itemTag(pair.second.slots[static_cast<std::size_t>(i)], i));
        value.compound["Items"] = std::move(items);
        stands.list.push_back(std::move(value));
    }
    root.compound["BrewingStands"] = std::move(stands);
    auto entities = list(10);
    for (const auto &pair : droppedEntities_) {
        const auto &entity = pair.second;
        if (entity.dead)
            continue;
        auto value = compound();
        value.compound["id"] = nbt::text(entity.orb ? "XPOrb" : "Item");
        value.compound["NativeId"] = nbt::integer(3, entity.id);
        value.compound["Age"] = nbt::integer(2, entity.age);
        value.compound["PickupDelay"] = nbt::integer(2, entity.pickupDelay);
        value.compound["Health"] = nbt::integer(2, entity.health);
        value.compound["Fire"] = nbt::integer(2, entity.fire);
        value.compound["Ticks"] = nbt::integer(3, entity.ticks);
        value.compound["Value"] = nbt::integer(2, entity.value);
        value.compound["Color"] = nbt::integer(3, entity.color);
        value.compound["TargetColor"] = nbt::integer(3, entity.targetColor);
        value.compound["HasTarget"] = nbt::integer(1, entity.hasTarget);
        value.compound["OnGround"] = nbt::integer(1, entity.onGround);
        value.compound["NoClip"] = nbt::integer(1, entity.noClip);
        value.compound["InWeb"] = nbt::integer(1, entity.inWeb);
        value.compound["InWater"] = nbt::integer(1, entity.inWater);
        value.compound["FirstUpdate"] = nbt::integer(1, entity.firstUpdate);
        value.compound["OutsideBorder"] = nbt::integer(1, entity.outsideBorder);
        value.compound["Width"] = real(5, entity.width);
        value.compound["Height"] = real(5, entity.height);
        value.compound["Yaw"] = real(5, entity.yaw);
        value.compound["HoverStart"] = real(5, entity.hoverStart);
        value.compound["FallDistance"] = real(5, entity.fallDistance);
        value.compound["RandomState"] = nbt::integer(4, static_cast<std::int64_t>(entity.random.internalSeed()));
        auto position = list(6);
        for (double v : {entity.x, entity.y, entity.z})
            position.list.push_back(real(6, v));
        value.compound["Pos"] = std::move(position);
        auto motion = list(6);
        for (double v : {entity.motionX, entity.motionY, entity.motionZ})
            motion.list.push_back(real(6, v));
        value.compound["Motion"] = std::move(motion);
        // Exact translated bounds preserve sub-ULP differences from repeated movement.
        auto bounds = list(6);
        for (double v : {entity.bounds.minX, entity.bounds.minY, entity.bounds.minZ, entity.bounds.maxX,
                         entity.bounds.maxY, entity.bounds.maxZ})
            bounds.list.push_back(real(6, v));
        value.compound["NativeBounds"] = std::move(bounds);
        if (!entity.owner.empty())
            value.compound["Owner"] = nbt::text(entity.owner);
        if (!entity.thrower.empty())
            value.compound["Thrower"] = nbt::text(entity.thrower);
        if (!entity.orb)
            value.compound["Item"] = itemTag(entity.item, 0);
        entities.list.push_back(std::move(value));
    }
    root.compound["DroppedEntities"] = std::move(entities);
    return root;
}
void Simulation::loadState(Game &game, const nbt::Tag &root) {
    offline(game);
    if (root.type != 10 || integer(root, "NativeVoxelSimulation") != 1)
        throw ProtocolError("Unsupported simulation save version");
    Simulation loaded;
    const auto &options = child(root, "Settings", 10);
    loaded.settings.difficulty = static_cast<int>(integer(options, "Difficulty", 2));
    loaded.settings.gameMode = static_cast<int>(integer(options, "GameMode"));
    loaded.settings.naturalRegeneration = integer(options, "naturalRegeneration", 1) != 0;
    loaded.settings.daylightCycle = integer(options, "doDaylightCycle", 1) != 0;
    loaded.settings.weatherCycle = integer(options, "WeatherCycle", 1) != 0;
    if (loaded.settings.difficulty < 0 || loaded.settings.difficulty > 3 || loaded.settings.gameMode < 0 ||
        loaded.settings.gameMode > 3)
        throw ProtocolError("Invalid saved game mode/difficulty");
    loaded.seed_ = static_cast<std::uint64_t>(integer(root, "Seed", 189));
    loaded.ticks_ = static_cast<std::uint64_t>(integer(root, "Ticks"));
    loaded.randomState_ = static_cast<std::uint64_t>(integer(root, "RandomState")) & RandomMask;
    loaded.playerRandomState_ =
        static_cast<std::uint64_t>(integer(root, "PlayerRandomState",
                                           static_cast<std::int64_t>(loaded.seed_ ^ 0x8e1d189ull ^ 0x5deece66dull))) &
        RandomMask;
    loaded.mathRandomState_ =
        static_cast<std::uint64_t>(integer(root, "MathRandomState",
                                           static_cast<std::int64_t>(loaded.seed_ ^ 0x4d415448ull ^ 0x5deece66dull))) &
        RandomMask;
    loaded.entitySeedState_ =
        static_cast<std::uint64_t>(integer(
            root, "EntitySeedState", static_cast<std::int64_t>(loaded.seed_ ^ 0x454e54495459ull ^ 0x5deece66dull))) &
        RandomMask;
    loaded.nextEntityId_ = static_cast<int>(integer(root, "NextEntityId", 1000000));
    loaded.xpCooldown_ = static_cast<int>(integer(root, "XpCooldown"));
    if (loaded.nextEntityId_ < 1 || loaded.nextEntityId_ > 2000000000 || loaded.xpCooldown_ < 0 ||
        loaded.xpCooldown_ > 2)
        throw ProtocolError("Invalid saved entity allocator/cooldown");
    loaded.settings.tileDrops = integer(options, "doTileDrops", 1) != 0;
    if (const auto *name = options.get("PlayerName")) {
        if (name->type != 8 || name->string.size() > 64)
            throw ProtocolError("Invalid saved player name");
        loaded.settings.playerName = name->string;
    }
    const auto &time = child(root, "Data", 10);
    loaded.weather.totalTime = integer(time, "Time");
    loaded.weather.dayTime = integer(time, "DayTime");
    loaded.weather.cleanWeatherTime = static_cast<int>(integer(time, "clearWeatherTime"));
    loaded.weather.rainTime = static_cast<int>(integer(time, "rainTime"));
    loaded.weather.thunderTime = static_cast<int>(integer(time, "thunderTime"));
    loaded.weather.raining = integer(time, "raining") != 0;
    loaded.weather.thundering = integer(time, "thundering") != 0;
    loaded.weather.rainStrength = loaded.weather.previousRainStrength =
        static_cast<float>(number(time, "RainStrength"));
    loaded.weather.thunderStrength = loaded.weather.previousThunderStrength =
        static_cast<float>(number(time, "ThunderStrength"));
    const auto &saved = child(root, "Player", 10);
    item_nbt::readSlots(saved, loaded.enderItems, "EnderItems");
    const auto &position = child(saved, "Pos", 9);
    const auto &motion = child(saved, "Motion", 9);
    const auto &rotation = child(saved, "Rotation", 9);
    if (position.subtype != 6 || position.list.size() != 3 || motion.subtype != 6 || motion.list.size() != 3 ||
        rotation.subtype != 5 || rotation.list.size() != 2)
        throw ProtocolError("Invalid saved player vectors");
    Player player;
    player.teleport(position.list[0].number, position.list[1].number, position.list[2].number);
    player.motionX = motion.list[0].number;
    player.motionY = motion.list[1].number;
    player.motionZ = motion.list[2].number;
    player.yaw = static_cast<float>(rotation.list[0].number);
    player.pitch = static_cast<float>(rotation.list[1].number);
    player.onGround = integer(saved, "OnGround") != 0;
    player.flying = integer(saved, "Flying") != 0;
    finite(player.motionX, "motion");
    finite(player.motionY, "motion");
    finite(player.motionZ, "motion");
    finite(player.yaw, "rotation");
    finite(player.pitch, "rotation");
    const float health = static_cast<float>(number(saved, "Health", 20)),
                maximum = static_cast<float>(number(saved, "MaxHealth", 20)),
                saturation = static_cast<float>(number(saved, "foodSaturationLevel", 5));
    const int foodLevel = static_cast<int>(integer(saved, "foodLevel", 20)),
              selected = static_cast<int>(integer(saved, "SelectedItemSlot"));
    if (selected < 0 || selected > 8)
        throw ProtocolError("Invalid saved selected slot");
    finite(health, "health");
    finite(maximum, "max health");
    finite(saturation, "saturation");
    loaded.food.exhaustion = static_cast<float>(number(saved, "foodExhaustionLevel"));
    finite(loaded.food.exhaustion, "exhaustion");
    loaded.food.timer = static_cast<int>(integer(saved, "foodTickTimer"));
    loaded.food.previousFoodLevel = static_cast<int>(integer(saved, "PreviousFoodLevel", 20));
    std::array<ItemStack, 45> inventory{};
    const auto &items = child(saved, "Inventory", 9);
    if (items.subtype != 10 || items.list.size() > 45)
        throw ProtocolError("Invalid saved inventory");
    for (const auto &item : items.list) {
        const auto slot = integer(item, "Slot", -1);
        if (slot < 0 || slot >= 45 || !inventory[static_cast<std::size_t>(slot)].empty())
            throw ProtocolError("Invalid/duplicate saved inventory slot");
        inventory[static_cast<std::size_t>(slot)] = readItem(item);
    }
    const auto &furnaces = child(root, "Furnaces", 9);
    if (furnaces.subtype != 10 || furnaces.list.size() > 4096)
        throw ProtocolError("Invalid furnace save list");
    for (const auto &tag : furnaces.list) {
        const int x = static_cast<int>(integer(tag, "x")), y = static_cast<int>(integer(tag, "y")),
                  z = static_cast<int>(integer(tag, "z"));
        const auto packed = key(x, y, z);
        if (loaded.furnaces_.count(packed))
            throw ProtocolError("Duplicate saved furnace");
        auto &furnace = loaded.furnaces_[packed];
        furnace.burnTime = static_cast<std::int16_t>(integer(tag, "BurnTime"));
        furnace.cookTime = static_cast<std::int16_t>(integer(tag, "CookTime"));
        furnace.totalCookTime = static_cast<std::int16_t>(integer(tag, "CookTimeTotal"));
        const auto &stacks = child(tag, "Items", 9);
        if (stacks.subtype != 10 || stacks.list.size() > 3)
            throw ProtocolError("Invalid furnace items");
        for (const auto &item : stacks.list) {
            const auto slot = integer(item, "Slot", -1);
            if (slot < 0 || slot > 2 || !furnace.slots[static_cast<std::size_t>(slot)].empty())
                throw ProtocolError("Invalid saved furnace slot");
            furnace.slots[static_cast<std::size_t>(slot)] = readItem(item);
        }
        furnace.currentItemBurnTime = crafting::fuelBurnTime(furnace.slots[1]);
    }
    if (const auto *stands = root.get("BrewingStands")) {
        if (stands->type != 9 || stands->subtype != 10 || stands->list.size() > 4096)
            throw ProtocolError("Invalid brewing save list");
        for (const auto &tag : stands->list) {
            const auto packed = key(static_cast<int>(integer(tag, "x")), static_cast<int>(integer(tag, "y")),
                                    static_cast<int>(integer(tag, "z")));
            if (loaded.brewingStands_.count(packed))
                throw ProtocolError("Duplicate saved brewing stand");
            auto &stand = loaded.brewingStands_[packed];
            stand.brewTime = static_cast<std::int16_t>(integer(tag, "BrewTime"));
            const auto &stacks = child(tag, "Items", 9);
            if (stacks.subtype != 10 || stacks.list.size() > 4)
                throw ProtocolError("Invalid brewing items");
            for (const auto &item : stacks.list) {
                const auto slot = integer(item, "Slot", -1);
                if (slot < 0 || slot > 3 || !stand.slots[static_cast<std::size_t>(slot)].empty())
                    throw ProtocolError("Invalid saved brewing slot");
                stand.slots[static_cast<std::size_t>(slot)] = readItem(item);
            }
        }
    }
    finite(loaded.weather.rainStrength, "rain strength");
    finite(loaded.weather.thunderStrength, "thunder strength");
    finite(number(saved, "XpP"), "experience progress");
    if (const auto *entities = root.get("DroppedEntities")) {
        if (entities->type != 9 || entities->subtype != 10 || entities->list.size() > 65536)
            throw ProtocolError("Invalid saved dropped entities");
        for (const auto &value : entities->list) {
            drop_entities::State entity;
            entity.id = static_cast<int>(integer(value, "NativeId"));
            if (entity.id < 1 || entity.id > 2000000000 || loaded.droppedEntities_.count(entity.id))
                throw ProtocolError("Invalid/duplicate dropped entity ID");
            const auto &type = child(value, "id", 8);
            if (type.string != "Item" && type.string != "XPOrb")
                throw ProtocolError("Unknown saved dropped entity type");
            entity.orb = type.string == "XPOrb";
            entity.age = static_cast<int>(integer(value, "Age"));
            entity.pickupDelay = static_cast<int>(integer(value, "PickupDelay"));
            entity.health = static_cast<int>(integer(value, "Health", 5));
            entity.fire = static_cast<int>(integer(value, "Fire"));
            entity.ticks = static_cast<int>(integer(value, "Ticks"));
            entity.value = static_cast<int>(integer(value, "Value"));
            entity.color = static_cast<int>(integer(value, "Color"));
            entity.targetColor = static_cast<int>(integer(value, "TargetColor"));
            entity.hasTarget = integer(value, "HasTarget") != 0;
            entity.onGround = integer(value, "OnGround") != 0;
            entity.noClip = integer(value, "NoClip") != 0;
            entity.inWeb = integer(value, "InWeb") != 0;
            entity.inWater = integer(value, "InWater") != 0;
            entity.firstUpdate = integer(value, "FirstUpdate", 1) != 0;
            entity.outsideBorder = integer(value, "OutsideBorder") != 0;
            entity.width = static_cast<float>(number(value, "Width", .25));
            entity.height = static_cast<float>(number(value, "Height", .25));
            entity.yaw = static_cast<float>(number(value, "Yaw"));
            entity.hoverStart = static_cast<float>(number(value, "HoverStart"));
            entity.fallDistance = static_cast<float>(number(value, "FallDistance"));
            entity.random.setSeed(integer(value, "RandomState") ^ 0x5deece66dLL);
            finite(entity.yaw, "entity yaw");
            finite(entity.hoverStart, "entity hover");
            finite(entity.fallDistance, "entity fall distance");
            if ((entity.width != .25f && entity.width != .5f) || (entity.height != .25f && entity.height != .5f) ||
                entity.health < 1 || entity.health > 255 || entity.age < -32768 || entity.age >= 6000 ||
                entity.pickupDelay < 0 || entity.pickupDelay > 32767 || entity.ticks < 0 ||
                (entity.orb && (entity.value < 1 || entity.value > 32767)))
                throw ProtocolError("Invalid saved dropped entity state");
            const auto &pos = child(value, "Pos", 9);
            const auto &motion = child(value, "Motion", 9);
            if (pos.subtype != 6 || pos.list.size() != 3 || motion.subtype != 6 || motion.list.size() != 3)
                throw ProtocolError("Invalid dropped entity vectors");
            entity.setPosition(pos.list[0].number, pos.list[1].number, pos.list[2].number);
            entity.previousX = entity.x;
            entity.previousY = entity.y;
            entity.previousZ = entity.z;
            entity.motionX = motion.list[0].number;
            entity.motionY = motion.list[1].number;
            entity.motionZ = motion.list[2].number;
            for (double v : {entity.motionX, entity.motionY, entity.motionZ}) {
                finite(v, "entity motion");
                if (std::abs(v) > 512)
                    throw ProtocolError("Saved entity motion exceeds movement bounds");
            }
            if (const auto *bounds = value.get("NativeBounds")) {
                if (bounds->type != 9 || bounds->subtype != 6 || bounds->list.size() != 6)
                    throw ProtocolError("Invalid saved entity bounds");
                double b[6];
                for (int i = 0; i < 6; ++i) {
                    b[i] = bounds->list[static_cast<std::size_t>(i)].number;
                    finite(b[i], "entity bounds");
                }
                if (std::abs(b[0] - (entity.x - entity.width / 2.0)) > 1e-7 || std::abs(b[1] - entity.y) > 1e-7 ||
                    std::abs(b[2] - (entity.z - entity.width / 2.0)) > 1e-7 ||
                    std::abs(b[3] - (entity.x + entity.width / 2.0)) > 1e-7 ||
                    std::abs(b[4] - (entity.y + entity.height)) > 1e-7 ||
                    std::abs(b[5] - (entity.z + entity.width / 2.0)) > 1e-7)
                    throw ProtocolError("Saved entity bounds disagree with position");
                entity.bounds = {b[0], b[1], b[2], b[3], b[4], b[5]};
            }
            if (const auto *owner = value.get("Owner")) {
                if (owner->type != 8 || owner->string.size() > 64)
                    throw ProtocolError("Invalid dropped item owner");
                entity.owner = owner->string;
            }
            if (const auto *thrower = value.get("Thrower")) {
                if (thrower->type != 8 || thrower->string.size() > 64)
                    throw ProtocolError("Invalid dropped item thrower");
                entity.thrower = thrower->string;
            }
            if (!entity.orb)
                entity.item = readItem(child(value, "Item", 10));
            loaded.droppedEntities_.emplace(entity.id, std::move(entity));
        }
    }
    for (const auto &entity : droppedEntities_)
        game.entities.erase(entity.first);
    loaded.running_ = true;
    *this = std::move(loaded);
    game.player = std::move(player);
    game.inventory = std::move(inventory);
    game.health = health;
    game.maxHealth = maximum;
    game.food = foodLevel;
    game.saturation = saturation;
    game.selectedSlot = selected;
    game.dimension = static_cast<int>(integer(saved, "Dimension"));
    game.experienceLevel = static_cast<int>(integer(saved, "XpLevel"));
    game.totalExperience = static_cast<int>(integer(saved, "XpTotal"));
    game.experienceProgress = static_cast<float>(number(saved, "XpP"));
    game.ticks = ticks_;
    game.joined = game.spawned = true;
    game.window = ContainerWindow{};
    ++game.windowRevision;
    game.status = "Native singleplayer simulation";
    syncGame(game);
    publishDroppedEntities(game);
}
bool Simulation::saveStateFile(const std::string &path, const Game &game, std::string *error) const {
    fs::path temporary;
    bool created = false;
    try {
        const auto plain = nbt::encode(saveState(game));
        std::vector<std::uint8_t> compressed(compressBound(static_cast<uLong>(plain.size())) + 32);
        z_stream stream{};
        if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
            throw ProtocolError("Could not initialize save compression");
        stream.next_in = const_cast<Bytef *>(plain.data());
        stream.avail_in = static_cast<uInt>(plain.size());
        stream.next_out = compressed.data();
        stream.avail_out = static_cast<uInt>(compressed.size());
        const int status = deflate(&stream, Z_FINISH);
        const auto size = stream.total_out;
        deflateEnd(&stream);
        if (status != Z_STREAM_END)
            throw ProtocolError("Simulation save compression failed");
        compressed.resize(size);
        const auto destination = fs::u8path(path);
        temporary = destination;
        temporary += L".tmp";
        if (fs::exists(temporary))
            throw ProtocolError("Simulation temporary save already exists");
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output)
                throw ProtocolError("Could not open simulation save");
            created = true;
            output.write(reinterpret_cast<const char *>(compressed.data()),
                         static_cast<std::streamsize>(compressed.size()));
            output.flush();
            if (!output)
                throw ProtocolError("Could not write simulation save");
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw ProtocolError("Could not install simulation save");
#else
        fs::rename(temporary, destination);
#endif
        return true;
    } catch (const std::exception &exception) {
        if (created) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
        }
        if (error)
            *error = exception.what();
        return false;
    }
}
bool Simulation::loadStateFile(const std::string &path, Game &game, std::string *error) {
    try {
        const auto source = fs::u8path(path);
        const auto size = fs::file_size(source);
        if (size == 0 || size > MaxStateBytes + 65536)
            throw ProtocolError("Simulation save file size limit exceeded");
        std::vector<std::uint8_t> compressed(static_cast<std::size_t>(size));
        {
            std::ifstream input(source, std::ios::binary);
            input.read(reinterpret_cast<char *>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
            if (!input)
                throw ProtocolError("Could not read simulation save");
        }
        std::vector<std::uint8_t> plain(MaxStateBytes);
        z_stream stream{};
        if (inflateInit2(&stream, MAX_WBITS + 16) != Z_OK)
            throw ProtocolError("Could not initialize save decompression");
        stream.next_in = compressed.data();
        stream.avail_in = static_cast<uInt>(compressed.size());
        stream.next_out = plain.data();
        stream.avail_out = static_cast<uInt>(plain.size());
        const int status = inflate(&stream, Z_FINISH);
        const auto length = stream.total_out;
        const bool valid = status == Z_STREAM_END && stream.avail_in == 0;
        inflateEnd(&stream);
        if (!valid)
            throw ProtocolError("Invalid/oversize compressed simulation save");
        plain.resize(length);
        loadState(game, nbt::parse(plain));
        return true;
    } catch (const std::exception &exception) {
        if (error)
            *error = exception.what();
        return false;
    }
}

} // namespace mc
