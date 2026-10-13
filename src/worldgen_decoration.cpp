#include "mc/worldgen_decoration.hpp"
#include "mc/worldgen_decoration_data.hpp"
#include "mc/registry.hpp"
#include "worldgen_features.hpp"
#include <algorithm>
#include <array>
#include <mutex>
#include <stdexcept>

namespace mc::worldgen {
namespace {
using detail::Position;
using detail::ScatterKind;
using detail::TreeKind;
struct BiomeState {
    DecorationDescription description;
    detail::BigTreeState bigTree;
    unsigned mushroom{};
    bool active{};
};
struct SharedDecoration {
    std::recursive_mutex mutex;
    JavaRandom noiseRandom{2345};
    NoisePerlin grassNoise{noiseRandom, 1};
    std::array<BiomeState, 256> biomes{};
    SharedDecoration() {
        for (int i = 0; i < 256; ++i)
            biomes[i].description = decorationDescriptions[i];
    }
};
SharedDecoration &shared() {
    static SharedDecoration state;
    return state;
}
unsigned id(const World &world, Position p) { return blockId(world.getBlock(p.x, p.y, p.z)); }
void set(World &world, Position p, BlockState block) { world.setBlock(p.x, p.y, p.z, block); }
int height(const World &world, int x, int z) {
    if (x < -30000000 || x >= 30000000 || z < -30000000 || z >= 30000000)
        return 64;
    const auto *chunk = world.chunk(floorDiv16(x), floorDiv16(z));
    return chunk ? chunk->heightMap[(z & 15) * 16 + (x & 15)] : 0;
}
Position above(const World &world, Position p) { return {p.x, height(world, p.x, p.z), p.z}; }
Position solidTop(const World &world, Position p) { return {p.x, detail::topSolidOrLiquid(world, p.x, p.z), p.z}; }
struct TreeChoice {
    TreeKind kind{TreeKind::Oak};
    detail::TreeOptions options;
    bool big{};
    int owner{};
};
TreeChoice chooseTree(SharedDecoration &state, int biome, JavaRandom &random) {
    const auto &d = state.biomes[biome].description;
    TreeChoice result;
    result.owner = biome;
    switch (d.trees) {
    case BiomeRule::BiomeGenMutated:
        return chooseTree(state, d.delegate, random);
    case BiomeRule::BiomeGenForest_2:
        result.kind = random.nextBoolean() ? TreeKind::TallBirch : TreeKind::Birch;
        break;
    case BiomeRule::BiomeGenForest:
        if (d.variant == 3 && random.nextInt(3) > 0)
            result.kind = TreeKind::DarkOak;
        else
            result.kind = d.variant != 2 && random.nextInt(5) != 0 ? TreeKind::Oak : TreeKind::Birch;
        break;
    case BiomeRule::BiomeGenJungle:
        if (random.nextInt(10) == 0)
            result.big = true;
        else if (random.nextInt(2) == 0) {
            result.kind = TreeKind::Shrub;
            result.options.log = blockState(17, 3);
        } else if (!d.jungleEdge && random.nextInt(3) == 0)
            result.kind = TreeKind::MegaJungle;
        else {
            result.options.minimumHeight = 4 + random.nextInt(7);
            result.options.log = blockState(17, 3);
            result.options.leaves = blockState(18, 3);
            result.options.vines = true;
        }
        break;
    case BiomeRule::BiomeGenTaiga:
        if ((d.variant == 1 || d.variant == 2) && random.nextInt(3) == 0)
            result.kind = d.variant != 2 && random.nextInt(13) != 0 ? TreeKind::MegaPine : TreeKind::MegaSpruce;
        else
            result.kind = random.nextInt(3) == 0 ? TreeKind::Pine : TreeKind::Spruce;
        break;
    case BiomeRule::BiomeGenHills:
        if (random.nextInt(3) > 0)
            result.kind = TreeKind::Spruce;
        else
            result.big = random.nextInt(10) == 0;
        break;
    case BiomeRule::BiomeGenSavanna:
        result.kind = random.nextInt(5) > 0 ? TreeKind::Acacia : TreeKind::Oak;
        break;
    case BiomeRule::BiomeGenSnow:
        result.kind = TreeKind::Spruce;
        break;
    case BiomeRule::BiomeGenSwamp:
        result.kind = TreeKind::Swamp;
        break;
    case BiomeRule::BiomeGenMesa:
        break;
    default:
        result.big = random.nextInt(10) == 0;
        break;
    }
    return result;
}
void tree(World &world, SharedDecoration &state, JavaRandom &random, Position p, const TreeChoice &choice) {
    if (choice.big)
        detail::generateBigTree(world, random, p, state.biomes[choice.owner].bigTree, true);
    else if (detail::generateTree(world, random, p, choice.kind, choice.options))
        detail::decorateTreeBase(world, random, p, choice.kind);
}
BlockState flower(SharedDecoration &state, int biome, JavaRandom &random, Position p) {
    const auto &d = state.biomes[biome].description;
    if (d.flowers == BiomeRule::BiomeGenSwamp)
        return blockState(38, 1);
    if (d.flowers == BiomeRule::BiomeGenForest && d.variant == 1) {
        const double value =
            std::clamp((1.0 + state.grassNoise.sample(double(p.x) / 48.0, double(p.z) / 48.0)) / 2.0, 0.0, .9999);
        int choice = int(value * 10.0);
        if (choice == 2)
            choice = 1;
        return choice == 0 ? blockState(37) : blockState(38, choice - 1);
    }
    if (d.flowers == BiomeRule::BiomeGenPlains) {
        if (state.grassNoise.sample(double(p.x) / 200.0, double(p.z) / 200.0) < -.8) {
            constexpr unsigned tulip[] = {5, 4, 7, 6};
            return blockState(38, tulip[random.nextInt(4)]);
        }
        if (random.nextInt(3) > 0) {
            constexpr unsigned types[] = {0, 3, 8};
            return blockState(38, types[random.nextInt(3)]);
        }
        return blockState(37);
    }
    return random.nextInt(3) > 0 ? blockState(37) : blockState(38);
}
BlockState grass(SharedDecoration &state, int biome, JavaRandom &random) {
    const auto rule = state.biomes[biome].description.grass;
    if (rule == BiomeRule::BiomeGenTaiga)
        return blockState(31, random.nextInt(5) > 0 ? 2 : 1);
    if (rule == BiomeRule::BiomeGenJungle)
        return blockState(31, random.nextInt(4) == 0 ? 2 : 1);
    return blockState(31, 1);
}
void spring(World &world, JavaRandom &random, Position p, BlockState liquid, const DecorationHooks &hooks) {
    if (id(world, p.add(0, 1, 0)) != 1 || id(world, p.add(0, -1, 0)) != 1 || (id(world, p) != 0 && id(world, p) != 1))
        return;
    constexpr Position sides[] = {{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}};
    int stone = 0, air = 0;
    for (auto side : sides) {
        const auto block = id(world, p.add(side.x, 0, side.z));
        if (block == 1)
            ++stone;
        if (block == 0)
            ++air;
    }
    if (stone == 3 && air == 1) {
        set(world, p, liquid);
        hooks.forceBlockTick(p.x, p.y, p.z, liquid, random);
    }
}
class DecorationOperation {
    World &world;
    SharedDecoration &state;
    JavaRandom &random;
    PopulationOutput &output;
    const DecorationHooks &hooks;
    Position origin;
    std::function<int(Position)> light;
    Position horizontal(int spread = 16, int offset = 8) {
        const int x = random.nextInt(spread) + offset, z = random.nextInt(spread) + offset;
        return origin.add(x, 0, z);
    }
    bool scatter(Position p, ScatterKind kind, BlockState block = 0) {
        return detail::generateScatter(world, random, p, kind, block, hooks.noSky, light);
    }
    void doubles(int count, unsigned type) {
        for (int i = 0; i < count; ++i) {
            auto p = horizontal();
            p.y = random.nextInt(height(world, p.x, p.z) + 32);
            scatter(p, ScatterKind::DoublePlant, blockState(175, type));
        }
    }
    void randomHeightPatch(ScatterKind kind, BlockState block = 0) {
        auto p = horizontal();
        const int limit = height(world, p.x, p.z) * 2;
        if (limit > 0) {
            p.y = random.nextInt(limit);
            scatter(p, kind, block);
        }
    }
    void core(int biome, int owner) {
        auto &cache = state.biomes[owner];
        auto &d = cache.description;
        if (cache.active)
            throw std::runtime_error("Already decorating");
        cache.active = true;
        if (owner == 9) {
            detail::populateEnd(world, random, origin.x / 16, origin.z / 16, output);
            cache.active = false;
            return;
        }
        detail::generateDefaultOres(world, random, origin);
        for (int i = 0; i < d.sandPerChunk2; ++i)
            detail::generateSediment(world, random, solidTop(world, horizontal()), blockState(12), 7);
        for (int i = 0; i < d.clayPerChunk; ++i)
            detail::generateSediment(world, random, solidTop(world, horizontal()), blockState(82), 4, true);
        for (int i = 0; i < d.sandPerChunk; ++i)
            detail::generateSediment(world, random, solidTop(world, horizontal()), blockState(13), 6);
        int trees = d.treesPerChunk;
        if (random.nextInt(10) == 0)
            ++trees;
        for (int i = 0; i < trees; ++i) {
            const auto p = horizontal();
            const auto choice = chooseTree(state, biome, random);
            tree(world, state, random, above(world, p), choice);
        }
        for (int i = 0; i < d.bigMushroomsPerChunk; ++i)
            detail::generateHugeMushroom(world, random, above(world, horizontal()), cache.mushroom);
        for (int i = 0; i < d.flowersPerChunk; ++i) {
            auto p = horizontal();
            const int limit = height(world, p.x, p.z) + 32;
            if (limit > 0) {
                p.y = random.nextInt(limit);
                const auto block = flower(state, biome, random, p);
                scatter(p, ScatterKind::Flowers, block);
            }
        }
        for (int i = 0; i < d.grassPerChunk; ++i) {
            auto p = horizontal();
            const int limit = height(world, p.x, p.z) * 2;
            if (limit > 0) {
                p.y = random.nextInt(limit);
                const auto block = grass(state, biome, random);
                scatter(p, ScatterKind::TallGrass, block);
            }
        }
        for (int i = 0; i < d.deadBushPerChunk; ++i)
            randomHeightPatch(ScatterKind::DeadBush);
        for (int i = 0; i < d.waterlilyPerChunk; ++i) {
            auto p = horizontal();
            const int limit = height(world, p.x, p.z) * 2;
            if (limit > 0) {
                p.y = random.nextInt(limit);
                while (p.y > 0 && id(world, p.add(0, -1, 0)) == 0)
                    --p.y;
                scatter(p, ScatterKind::Waterlily);
            }
        }
        for (int i = 0; i < d.mushroomsPerChunk; ++i) {
            if (random.nextInt(4) == 0)
                scatter(above(world, horizontal()), ScatterKind::Mushroom, blockState(39));
            if (random.nextInt(8) == 0)
                randomHeightPatch(ScatterKind::Mushroom, blockState(40));
        }
        if (random.nextInt(4) == 0)
            randomHeightPatch(ScatterKind::Mushroom, blockState(39));
        if (random.nextInt(8) == 0)
            randomHeightPatch(ScatterKind::Mushroom, blockState(40));
        for (int i = 0; i < d.reedsPerChunk; ++i)
            randomHeightPatch(ScatterKind::Reed);
        for (int i = 0; i < 10; ++i)
            randomHeightPatch(ScatterKind::Reed);
        if (random.nextInt(32) == 0)
            randomHeightPatch(ScatterKind::Pumpkin);
        for (int i = 0; i < d.cactiPerChunk; ++i)
            randomHeightPatch(ScatterKind::Cactus);
        if (d.springs && hooks.springs) {
            for (int i = 0; i < 50; ++i) {
                auto p = horizontal();
                const int limit = random.nextInt(248) + 8;
                if (limit > 0) {
                    p.y = random.nextInt(limit);
                    spring(world, random, p, blockState(8), hooks);
                }
            }
            for (int i = 0; i < 20; ++i) {
                auto p = horizontal();
                const int inner = random.nextInt(240) + 8, middle = random.nextInt(inner) + 8;
                p.y = random.nextInt(middle);
                spring(world, random, p, blockState(10), hooks);
            }
        }
        cache.active = false;
    }

  public:
    DecorationOperation(World &w, SharedDecoration &s, JavaRandom &r, PopulationOutput &o, const DecorationHooks &h,
                        Position p)
        : world(w), state(s), random(r), output(o), hooks(h), origin(p) {
        if (hooks.light)
            light = [&](Position q) { return hooks.light(q.x, q.y, q.z); };
    }
    void decorate(int biome) {
        auto &d = state.biomes[biome].description;
        if (d.decorate == BiomeRule::BiomeGenForest_1) {
            decorate(d.delegate);
            return;
        }
        if (d.decorate == BiomeRule::BiomeGenMutated) {
            core(biome, d.delegate);
            return;
        }
        if (d.decorate == BiomeRule::BiomeGenPlains) {
            if (state.grassNoise.sample(double(origin.x + 8) / 200.0, double(origin.z + 8) / 200.0) < -.8) {
                d.flowersPerChunk = 15;
                d.grassPerChunk = 5;
            } else {
                d.flowersPerChunk = 4;
                d.grassPerChunk = 10;
                doubles(7, 2);
            }
            if (d.sunflowers)
                doubles(10, 0);
        } else if (d.decorate == BiomeRule::BiomeGenForest) {
            if (d.variant == 3)
                for (int x = 0; x < 4; ++x)
                    for (int z = 0; z < 4; ++z) {
                        const int px = x * 4 + 9 + random.nextInt(3), pz = z * 4 + 9 + random.nextInt(3);
                        const auto p = above(world, origin.add(px, 0, pz));
                        if (random.nextInt(20) == 0) {
                            unsigned type = 0;
                            detail::generateHugeMushroom(world, random, p, type);
                        } else {
                            const auto choice = chooseTree(state, biome, random);
                            tree(world, state, random, p, choice);
                        }
                    }
            int patches = random.nextInt(5) - 3;
            if (d.variant == 1)
                patches += 2;
            for (int i = 0; i < patches; ++i) {
                constexpr unsigned types[] = {1, 4, 5};
                const auto type = types[random.nextInt(3)];
                for (int attempt = 0; attempt < 5; ++attempt) {
                    auto p = horizontal();
                    p.y = random.nextInt(height(world, p.x, p.z) + 32);
                    if (scatter(p, ScatterKind::DoublePlant, blockState(175, type)))
                        break;
                }
            }
        } else if (d.decorate == BiomeRule::BiomeGenTaiga) {
            if (d.variant == 1 || d.variant == 2) {
                const int count = random.nextInt(3);
                for (int i = 0; i < count; ++i)
                    detail::generateBlob(world, random, above(world, horizontal()), blockState(48), 0);
            }
            doubles(7, 3);
        } else if (d.decorate == BiomeRule::BiomeGenSavanna)
            doubles(7, 2);
        else if (d.decorate == BiomeRule::BiomeGenSnow && d.iceSpikes) {
            for (int i = 0; i < 3; ++i)
                detail::generateIceSpike(world, random, above(world, horizontal()));
            for (int i = 0; i < 2; ++i)
                detail::generateIcePath(world, random, above(world, horizontal()));
        }
        core(biome, biome);
        if (d.decorate == BiomeRule::BiomeGenHills) {
            const int count = 3 + random.nextInt(6);
            for (int i = 0; i < count; ++i) {
                const int x = random.nextInt(16), y = random.nextInt(28) + 4, z = random.nextInt(16);
                const auto p = origin.add(x, y, z);
                if (id(world, p) == 1)
                    set(world, p, blockState(129));
            }
            for (int i = 0; i < 7; ++i) {
                const int x = random.nextInt(16), y = random.nextInt(64), z = random.nextInt(16);
                detail::generateOre(world, random, origin.add(x, y, z), blockState(97), 9);
            }
        } else if (d.decorate == BiomeRule::BiomeGenJungle) {
            auto p = horizontal();
            p.y = random.nextInt(height(world, p.x, p.z) * 2);
            scatter(p, ScatterKind::Melon);
            // Original Jungle calls WorldGenVines at Y128, where its Y<128
            // loop is empty. Preserve its two coordinate draws per attempt.
            for (int i = 0; i < 50; ++i) {
                auto vine = horizontal();
                vine.y = 128;
                detail::generateVines(world, random, vine);
            }
        } else if (d.decorate == BiomeRule::BiomeGenDesert && random.nextInt(1000) == 0) {
            auto p = above(world, horizontal());
            ++p.y;
            detail::generateDesertWell(world, p);
        }
    }
};
} // namespace
bool decorateBiome(World &world, int biome, int cx, int cz, JavaRandom &random, PopulationOutput &output,
                   const DecorationHooks &hooks, std::string *error) {
    if (error)
        error->clear();
    try {
        if (biome < 0 || biome >= 256 || !decorationDescriptions[biome].valid)
            throw std::invalid_argument("unknown biome decoration id");
        if (cx < -1875000 || cx >= 1875000 || cz < -1875000 || cz >= 1875000)
            throw std::invalid_argument("decoration coordinate exceeds world bounds");
        if (hooks.springs && !hooks.forceBlockTick && biome != 9)
            throw std::invalid_argument("spring decoration requires the owning server's immediate block-tick callback");
        for (int x = cx; x <= cx + 1; ++x)
            for (int z = cz; z <= cz + 1; ++z)
                if (x < 1875000 && z < 1875000 && !world.hasChunk(x, z))
                    throw std::runtime_error(
                        "biome decoration requires the target and its three positive-axis neighboring chunks");
        auto &state = shared();
        std::lock_guard<std::recursive_mutex> lock(state.mutex);
        DecorationOperation(world, state, random, output, hooks, {cx * 16, 0, cz * 16}).decorate(biome);
        return true;
    } catch (const std::exception &ex) {
        if (error)
            *error = ex.what();
        return false;
    }
}
} // namespace mc::worldgen
