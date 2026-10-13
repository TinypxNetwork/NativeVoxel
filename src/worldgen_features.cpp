#include "worldgen_features.hpp"
#include "mc/registry.hpp"
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace mc::worldgen::detail {
namespace {
BlockState stateAt(const World &world, Position at) { return world.getBlock(at.x, at.y, at.z); }
unsigned idAt(const World &world, Position at) { return blockId(stateAt(world, at)); }
bool materialIs(const World &world, Position at, const char *material) {
    return std::strcmp(registry::block(idAt(world, at)).material, material) == 0;
}
void set(World &world, Position at, BlockState state) { world.setBlock(at.x, at.y, at.z, state); }
bool plantSoil(unsigned id) { return id == 2 || id == 3 || id == 60; }
bool materialReplaceable(const World &world, Position at) {
    const char *material = registry::block(idAt(world, at)).material;
    for (const auto *kind : {"air", "fire", "water", "lava", "vine", "snow"})
        if (std::strcmp(kind, material) == 0)
            return true;
    return false;
}
Position scatter(JavaRandom &random, Position at) {
    const int x0 = random.nextInt(8), x1 = random.nextInt(8), y0 = random.nextInt(4), y1 = random.nextInt(4),
              z0 = random.nextInt(8), z1 = random.nextInt(8);
    return at.add(x0 - x1, y0 - y1, z0 - z1);
}
constexpr Position horizontal[] = {{0, 0, -1}, {1, 0, 0}, {0, 0, 1}, {-1, 0, 0}};
constexpr Position faces[] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
bool adjacentWater(const World &world, Position at) {
    for (auto side : horizontal)
        if (materialIs(world, at.add(side.x, side.y, side.z), "water"))
            return true;
    return false;
}
bool cactusStays(const World &world, Position at) {
    for (auto side : horizontal)
        if (registry::block(idAt(world, at.add(side.x, 0, side.z))).materialSolid)
            return false;
    const unsigned below = idAt(world, at.add(0, -1, 0));
    return below == 81 || below == 12;
}
bool reedStays(const World &world, Position at) {
    const unsigned below = idAt(world, at.add(0, -1, 0));
    return below == 83 || ((below == 2 || below == 3 || below == 12) && adjacentWater(world, at.add(0, -1, 0)));
}
} // namespace
bool generateOre(World &world, JavaRandom &random, Position at, BlockState ore, int count, unsigned replaceId) {
    if (count < 0 || count > 256)
        throw std::invalid_argument("ore vein size exceeds supported vanilla settings");
    const float angle = random.nextFloat() * 3.1415927f;
    const double x0 = double(float(at.x + 8) + javaSin(angle) * float(count) / 8.0f);
    const double x1 = double(float(at.x + 8) - javaSin(angle) * float(count) / 8.0f);
    const double z0 = double(float(at.z + 8) + javaCos(angle) * float(count) / 8.0f);
    const double z1 = double(float(at.z + 8) - javaCos(angle) * float(count) / 8.0f);
    const double y0 = double(at.y + random.nextInt(3) - 2), y1 = double(at.y + random.nextInt(3) - 2);
    for (int step = 0; step < count; ++step) {
        const float progress = float(step) / float(count);
        const double x = x0 + (x1 - x0) * double(progress), y = y0 + (y1 - y0) * double(progress),
                     z = z0 + (z1 - z0) * double(progress);
        const double size = random.nextDouble() * double(count) / 16.0;
        const double width = double(javaSin(3.1415927f * progress) + 1.0f) * size + 1.0;
        const double height = double(javaSin(3.1415927f * progress) + 1.0f) * size + 1.0;
        const int minX = floorBlock(x - width / 2.0), minY = floorBlock(y - height / 2.0),
                  minZ = floorBlock(z - width / 2.0);
        const int maxX = floorBlock(x + width / 2.0), maxY = floorBlock(y + height / 2.0),
                  maxZ = floorBlock(z + width / 2.0);
        for (int bx = minX; bx <= maxX; ++bx) {
            const double nx = (double(bx) + .5 - x) / (width / 2.0);
            if (nx * nx < 1.0) {
                for (int by = minY; by <= maxY; ++by) {
                    const double ny = (double(by) + .5 - y) / (height / 2.0);
                    if (nx * nx + ny * ny < 1.0) {
                        for (int bz = minZ; bz <= maxZ; ++bz) {
                            const double nz = (double(bz) + .5 - z) / (width / 2.0);
                            if (nx * nx + ny * ny + nz * nz < 1.0 && blockId(world.getBlock(bx, by, bz)) == replaceId)
                                world.setBlock(bx, by, bz, ore);
                        }
                    }
                }
            }
        }
    }
    return true;
}
void generateDefaultOres(World &world, JavaRandom &random, Position origin) {
    struct Ore {
        BlockState state;
        int size, count, maxHeight;
    };
    static constexpr Ore ores[] = {{blockState(3), 33, 10, 256},   {blockState(13), 33, 8, 256},
                                   {blockState(1, 3), 33, 10, 80}, {blockState(1, 1), 33, 10, 80},
                                   {blockState(1, 5), 33, 10, 80}, {blockState(16), 17, 20, 128},
                                   {blockState(15), 9, 20, 64},    {blockState(14), 9, 2, 32},
                                   {blockState(73), 8, 8, 16},     {blockState(56), 8, 1, 16}};
    for (const auto &ore : ores)
        for (int vein = 0; vein < ore.count; ++vein) {
            const int x = random.nextInt(16), y = random.nextInt(ore.maxHeight), z = random.nextInt(16);
            generateOre(world, random, origin.add(x, y, z), ore.state, ore.size);
        }
    const int x = random.nextInt(16), y0 = random.nextInt(16), y1 = random.nextInt(16), z = random.nextInt(16);
    generateOre(world, random, origin.add(x, y0 + y1, z), blockState(21), 7);
}
int topSolidOrLiquid(const World &world, int x, int z) {
    for (int y = 255; y >= 0; --y) {
        const auto &block = registry::block(blockId(world.getBlock(x, y, z)));
        if (block.materialBlocksMovement && std::strcmp(block.material, "leaves") != 0)
            return y + 1;
    }
    return -1;
}
bool generateSpike(World &world, JavaRandom &random, Position at, unsigned requiredBase, PopulationOutput &output) {
    if (blockId(world.getBlock(at.x, at.y, at.z)) != 0 || blockId(world.getBlock(at.x, at.y - 1, at.z)) != requiredBase)
        return false;
    const int height = random.nextInt(32) + 6, radius = random.nextInt(4) + 1;
    for (int x = at.x - radius; x <= at.x + radius; ++x)
        for (int z = at.z - radius; z <= at.z + radius; ++z) {
            const int dx = x - at.x, dz = z - at.z;
            if (dx * dx + dz * dz <= radius * radius + 1 && blockId(world.getBlock(x, at.y - 1, z)) != requiredBase)
                return false;
        }
    for (int y = at.y; y < at.y + height && y < 256; ++y)
        for (int x = at.x - radius; x <= at.x + radius; ++x)
            for (int z = at.z - radius; z <= at.z + radius; ++z) {
                const int dx = x - at.x, dz = z - at.z;
                if (dx * dx + dz * dz <= radius * radius + 1)
                    world.setBlock(x, y, z, blockState(49));
            }
    output.entities.push_back({"EnderCrystal", double(float(at.x) + .5f), double(at.y + height),
                               double(float(at.z) + .5f), random.nextFloat() * 360.0f, 0.0f});
    world.setBlock(at.x, at.y + height, at.z, blockState(7));
    return true;
}
void populateEnd(World &world, JavaRandom &random, int cx, int cz, PopulationOutput &output) {
    const Position origin{cx * 16, 0, cz * 16};
    generateDefaultOres(world, random, origin);
    if (random.nextInt(5) == 0) {
        const int x = random.nextInt(16) + 8, z = random.nextInt(16) + 8;
        generateSpike(world, random, {origin.x + x, topSolidOrLiquid(world, origin.x + x, origin.z + z), origin.z + z},
                      121, output);
    }
    if (cx == 0 && cz == 0)
        output.entities.push_back({"EnderDragon", 0.0, 128.0, 0.0, random.nextFloat() * 360.0f, 0.0f});
}
bool generateScatter(World &world, JavaRandom &random, Position at, ScatterKind kind, BlockState state, bool noSky,
                     const std::function<int(Position)> &light) {
    if (kind == ScatterKind::TallGrass || kind == ScatterKind::DeadBush)
        while ((idAt(world, at) == 0 || materialIs(world, at, "leaves")) && at.y > 0)
            --at.y;
    if (kind == ScatterKind::Glowstone) {
        if (idAt(world, at) != 0 || idAt(world, at.add(0, 1, 0)) != 87)
            return false;
        set(world, at, blockState(89));
        for (int i = 0; i < 1500; ++i) {
            const int x0 = random.nextInt(8), x1 = random.nextInt(8), dy = -random.nextInt(12), z0 = random.nextInt(8),
                      z1 = random.nextInt(8);
            const auto p = at.add(x0 - x1, dy, z0 - z1);
            if (idAt(world, p) == 0) {
                int neighbors = 0;
                for (auto face : faces) {
                    if (idAt(world, p.add(face.x, face.y, face.z)) == 89)
                        ++neighbors;
                    if (neighbors > 1)
                        break;
                }
                if (neighbors == 1)
                    set(world, p, blockState(89));
            }
        }
        return true;
    }
    const int count = kind == ScatterKind::TallGrass                                  ? 128
                      : kind == ScatterKind::DeadBush                                 ? 4
                      : kind == ScatterKind::Cactus || kind == ScatterKind::Waterlily ? 10
                      : kind == ScatterKind::Reed                                     ? 20
                                                                                      : 64;
    bool placed = false;
    for (int i = 0; i < count; ++i) {
        Position p;
        if (kind == ScatterKind::Reed) {
            const int x0 = random.nextInt(4), x1 = random.nextInt(4), z0 = random.nextInt(4), z1 = random.nextInt(4);
            p = at.add(x0 - x1, 0, z0 - z1);
        } else
            p = scatter(random, at);
        const unsigned current = idAt(world, p), below = idAt(world, p.add(0, -1, 0));
        switch (kind) {
        case ScatterKind::Flowers:
            if (current == 0 && (!noSky || p.y < 255) && plantSoil(below))
                set(world, p, state);
            break;
        case ScatterKind::TallGrass:
            if (current == 0 && plantSoil(below))
                set(world, p, state);
            break;
        case ScatterKind::DeadBush:
            if (current == 0 && (below == 12 || below == 172 || below == 159 || below == 3))
                set(world, p, blockState(32));
            break;
        case ScatterKind::DoublePlant:
            if (current == 0 && (!noSky || p.y < 254) && plantSoil(below) && idAt(world, p.add(0, 1, 0)) == 0) {
                set(world, p, blockState(175, blockMeta(state)));
                set(world, p.add(0, 1, 0), blockState(175, 10));
                placed = true;
            }
            break;
        case ScatterKind::Cactus:
            if (current == 0) {
                const int bound = random.nextInt(3) + 1, height = 1 + random.nextInt(bound);
                for (int k = 0; k < height; ++k)
                    if (cactusStays(world, p))
                        set(world, p.add(0, k, 0), blockState(81));
            }
            break;
        case ScatterKind::Reed:
            if (current == 0 && adjacentWater(world, p.add(0, -1, 0))) {
                const int bound = random.nextInt(3) + 1, height = 2 + random.nextInt(bound);
                for (int k = 0; k < height; ++k)
                    if (reedStays(world, p))
                        set(world, p.add(0, k, 0), blockState(83));
            }
            break;
        case ScatterKind::Pumpkin:
            if (current == 0 && below == 2) {
                static constexpr unsigned facing[] = {2, 3, 0, 1};
                set(world, p, blockState(86, facing[random.nextInt(4)]));
            }
            break;
        case ScatterKind::Melon:
            if (materialReplaceable(world, p) && below == 2)
                set(world, p, blockState(103));
            break;
        case ScatterKind::Waterlily:
            // WorldGenWaterlily calls canPlaceBlockAt, whose soil rule checks
            // still-water identity, not canBlockStay's source-level predicate.
            if (current == 0 && below == 9)
                set(world, p, blockState(111));
            break;
        case ScatterKind::Mushroom:
            if (current == 0 && (!noSky || p.y < 255) && p.y >= 0 && p.y < 256) {
                const auto ground = stateAt(world, p.add(0, -1, 0));
                if (below == 110 || (below == 3 && blockMeta(ground) == 2) ||
                    ((light ? light(p)
                            : std::max(world.getSkyLight(p.x, p.y, p.z), world.getBlockLight(p.x, p.y, p.z))) < 13 &&
                     registry::block(below).fullBlock))
                    set(world, p, state);
            }
            break;
        case ScatterKind::Fire:
            if (current == 0 && below == 87)
                set(world, p, blockState(51));
            break;
        case ScatterKind::Glowstone:
            break;
        }
    }
    return kind == ScatterKind::DoublePlant ? placed : true;
}
bool generateSediment(World &world, JavaRandom &random, Position at, BlockState state, int radius, bool clay) {
    if (!materialIs(world, at, "water"))
        return false;
    if (radius <= 2 || radius > 32)
        throw std::invalid_argument("invalid sediment radius");
    const int chosen = random.nextInt(radius - 2) + 2, depth = clay ? 1 : 2;
    for (int x = at.x - chosen; x <= at.x + chosen; ++x)
        for (int z = at.z - chosen; z <= at.z + chosen; ++z) {
            const int dx = x - at.x, dz = z - at.z;
            if (dx * dx + dz * dz <= chosen * chosen) {
                for (int y = at.y - depth; y <= at.y + depth; ++y) {
                    const unsigned block = blockId(world.getBlock(x, y, z));
                    if (block == 3 || block == (clay ? 82u : 2u))
                        world.setBlock(x, y, z, state);
                }
            }
        }
    return true;
}
bool generateBlob(World &world, JavaRandom &random, Position at, BlockState state, int radius) {
    if (radius > 16)
        throw std::invalid_argument("unsupported blob radius");
    while (at.y > 3) {
        const unsigned below = idAt(world, at.add(0, -1, 0));
        if (below == 2 || below == 3 || below == 1)
            break;
        --at.y;
    }
    if (at.y <= 3)
        return false;
    for (int i = 0; radius >= 0 && i < 3; ++i) {
        const int x = radius + random.nextInt(2), y = radius + random.nextInt(2), z = radius + random.nextInt(2);
        const float size = float(x + y + z) * .333f + .5f;
        for (int dz = -z; dz <= z; ++dz)
            for (int dy = -y; dy <= y; ++dy)
                for (int dx = -x; dx <= x; ++dx)
                    if (double(dx * dx + dy * dy + dz * dz) <= double(size * size))
                        set(world, at.add(dx, dy, dz), state);
        const int dx = -(radius + 1) + random.nextInt(2 + radius * 2), dy = -random.nextInt(2),
                  dz = -(radius + 1) + random.nextInt(2 + radius * 2);
        at = at.add(dx, dy, dz);
    }
    return true;
}
bool generateIceSpike(World &world, JavaRandom &random, Position at) {
    while (idAt(world, at) == 0 && at.y > 2)
        --at.y;
    if (idAt(world, at) != 80)
        return false;
    at.y += random.nextInt(4);
    const int height = random.nextInt(4) + 7, radius = height / 4 + random.nextInt(2);
    if (radius > 1 && random.nextInt(60) == 0)
        at.y += 10 + random.nextInt(30);
    auto replace = [&](Position p) {
        const unsigned id = idAt(world, p);
        if (id == 0 || id == 3 || id == 80 || id == 79)
            set(world, p, blockState(174));
    };
    for (int y = 0; y < height; ++y) {
        const float width = (1.0f - float(y) / float(height)) * float(radius);
        const int edge = int(std::ceil(width));
        for (int x = -edge; x <= edge; ++x) {
            const float dx = float(std::abs(x)) - .25f;
            for (int z = -edge; z <= edge; ++z) {
                const float dz = float(std::abs(z)) - .25f;
                if (((x == 0 && z == 0) || dx * dx + dz * dz <= width * width) &&
                    ((x != -edge && x != edge && z != -edge && z != edge) || random.nextFloat() <= .75f)) {
                    replace(at.add(x, y, z));
                    if (y != 0 && edge > 1)
                        replace(at.add(x, -y, z));
                }
            }
        }
    }
    const int rootRadius = std::clamp(radius - 1, 0, 1);
    for (int x = -rootRadius; x <= rootRadius; ++x)
        for (int z = -rootRadius; z <= rootRadius; ++z) {
            Position p = at.add(x, -1, z);
            int remaining = 50;
            if (std::abs(x) == 1 && std::abs(z) == 1)
                remaining = random.nextInt(5);
            while (p.y > 50) {
                const unsigned id = idAt(world, p);
                if (id != 0 && id != 3 && id != 80 && id != 79 && id != 174)
                    break;
                set(world, p, blockState(174));
                --p.y;
                --remaining;
                if (remaining <= 0) {
                    p.y -= random.nextInt(5) + 1;
                    remaining = random.nextInt(5);
                }
            }
        }
    return true;
}
bool generateIcePath(World &world, JavaRandom &random, Position at, int width) {
    while (idAt(world, at) == 0 && at.y > 2)
        --at.y;
    if (idAt(world, at) != 80)
        return false;
    if (width <= 2 || width > 32)
        throw std::invalid_argument("invalid ice path width");
    const int radius = random.nextInt(width - 2) + 2;
    for (int x = -radius; x <= radius; ++x)
        for (int z = -radius; z <= radius; ++z)
            if (x * x + z * z <= radius * radius)
                for (int y = -1; y <= 1; ++y) {
                    const auto p = at.add(x, y, z);
                    const unsigned id = idAt(world, p);
                    if (id == 3 || id == 80 || id == 79)
                        set(world, p, blockState(174));
                }
    return true;
}
bool generateDesertWell(World &world, Position at) {
    while (idAt(world, at) == 0 && at.y > 2)
        --at.y;
    if (stateAt(world, at) != blockState(12))
        return false;
    for (int x = -2; x <= 2; ++x)
        for (int z = -2; z <= 2; ++z)
            if (idAt(world, at.add(x, -1, z)) == 0 && idAt(world, at.add(x, -2, z)) == 0)
                return false;
    for (int y = -1; y <= 0; ++y)
        for (int x = -2; x <= 2; ++x)
            for (int z = -2; z <= 2; ++z)
                set(world, at.add(x, y, z), blockState(24));
    set(world, at, blockState(8));
    for (auto direction : horizontal)
        set(world, at.add(direction.x, 0, direction.z), blockState(8));
    for (int x = -2; x <= 2; ++x)
        for (int z = -2; z <= 2; ++z)
            if (x == -2 || x == 2 || z == -2 || z == 2)
                set(world, at.add(x, 1, z), blockState(24));
    set(world, at.add(2, 1, 0), blockState(44, 1));
    set(world, at.add(-2, 1, 0), blockState(44, 1));
    set(world, at.add(0, 1, 2), blockState(44, 1));
    set(world, at.add(0, 1, -2), blockState(44, 1));
    for (int x = -1; x <= 1; ++x)
        for (int z = -1; z <= 1; ++z)
            set(world, at.add(x, 4, z), x == 0 && z == 0 ? blockState(24) : blockState(44, 1));
    for (int y = 1; y <= 3; ++y) {
        set(world, at.add(-1, y, -1), blockState(24));
        set(world, at.add(-1, y, 1), blockState(24));
        set(world, at.add(1, y, -1), blockState(24));
        set(world, at.add(1, y, 1), blockState(24));
    }
    return true;
}
bool generateVines(World &world, JavaRandom &random, Position at) {
    constexpr unsigned meta[] = {4, 8, 1, 2};
    for (; at.y < 128; ++at.y) {
        if (idAt(world, at) == 0) {
            for (int direction = 0; direction < 4; ++direction) {
                const auto offset = horizontal[direction];
                const auto &block = registry::block(idAt(world, at.add(-offset.x, 0, -offset.z)));
                if (block.fullCube && block.materialBlocksMovement) {
                    set(world, at, blockState(106, meta[direction]));
                    break;
                }
            }
        } else {
            const int x0 = random.nextInt(4), x1 = random.nextInt(4), z0 = random.nextInt(4), z1 = random.nextInt(4);
            at = at.add(x0 - x1, 0, z0 - z1);
        }
    }
    return true;
}
bool generateLake(World &world, JavaRandom &random, Position at, unsigned liquidBlock, const LakeHooks &hooks) {
    if (liquidBlock < 8 || liquidBlock > 11)
        throw std::invalid_argument("lake generator requires a water or lava block");
    if (!hooks.skyLight || !hooks.biomeTop || !hooks.canFreezeWater)
        throw std::invalid_argument("lake generator requires world light/biome/freezing queries");
    at = at.add(-8, 0, -8);
    while (at.y > 5 && idAt(world, at) == 0)
        --at.y;
    if (at.y <= 4)
        return false;
    at.y -= 4;
    std::array<bool, 2048> mask{};
    const int count = random.nextInt(4) + 4;
    auto index = [](int x, int y, int z) { return (x * 16 + z) * 8 + y; };
    for (int sphere = 0; sphere < count; ++sphere) {
        const double width = random.nextDouble() * 6.0 + 3.0, height = random.nextDouble() * 4.0 + 2.0,
                     depth = random.nextDouble() * 6.0 + 3.0;
        const double centerX = random.nextDouble() * (16.0 - width - 2.0) + 1.0 + width / 2.0;
        const double centerY = random.nextDouble() * (8.0 - height - 4.0) + 2.0 + height / 2.0;
        const double centerZ = random.nextDouble() * (16.0 - depth - 2.0) + 1.0 + depth / 2.0;
        for (int x = 1; x < 15; ++x)
            for (int z = 1; z < 15; ++z)
                for (int y = 1; y < 7; ++y) {
                    const double dx = (double(x) - centerX) / (width / 2.0),
                                 dy = (double(y) - centerY) / (height / 2.0),
                                 dz = (double(z) - centerZ) / (depth / 2.0);
                    if (dx * dx + dy * dy + dz * dz < 1.0)
                        mask[index(x, y, z)] = true;
                }
    }
    auto boundary = [&](int x, int y, int z) {
        return !mask[index(x, y, z)] && ((x < 15 && mask[index(x + 1, y, z)]) || (x > 0 && mask[index(x - 1, y, z)]) ||
                                         (z < 15 && mask[index(x, y, z + 1)]) || (z > 0 && mask[index(x, y, z - 1)]) ||
                                         (y < 7 && mask[index(x, y + 1, z)]) || (y > 0 && mask[index(x, y - 1, z)]));
    };
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = 0; y < 8; ++y)
                if (boundary(x, y, z)) {
                    const auto block = idAt(world, at.add(x, y, z));
                    if (y >= 4 && block >= 8 && block <= 11)
                        return false;
                    if (y < 4 && !registry::block(block).materialSolid && block != liquidBlock)
                        return false;
                }
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = 0; y < 8; ++y)
                if (mask[index(x, y, z)])
                    set(world, at.add(x, y, z), y >= 4 ? 0 : blockState(liquidBlock));
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = 4; y < 8; ++y)
                if (mask[index(x, y, z)]) {
                    const auto below = at.add(x, y - 1, z);
                    if (idAt(world, below) == 3 && hooks.skyLight(at.add(x, y, z)) > 0)
                        set(world, below, blockState(blockId(hooks.biomeTop(below)) == 110 ? 110 : 2));
                }
    if (liquidBlock == 10 || liquidBlock == 11) {
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z)
                for (int y = 0; y < 8; ++y)
                    if (boundary(x, y, z) && (y < 4 || random.nextInt(2) != 0) &&
                        registry::block(idAt(world, at.add(x, y, z))).materialSolid)
                        set(world, at.add(x, y, z), blockState(1));
    }
    if (liquidBlock == 8 || liquidBlock == 9)
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z) {
                const auto p = at.add(x, 4, z);
                if (hooks.canFreezeWater(p))
                    set(world, p, blockState(79));
            }
    return true;
}
} // namespace mc::worldgen::detail
