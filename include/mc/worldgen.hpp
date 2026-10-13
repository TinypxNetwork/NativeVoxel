#pragma once

#include "mc/world.hpp"
#include "mc/nbt.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace mc::worldgen {

class JavaRandom;

enum class Dimension : int { Nether = -1, Overworld = 0, End = 1 };
enum class WorldType { Default, LargeBiomes, Amplified };
// Each stage includes the preceding stages. Population is a separate operation
// because vanilla population can modify neighboring columns.
enum class ChunkStage { Terrain, Surface, Carved, Structures };

struct Settings {
    Dimension dimension{Dimension::Overworld};
    WorldType worldType{WorldType::Default};
    ChunkStage stage{ChunkStage::Carved};
    bool mapFeatures{true};
};

struct SpawnCandidate {
    int x{}, y{64}, z{};
    bool found{};
    bool suitableGround{};
    int attempts{}; // Vanilla still accepts its final coordinate after 1,000 moves.
};

// Generation requests creation of real entities. These are constructor/placement
// records, not incomplete snapshots masquerading as a simulated entity's NBT.
struct EntitySpawn {
    std::string id;
    double x{}, y{}, z{};
    float yaw{}, pitch{};
};
struct BlockEntityUpdate {
    int x{},y{},z{};
    nbt::Tag data; // Complete tile NBT; TAG_End removes an older tile at this position.
};
struct PopulationOutput {
    std::vector<EntitySpawn> entities;
    std::vector<BlockEntityUpdate> blockEntities; // Apply in order, including removals.
};

class Generator {
public:
    explicit Generator(std::int64_t seed, Settings settings = {});
    ~Generator();
    Generator(Generator&&) noexcept;
    Generator& operator=(Generator&&) noexcept;
    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;

    std::int64_t seed() const noexcept;
    const Settings& settings() const noexcept;
    // Exact vanilla generation for the explicitly selected stage. Unsupported
    // stages/settings return false with an error; no preview fallback is used.
    // Successful output replaces this column and preserves unrelated columns.
    bool generateChunk(World& destination, int chunkX, int chunkZ, std::string* error = nullptr);
    // Caller supplies neighboring generated chunks in vanilla population order.
    bool populate(World& world, int chunkX, int chunkZ, std::string* error = nullptr);
    // End decoration uses the running World's Random, not a seed derived from
    // chunk coordinates. Effects append to output and must be instantiated by
    // the owning server. Currently supports complete 1.8.9 End decoration.
    bool populate(World& world, int chunkX, int chunkZ, JavaRandom& worldRandom,
                  PopulationOutput& output, std::string* error = nullptr);
    int biomeAt(int blockX, int blockZ);
    // Follows WorldServer.createSpawnPosition against chunks at settings.stage.
    // Does not populate neighbors; completed server-world spawning must instead
    // supply its provider's canCoordinateBeSpawn callback using the overload.
    // Y is the world's spawn field, not a player's resolved feet elevation.
    SpawnCandidate findSpawn();
    SpawnCandidate findSpawn(const std::function<bool(int,int)>& canCoordinateBeSpawn);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mc::worldgen
