#include "worldgen_features.hpp"
#include "mc/worldgen_loot.hpp"
#include "mc/registry.hpp"

namespace mc::worldgen::detail {
namespace {
constexpr Position horizontal[] = {{0, 0, -1}, {1, 0, 0}, {0, 0, 1}, {-1, 0, 0}};
constexpr unsigned facingMeta[] = {2, 5, 3, 4};
unsigned id(const World &world, Position p) { return blockId(world.getBlock(p.x, p.y, p.z)); }
bool solid(const World &world, Position p) { return registry::block(id(world, p)).materialSolid; }
bool full(const World &world, Position p) { return registry::block(id(world, p)).fullBlock; }
bool tileBlock(unsigned id) {
    for (unsigned value : {23u,  25u,  36u,  52u,  54u,  61u,  62u,  63u,  68u,  84u,  116u, 117u, 119u, 130u,
                           137u, 138u, 140u, 144u, 146u, 149u, 150u, 151u, 154u, 158u, 176u, 177u, 178u})
        if (id == value)
            return true;
    return false;
}
void set(World &world, Position p, BlockState block, PopulationOutput &output) {
    const auto previous = world.getBlock(p.x, p.y, p.z);
    if (world.setBlock(p.x, p.y, p.z, block) && blockId(previous) != blockId(block) && tileBlock(blockId(previous)))
        output.blockEntities.push_back({p.x, p.y, p.z, {}});
}
BlockState correctChestFacing(const World &world, Position p) {
    int neighbor = -1;
    for (int side = 0; side < 4; ++side) {
        const auto d = horizontal[side];
        const auto at = p.add(d.x, 0, d.z);
        if (id(world, at) == 54)
            return blockState(54, 2);
        if (full(world, at)) {
            if (neighbor != -1) {
                neighbor = -1;
                break;
            }
            neighbor = side;
        }
    }
    if (neighbor != -1)
        return blockState(54, facingMeta[(neighbor + 2) & 3]);
    int direction = 0;
    auto blocked = [&]() { return full(world, p.add(horizontal[direction].x, 0, horizontal[direction].z)); };
    if (blocked())
        direction = (direction + 2) & 3;
    if (blocked())
        direction = (direction + 1) & 3;
    if (blocked())
        direction = (direction + 2) & 3;
    return blockState(54, facingMeta[direction]);
}
void adjustChest(World &world, Position p) {
    const Position n = p.add(0, 0, -1), s = p.add(0, 0, 1), w = p.add(-1, 0, 0), e = p.add(1, 0, 0);
    unsigned face = blockMeta(world.getBlock(p.x, p.y, p.z));
    if (id(world, n) != 54 && id(world, s) != 54) {
        if (id(world, w) == 54 || id(world, e) == 54) {
            const auto other = id(world, w) == 54 ? w : e;
            const auto on = other.add(0, 0, -1), os = other.add(0, 0, 1);
            face = blockMeta(world.getBlock(other.x, other.y, other.z)) == 2 ? 2 : 3;
            if ((full(world, n) || full(world, on)) && !full(world, s) && !full(world, os))
                face = 3;
            if ((full(world, s) || full(world, os)) && !full(world, n) && !full(world, on))
                face = 2;
        }
    } else {
        const auto other = id(world, n) == 54 ? n : s;
        const auto ow = other.add(-1, 0, 0), oe = other.add(1, 0, 0);
        face = blockMeta(world.getBlock(other.x, other.y, other.z)) == 4 ? 4 : 5;
        if ((full(world, w) || full(world, ow)) && !full(world, e) && !full(world, oe))
            face = 5;
        if ((full(world, e) || full(world, oe)) && !full(world, w) && !full(world, ow))
            face = 4;
    }
    world.setBlock(p.x, p.y, p.z, blockState(54, face));
}
void chestAdded(World &world, Position p) {
    adjustChest(world, p);
    for (auto d : horizontal) {
        const auto other = p.add(d.x, 0, d.z);
        if (id(world, other) == 54)
            adjustChest(world, other);
    }
}
} // namespace
bool generateDungeon(World &world, JavaRandom &random, Position at, PopulationOutput &output) {
    const int radiusX = random.nextInt(2) + 2, minX = -radiusX - 1, maxX = radiusX + 1;
    const int radiusZ = random.nextInt(2) + 2, minZ = -radiusZ - 1, maxZ = radiusZ + 1;
    int openings = 0;
    for (int x = minX; x <= maxX; ++x)
        for (int y = -1; y <= 4; ++y)
            for (int z = minZ; z <= maxZ; ++z) {
                const auto p = at.add(x, y, z);
                const bool materialSolid = solid(world, p);
                if ((y == -1 || y == 4) && !materialSolid)
                    return false;
                if ((x == minX || x == maxX || z == minZ || z == maxZ) && y == 0 && id(world, p) == 0 &&
                    id(world, p.add(0, 1, 0)) == 0)
                    ++openings;
            }
    if (openings < 1 || openings > 5)
        return false;
    for (int x = minX; x <= maxX; ++x)
        for (int y = 3; y >= -1; --y)
            for (int z = minZ; z <= maxZ; ++z) {
                const auto p = at.add(x, y, z);
                if (x != minX && y != -1 && z != minZ && x != maxX && z != maxZ) {
                    if (id(world, p) != 54)
                        set(world, p, 0, output);
                } else if (p.y >= 0 && !solid(world, p.add(0, -1, 0)))
                    set(world, p, 0, output);
                else if (solid(world, p) && id(world, p) != 54)
                    set(world, p, blockState(y == -1 && random.nextInt(4) != 0 ? 48 : 4), output);
            }
    for (int chest = 0; chest < 2; ++chest)
        for (int attempt = 0; attempt < 3; ++attempt) {
            const int x = at.x + random.nextInt(radiusX * 2 + 1) - radiusX,
                      z = at.z + random.nextInt(radiusZ * 2 + 1) - radiusZ;
            const Position p{x, at.y, z};
            if (id(world, p) == 0) {
                int neighbors = 0;
                for (auto d : horizontal)
                    if (solid(world, p.add(d.x, 0, d.z)))
                        ++neighbors;
                if (neighbors == 1) {
                    set(world, p, correctChestFacing(world, p), output);
                    chestAdded(world, p);
                    output.blockEntities.push_back({p.x, p.y, p.z, makeDungeonChest(random, p.x, p.y, p.z)});
                    break;
                }
            }
        }
    set(world, at, blockState(52), output);
    output.blockEntities.push_back({at.x, at.y, at.z, makeDungeonSpawner(random, at.x, at.y, at.z)});
    return true;
}
} // namespace mc::worldgen::detail
