#pragma once
#include "mc/worldgen.hpp"
#include "mc/worldgen_noise.hpp"
#include <array>
#include <memory>

namespace mc::worldgen::detail {
struct Primer {
    // Original ChunkPrimer layout, converted only at the World boundary.
    std::array<BlockState,65536> blocks{};
    BlockState get(int x,int y,int z) const {return blocks[(x<<12)|(z<<8)|y];}
    void set(int x,int y,int z,BlockState state){blocks[(x<<12)|(z<<8)|y]=state;}
};
class SurfaceGenerator {
public:
    explicit SurfaceGenerator(std::int64_t seed);
    ~SurfaceGenerator();
    void column(Primer& primer,JavaRandom& random,int biome,int x,int z,double noise);
    float temperature(int biome,int x,int y,int z) const;
    BlockState currentTop(int biome) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
void carveCaves(Primer& primer,std::int64_t seed,int chunkX,int chunkZ,const std::vector<int>& biomes,const SurfaceGenerator& surface);
void carveRavines(Primer& primer,std::int64_t seed,int chunkX,int chunkZ,const std::vector<int>& biomes,const SurfaceGenerator& surface);
void carveNetherCaves(Primer& primer,std::int64_t seed,int chunkX,int chunkZ,const std::vector<int>& biomes,const SurfaceGenerator& surface);
class DimensionGenerator {
public:
    DimensionGenerator(std::int64_t seed,Dimension dimension);
    ~DimensionGenerator();
    void generate(Primer& primer,int chunkX,int chunkZ,ChunkStage stage);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
