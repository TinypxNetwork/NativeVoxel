#include "mc/local_session.hpp"
#include "mc/registry.hpp"
#include "mc/item_nbt.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <stdexcept>
namespace mc {
namespace {
nbt::Tag integerTag(int type, std::int64_t value) {
    nbt::Tag tag;
    tag.type = type;
    tag.integer = value;
    return tag;
}
nbt::Tag floatTag(float value) {
    nbt::Tag tag;
    tag.type = 5;
    tag.number = value;
    return tag;
}
std::int64_t integer(const nbt::Tag &tag, const char *key, std::int64_t fallback = 0) {
    auto *v = tag.get(key);
    return v ? v->integer : fallback;
}
float real(const nbt::Tag &tag, const char *key, float fallback = 0) {
    auto *v = tag.get(key);
    return v ? float(v->type == 5 || v->type == 6 ? v->number : v->integer) : fallback;
}
ItemStack savedItem(const nbt::Tag &tag) { return item_nbt::read(tag); }
nbt::Tag savedItem(const ItemStack &item, int slot) { return item_nbt::write(item, slot); }
void restorePlayer(Game &game, const worldio::SaveInfo &info) {
    if (!info.playerData)
        return;
    const auto &data = *info.playerData;
    game.health = real(data, "Health", 20);
    game.food = int(integer(data, "foodLevel", 20));
    game.saturation = real(data, "foodSaturationLevel", 5);
    game.experienceLevel = int(integer(data, "XpLevel"));
    game.totalExperience = int(integer(data, "XpTotal"));
    game.experienceProgress = real(data, "XpP");
    game.selectedSlot = std::clamp(int(integer(data, "SelectedItemSlot")), 0, 8);
    game.player.onGround = integer(data, "OnGround") != 0;
    game.player.fallDistance = real(data, "FallDistance");
    if (const auto *motion = data.get("Motion");
        motion && motion->type == 9 && motion->subtype == 6 && motion->list.size() == 3) {
        game.player.motionX = motion->list[0].number;
        game.player.motionY = motion->list[1].number;
        game.player.motionZ = motion->list[2].number;
    }
    if (const auto *abilities = data.get("abilities")) {
        game.player.flying = integer(*abilities, "flying") != 0;
        game.player.flySpeed = real(*abilities, "flySpeed", .05f);
    }
    for (double value :
         {double(game.health), double(game.saturation), double(game.experienceProgress), game.player.motionX,
          game.player.motionY, game.player.motionZ, double(game.player.flySpeed), double(game.player.fallDistance)})
        if (!std::isfinite(value))
            throw std::runtime_error("Non-finite player data in level.dat");
    if (auto *items = data.get("Inventory"))
        for (const auto &value : items->list) {
            int slot = int(integer(value, "Slot", -1));
            int native = slot < 9 && slot >= 0        ? 36 + slot
                         : slot >= 9 && slot < 36     ? slot
                         : slot >= 100 && slot <= 103 ? 108 - slot
                                                      : -1;
            if (native >= 0)
                game.inventory[native] = savedItem(value);
        }
}
void publishTiles(Game &game, Simulation &simulation, const worldio::SaveInfo &info, std::size_t begin = 0) {
    for (std::size_t i = begin; i < info.blockEntityUpdates.size(); ++i) {
        const auto &update = info.blockEntityUpdates[i];
        std::uint64_t key = (std::uint64_t(std::uint32_t(update.x) & 0x3ffffff) << 38) |
                            (std::uint64_t(std::uint32_t(update.y) & 4095) << 26) |
                            (std::uint32_t(update.z) & 0x3ffffff);
        if (update.data.type == 0)
            game.tileEntities.erase(key);
        else {
            game.tileEntities[key] = {update.x, update.y, update.z, 0, nbt::encode(update.data)};
            simulation.importTileEntity(game, game.tileEntities[key]);
            if (const auto *id = update.data.get("id"); id && id->type == 8 && id->string == "Sign") {
                SignText sign;
                sign.x = update.x;
                sign.y = update.y;
                sign.z = update.z;
                for (int line = 0; line < 4; ++line) {
                    if (const auto *text = update.data.get("Text" + std::to_string(line + 1));
                        text && text->type == 8) {
                        sign.jsonLines[line] = text->string;
                        sign.lines[line] = Game::plainChat(text->string);
                    }
                }
                game.signs[key] = std::move(sign);
            }
        }
    }
}
} // namespace
void LocalSession::detach(Game &game) {
    active_ = false;
    generator_.reset();
    absentSavedChunks_.clear();
    game.offlineBlockInteraction = {};
    game.offlineBlockBreak = {};
    game.offlineItemDrop = {};
    game.offlineItemDrops = {};
    game.offlineHeldItemDrop = {};
    game.offlineCreativeDrop = {};
    game.offlineProjectileUse = {};
    game.offlineProjectileExperience = {};
    game.beforeOfflineContainerClose = {};
}
void LocalSession::attach(Game &game) {
    if (game.online)
        throw std::logic_error("Cannot attach local simulation to a server connection");
    active_ = true;
    game.offlineBlockInteraction = [this, &game](int x, int y, int z) {
        return active_ && !game.online && simulation.interactBlock(game, x, y, z);
    };
    game.offlineBlockBreak = [this, &game](int x, int y, int z, int face) {
        return active_ && !game.online && simulation.breakBlock(game, x, y, z, face);
    };
    game.offlineItemDrop = [this, &game](const ItemStack &stack, bool trace) {
        return active_ && !game.online && simulation.dropFromPlayer(game, stack, trace) >= 0;
    };
    game.offlineItemDrops = [this, &game](const std::vector<ItemStack> &stacks, bool trace) {
        return active_ && !game.online && simulation.dropFromPlayerBatch(game, stacks, trace);
    };
    game.offlineHeldItemDrop = [this, &game](bool all) {
        return active_ && !game.online && simulation.dropHeldItem(game, all);
    };
    game.offlineCreativeDrop = [this, &game](const ItemStack &stack) {
        return active_ && !game.online && simulation.creativeDrop(game, stack);
    };
    game.offlineProjectileUse=[this,&game](const ItemStack& stack,float charge){return active_&&!game.online?simulation.useProjectile(game,stack,charge):-1;};
    game.offlineProjectileExperience=[this,&game](int value,double x,double y,double z){
        while(active_&&!game.online&&value>0){const int split=value>=7?7:value>=3?3:1;simulation.spawnExperience(game,split,x,y,z);value-=split;}
    };
    game.beforeOfflineContainerClose = [this, &game]() {
        if (active_ && !game.online)
            simulation.beforeContainerClose(game);
    };
}
void LocalSession::attachPreview(Game &game, int mode, const std::string &name) {
    detach(game);
    simulation.settings.gameMode = mode;
    simulation.settings.playerName = name;
    simulation.start(game, 189, true);
    saveInfo = {};
    saveInfo.levelName = "NativeVoxel test world";
    attach(game);
}
bool LocalSession::create(Game &game, std::int64_t seed, worldgen::Settings settings, int radius, int mode,
                          const std::string &name, std::string &error, const std::function<bool(int, int)> &progress) {
    try {
        if (mode < 0 || mode > 3)
            throw std::invalid_argument("Invalid game mode");
        if (settings.stage != worldgen::ChunkStage::Carved || settings.mapFeatures)
            throw std::invalid_argument("Local generation currently supports carved terrain without structures");
        radius = std::clamp(radius, 2, 12);
        auto generator = std::make_unique<worldgen::Generator>(seed, settings);
        const auto spawn = generator->findSpawn();
        World generated;
        int centerX = floorDiv16(spawn.x), centerZ = floorDiv16(spawn.z), total = (radius * 2 + 1) * (radius * 2 + 1),
            done = 0;
        for (int distance = 0; distance <= radius; ++distance)
            for (int z = centerZ - distance; z <= centerZ + distance; ++z)
                for (int x = centerX - distance; x <= centerX + distance; ++x) {
                    if (std::max(std::abs(x - centerX), std::abs(z - centerZ)) != distance)
                        continue;
                    if (!generator->generateChunk(generated, x, z, &error))
                        return false;
                    if (progress && !progress(++done, total)) {
                        error = "World creation cancelled";
                        return false;
                    }
                }
        Game candidate;
        candidate.world = std::move(generated);
        candidate.dimension = int(settings.dimension);
        candidate.spawnX = spawn.x;
        candidate.spawnY = spawn.y;
        candidate.spawnZ = spawn.z;
        candidate.player.teleport(spawn.x + .5, spawn.y, spawn.z + .5);
        while (candidate.player.y < 254 && !candidate.world.collisions(candidate.player.bounds()).empty())
            candidate.player.teleport(candidate.player.x, candidate.player.y + 1, candidate.player.z);
        Simulation ready;
        ready.settings.gameMode = mode;
        ready.settings.playerName = name;
        ready.start(candidate, std::uint64_t(seed), true);
        auto observer = game.incomingObserver;
        detach(game);
        game.disconnect();
        game = std::move(candidate);
        game.incomingObserver = std::move(observer);
        simulation = std::move(ready);
        saveInfo = {};
        saveInfo.levelName = "Native world";
        saveInfo.seed = seed;
        saveInfo.dimension = game.dimension;
        saveInfo.gameType = mode;
        saveInfo.spawnX = spawn.x;
        saveInfo.spawnY = spawn.y;
        saveInfo.spawnZ = spawn.z;
        worldio::GenerationInfo generation;
        generation.type = settings.worldType;
        generation.mapFeatures = settings.mapFeatures;
        generation.terrainPopulated = false;
        generation.gameType = mode;
        generation.allowCommands = false;
        saveInfo.generation = generation;
        generator_ = std::move(generator);
        attach(game);
        error.clear();
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}
bool LocalSession::load(Game &game, const std::string &directory, int radius, const std::string &name,
                        std::string &error) {
    try {
        World loaded;
        worldio::SaveInfo info;
        if (!worldio::loadWorld(directory, loaded, info, radius, &error))
            return false;
        Game candidate;
        candidate.world = std::move(loaded);
        candidate.dimension = info.dimension;
        candidate.spawnX = info.spawnX;
        candidate.spawnY = info.spawnY;
        candidate.spawnZ = info.spawnZ;
        candidate.player.teleport(info.x, info.y, info.z);
        candidate.player.yaw = info.yaw;
        candidate.player.pitch = info.pitch;
        auto border = candidate.world.border();
        candidate.border.centerX = (border.minX + border.maxX) * .5;
        candidate.border.centerZ = (border.minZ + border.maxZ) * .5;
        candidate.border.diameter = candidate.border.targetDiameter =
            std::min(border.maxX - border.minX, border.maxZ - border.minZ);
        Simulation ready;
        ready.settings.playerName = name;
        const auto state = std::filesystem::u8path(directory) / "native_simulation.dat";
        if (std::filesystem::exists(state)) {
            if (!ready.loadStateFile(state.u8string(), candidate, &error))
                return false;
        } else {
            restorePlayer(candidate, info);
            ready.settings.gameMode = info.gameType;
            if (info.levelUpdates)
                ready.settings.difficulty = int(integer(*info.levelUpdates, "Difficulty", 2));
            ready.start(candidate, std::uint64_t(info.seed), false);
            if (info.playerData) {
                ready.food.exhaustion = real(*info.playerData, "foodExhaustionLevel");
                ready.food.timer = int(integer(*info.playerData, "foodTickTimer"));
                item_nbt::readSlots(*info.playerData, ready.enderItems, "EnderItems");
            }
            if (info.levelUpdates) {
                const auto &data = *info.levelUpdates;
                ready.weather.totalTime = integer(data, "Time");
                ready.weather.dayTime = integer(data, "DayTime", ready.weather.totalTime);
                ready.weather.raining = integer(data, "raining") != 0;
                ready.weather.thundering = integer(data, "thundering") != 0;
                ready.weather.rainTime = int(integer(data, "rainTime"));
                ready.weather.thunderTime = int(integer(data, "thunderTime"));
                ready.weather.cleanWeatherTime = int(integer(data, "clearWeatherTime"));
                ready.weather.previousRainStrength = ready.weather.rainStrength = ready.weather.raining ? 1.f : 0.f;
                ready.weather.previousThunderStrength = ready.weather.thunderStrength =
                    ready.weather.thundering ? 1.f : 0.f;
                if (const auto *rules = data.get("GameRules")) {
                    auto rule = [&](const char *key, bool fallback) {
                        const auto *value = rules->get(key);
                        return value && value->type == 8 ? value->string == "true" : fallback;
                    };
                    ready.settings.naturalRegeneration = rule("naturalRegeneration", true);
                    ready.settings.daylightCycle = rule("doDaylightCycle", true);
                    ready.settings.tileDrops = rule("doTileDrops", true);
                }
            }
        }
        if (candidate.dimension != info.dimension)
            throw std::runtime_error("Simulation sidecar dimension does not match level.dat");
        std::unique_ptr<worldgen::Generator> generator;
        if (info.generation) {
            worldgen::Settings settings;
            settings.worldType = info.generation->type;
            settings.dimension = worldgen::Dimension(info.dimension);
            settings.mapFeatures = info.generation->mapFeatures;
            generator = std::make_unique<worldgen::Generator>(info.seed, settings);
        }
        publishTiles(candidate, ready, info);
        auto observer = game.incomingObserver;
        detach(game);
        game.disconnect();
        game = std::move(candidate);
        game.incomingObserver = std::move(observer);
        simulation = std::move(ready);
        saveInfo = std::move(info);
        game.worldAge = simulation.weather.totalTime;
        game.worldTime = simulation.weather.dayTime;
        game.raining = simulation.weather.raining;
        game.rainStrength = simulation.weather.rainStrength;
        game.thunderStrength = simulation.weather.thunderStrength;
        generator_ = std::move(generator);
        attach(game);
        error.clear();
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}
bool LocalSession::saveNew(Game &game, const std::string &directory, std::string &error) {
    if (!active_ || game.online) {
        error = "No active local world to save";
        return false;
    }
    namespace fs = std::filesystem;
    fs::path staging, parent;
    bool created = false;
    try {
        const auto destination = fs::weakly_canonical(fs::absolute(fs::u8path(directory)));
        if (destination.filename().empty() || fs::exists(fs::symlink_status(destination)))
            throw std::runtime_error("Save destination must be a new directory");
        parent = destination.parent_path();
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        staging = parent / (".native-save-" + std::to_string(stamp));
        // Closing the UI commits storage and drops cursor/crafting inputs, which
        // are then persisted as entities. No held stack can vanish on exit.
        game.closeContainer();
        simulation.commitContainer(game);
        simulation.publishTileEntities(game);
        auto info = saveInfo;
        info.dimension = game.dimension;
        info.gameType = game.gameMode;
        if (info.generation)
            info.generation->gameType = game.gameMode;
        auto player = info.playerData.value_or(nbt::Tag{});
        player.type = 10;
        player.compound["Health"] = floatTag(game.health);
        player.compound["foodLevel"] = integerTag(3, game.food);
        player.compound["foodSaturationLevel"] = floatTag(game.saturation);
        player.compound["foodExhaustionLevel"] = floatTag(simulation.food.exhaustion);
        player.compound["foodTickTimer"] = integerTag(3, simulation.food.timer);
        player.compound["XpLevel"] = integerTag(3, game.experienceLevel);
        player.compound["XpTotal"] = integerTag(3, game.totalExperience);
        player.compound["XpP"] = floatTag(game.experienceProgress);
        player.compound["SelectedItemSlot"] = integerTag(3, game.selectedSlot);
        nbt::Tag inventory;
        inventory.type = 9;
        inventory.subtype = 10;
        for (int i = 5; i < 45; ++i)
            if (!game.inventory[i].empty())
                inventory.list.push_back(savedItem(game.inventory[i], i >= 36 ? i - 36 : i >= 9 ? i : 108 - i));
        player.compound["Inventory"] = std::move(inventory);
        player.compound["EnderItems"] = item_nbt::writeSlots(simulation.enderItems);
        info.playerData = std::move(player);
        nbt::Tag level;
        level.type = 10;
        level.compound["Time"] = integerTag(4, game.worldAge);
        level.compound["DayTime"] = integerTag(4, game.worldTime);
        level.compound["GameType"] = integerTag(3, game.gameMode);
        level.compound["raining"] = integerTag(1, simulation.weather.raining);
        level.compound["thundering"] = integerTag(1, simulation.weather.thundering);
        level.compound["rainTime"] = integerTag(3, simulation.weather.rainTime);
        level.compound["thunderTime"] = integerTag(3, simulation.weather.thunderTime);
        level.compound["clearWeatherTime"] = integerTag(3, simulation.weather.cleanWeatherTime);
        level.compound["Difficulty"] = integerTag(1, simulation.settings.difficulty);
        nbt::Tag rules;
        if (info.levelUpdates)
            if (const auto *oldRules = info.levelUpdates->get("GameRules"))
                rules = *oldRules;
        rules.type = 10;
        rules.compound["naturalRegeneration"] = nbt::text(simulation.settings.naturalRegeneration ? "true" : "false");
        rules.compound["doDaylightCycle"] = nbt::text(simulation.settings.daylightCycle ? "true" : "false");
        rules.compound["doTileDrops"] = nbt::text(simulation.settings.tileDrops ? "true" : "false");
        level.compound["GameRules"] = std::move(rules);
        info.levelUpdates = std::move(level);
        std::map<std::tuple<int, int, int>, worldgen::BlockEntityUpdate> updates;
        for (const auto &tile : info.blockEntityUpdates)
            updates[{tile.x, tile.y, tile.z}] = {tile.x, tile.y, tile.z, {}};
        for (const auto &entry : game.tileEntities) {
            const auto &tile = entry.second;
            updates[{tile.x, tile.y, tile.z}] = {tile.x, tile.y, tile.z, nbt::parse(tile.nbt)};
        }
        info.blockEntityUpdates.clear();
        for (auto &entry : updates)
            info.blockEntityUpdates.push_back(std::move(entry.second));
        if (!worldio::saveNewWorld(staging.u8string(), game.world, game.player, info, &error))
            return false;
        created = true;
        if (!simulation.saveStateFile((staging / "native_simulation.dat").u8string(), game, &error))
            throw std::runtime_error(error);
        fs::rename(staging, destination);
        created = false;
        saveInfo = std::move(info);
        error.clear();
        return true;
    } catch (const std::exception &e) {
        if (created) {
            std::error_code ignored;
            const auto resolved = fs::weakly_canonical(staging, ignored);
            if (!ignored && resolved.parent_path() == parent &&
                resolved.filename().u8string().find(".native-save-") == 0)
                fs::remove_all(resolved, ignored);
        }
        error = e.what();
        return false;
    }
}
void LocalSession::stream(Game &game, int radius) {
    if ((!generator_ && !saveInfo.metadata) || game.world.chunks().size() >= 8192)
        return;
    radius = std::clamp(radius, 2, 32);
    int cx = floorDiv16(floorBlock(game.player.x)), cz = floorDiv16(floorBlock(game.player.z)), made = 0;
    std::string error;
    for (int distance = 0; distance <= radius && made < 2; ++distance)
        for (int z = cz - distance; z <= cz + distance && made < 2; ++z)
            for (int x = cx - distance; x <= cx + distance && made < 2; ++x) {
                if (std::max(std::abs(x - cx), std::abs(z - cz)) != distance || game.world.hasChunk(x, z))
                    continue;
                const auto chunkKey = (std::uint64_t(std::uint32_t(x)) << 32) | std::uint32_t(z);
                if (!generator_ && absentSavedChunks_.count(chunkKey))
                    continue;
                bool found = false;
                auto oldTiles = saveInfo.blockEntityUpdates.size();
                if (!worldio::loadChunk(game.world, saveInfo, x, z, found, &error)) {
                    simulation.lastInteractionError = error;
                    return;
                }
                if (found) {
                    publishTiles(game, simulation, saveInfo, oldTiles);
                    ++made;
                    continue;
                }
                if (!generator_) {
                    if (absentSavedChunks_.size() < 65536)
                        absentSavedChunks_.insert(chunkKey);
                    continue;
                }
                if (!generator_->generateChunk(game.world, x, z, &error)) {
                    simulation.lastInteractionError = error;
                    return;
                }
                ++made;
            }
}
void LocalSession::tick(Game &game, const Input &input, int renderDistance) {
    if (!active_ || game.online)
        throw std::logic_error("Local session is not active");
    stream(game, renderDistance);
    simulation.tick(game, input);
}
} // namespace mc
