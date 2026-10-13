#pragma once

#include "mc/worldgen.hpp"
#include "mc/worldgen_biome_data.hpp"
#include <memory>
#include <vector>

namespace mc::worldgen {
class JavaRandom;
struct BiomePosition { int x{}, z{}; bool found{}; };
// The original GenLayer river/biome map and final Voronoi map. The coarse map
// uses one cell per four blocks and is consumed directly by terrain blending.
class BiomeLayers {
public:
    explicit BiomeLayers(std::int64_t seed, WorldType type=WorldType::Default);
    ~BiomeLayers();
    BiomeLayers(BiomeLayers&&) noexcept;
    BiomeLayers& operator=(BiomeLayers&&) noexcept;
    BiomeLayers(const BiomeLayers&)=delete;
    BiomeLayers& operator=(const BiomeLayers&)=delete;
    std::vector<int> coarse(int x,int z,int width,int height);
    std::vector<int> blocks(int x,int z,int width,int height);
    // WorldChunkManager's coarse-map selection, including its original
    // replacement-count sampling behavior and exact Random consumption.
    BiomePosition findBiomePosition(int x,int z,int range,const std::vector<int>& allowed,JavaRandom& random);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
