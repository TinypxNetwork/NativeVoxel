#pragma once

#include "mc/world.hpp"
#include "mc/worldgen.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace mc::worldio {

struct SaveMetadata;
struct SavedEntity {
    int chunkX{}, chunkZ{};
    nbt::Tag data;
};
struct GenerationInfo {
    worldgen::WorldType type{worldgen::WorldType::Default};
    bool mapFeatures{true};
    bool terrainPopulated{}; // Leave false for an incomplete generation stage.
    int gameType{}; // Survival/Creative/Adventure/Spectator = 0/1/2/3.
    bool allowCommands{};
};

struct SaveInfo {
    std::string levelName{"NativeVoxel preview"};
    std::int64_t seed{189};
    int dimension{};
    int gameType{1};
    int spawnX{}, spawnY{64}, spawnZ{};
    bool hasPlayer{};
    double x{.5}, y{64}, z{.5};
    float yaw{}, pitch{};
    std::size_t loadedChunks{};
    // Original level/chunk NBT retained for export of imported terrain. Unknown
    // tags, block entities and entity data are preserved without being simulated.
    std::shared_ptr<SaveMetadata> metadata;
    // Explicit metadata for a newly generated vanilla world. When absent, new
    // exports retain the preview defaults; imported level metadata is retained.
    std::optional<GenerationInfo> generation;
    // Full typed tile updates from generation/runtime. Applies in order to the
    // retained chunk NBT; TAG_End removes a tile. Only loaded columns export.
    std::vector<worldgen::BlockEntityUpdate> blockEntityUpdates;
    // Supported drops from loaded Anvil columns, including their source column.
    // Other entity types remain untouched in the retained chunk documents.
    std::vector<SavedEntity> droppedEntities;
    // An engaged snapshot replaces Item/XPOrb entities in every exported column;
    // an empty snapshot therefore persists collection/despawn without resurrection.
    std::optional<std::vector<nbt::Tag>> droppedEntityUpdates;
    std::optional<nbt::Tag> playerData;
    std::optional<nbt::Tag> levelUpdates;
};

// Reads level.dat and .mca columns within a square radius around the saved
// player (or world spawn). Supports 1.8.9 Anvil sections in overworld, Nether and
// End. On failure, neither destination nor info changes. Input files are read-only.
bool loadWorld(const std::string& directory, World& destination, SaveInfo& info,
               int radiusChunks = 8, std::string* error = nullptr);
// Loads one existing column from the same source save before generation is
// considered. Missing is successful with found=false; malformed data is an error.
bool loadChunk(World& destination,SaveInfo& info,int chunkX,int chunkZ,bool& found,std::string* error=nullptr);

// Exports only the currently loaded columns to a NEW save directory. Refuses an
// existing destination, including an empty directory. No existing save is modified.
// This is terrain persistence, not an integrated server or world simulation.
bool saveNewWorld(const std::string& directory, const World& world, const Player& player,
                  const SaveInfo& info = SaveInfo{}, std::string* error = nullptr);

} // namespace mc::worldio
