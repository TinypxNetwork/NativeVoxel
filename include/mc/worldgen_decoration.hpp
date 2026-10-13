#pragma once
#include "mc/worldgen.hpp"
#include "mc/worldgen_noise.hpp"

namespace mc::worldgen {
struct DecorationHooks {
    bool noSky{};
    bool springs{true};
    // Exact World.getLight value used by mushroom placement.
    std::function<int(int,int,int)> light;
    // A spring invokes this immediately, with the same generator RNG. The
    // callback must execute the owning server's forceBlockUpdateTick operation.
    // Required when springs=true; absent callbacks cause an error before edits.
    std::function<void(int,int,int,BlockState,JavaRandom&)> forceBlockTick;
};
// Original BiomeGenBase.decorate and all registered overrides, with default ore
// settings. This is the biome-decoration portion of population, not lakes,
// dungeons, structures, mobs, or snow/ice finishing in ChunkProviderGenerate.
// The target and three positive-axis neighboring columns must already exist.
// Counter/tree/mushroom state follows original process-wide biome singletons.
bool decorateBiome(World& world,int biome,int chunkX,int chunkZ,JavaRandom& random,
                   PopulationOutput& output,const DecorationHooks& hooks={},std::string* error=nullptr);
// The feature portion of ChunkProviderHell.populate, after fortress placement.
// Uses the provider's existing RNG stream; vanilla does not reseed it here.
// forceBlockTick is mandatory; hooks.springs does not disable Nether lava.
// In addition to the four population columns, west and north neighbors must be
// loaded: enclosed springs at local X/Z zero read across those borders.
bool populateNetherFeatures(World& world,int chunkX,int chunkZ,JavaRandom& providerRandom,
                            const DecorationHooks& hooks,std::string* error=nullptr);
}
