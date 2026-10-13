#include "mc/worldgen_decoration.hpp"
#include "worldgen_features.hpp"
#include <stdexcept>

namespace mc::worldgen {
namespace {
using detail::Position;
unsigned id(const World &world, Position p) { return blockId(world.getBlock(p.x, p.y, p.z)); }
void lava(World &world, JavaRandom &random, Position p, bool enclosed, const DecorationHooks &hooks) {
    if (id(world, p.add(0, 1, 0)) != 87 || (id(world, p) != 0 && id(world, p) != 87))
        return;
    constexpr Position sides[] = {{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {0, -1, 0}};
    int solid = 0, air = 0;
    for (auto direction : sides) {
        const auto block = id(world, p.add(direction.x, direction.y, direction.z));
        if (block == 87)
            ++solid;
        if (block == 0)
            ++air;
    }
    if ((!enclosed && solid == 4 && air == 1) || solid == 5) {
        world.setBlock(p.x, p.y, p.z, blockState(10));
        hooks.forceBlockTick(p.x, p.y, p.z, blockState(10), random);
    }
}
} // namespace
bool populateNetherFeatures(World &world, int cx, int cz, JavaRandom &random, const DecorationHooks &hooks,
                            std::string *error) {
    if (error)
        error->clear();
    try {
        if (cx < -1875000 || cx >= 1875000 || cz < -1875000 || cz >= 1875000)
            throw std::invalid_argument("Nether population coordinate exceeds world bounds");
        if (!hooks.forceBlockTick)
            throw std::invalid_argument("Nether population requires the owning server's immediate block-tick callback");
        for (int x = cx; x <= cx + 1; ++x)
            for (int z = cz; z <= cz + 1; ++z)
                if (x < 1875000 && z < 1875000 && !world.hasChunk(x, z))
                    throw std::runtime_error(
                        "Nether population requires the target and its three positive-axis neighboring chunks");
        // Enclosed springs sample offsets 0..15, then inspect west/north at -1.
        // Vanilla World fetches those columns on demand. Treating missing data
        // as air silently suppresses valid border springs while leaving RNG equal.
        if ((cx > -1875000 && !world.hasChunk(cx - 1, cz)) || (cz > -1875000 && !world.hasChunk(cx, cz - 1)))
            throw std::runtime_error(
                "Nether enclosed springs require west and north neighbor columns for block queries");
        const Position origin{cx * 16, 0, cz * 16};
        auto position = [&](int height, int baseY, int offset) {
            const int x = random.nextInt(16) + offset, y = random.nextInt(height) + baseY,
                      z = random.nextInt(16) + offset;
            return origin.add(x, y, z);
        };
        std::function<int(Position)> light;
        if (hooks.light)
            light = [&](Position p) { return hooks.light(p.x, p.y, p.z); };
        for (int i = 0; i < 8; ++i)
            lava(world, random, position(120, 4, 8), false, hooks);
        // These are re-sampled loop conditions in the original source, not
        // random counts sampled once at the beginning of each loop.
        for (int i = 0;; ++i) {
            const int inner = random.nextInt(10) + 1, limit = random.nextInt(inner) + 1;
            if (i >= limit)
                break;
            detail::generateScatter(world, random, position(120, 4, 8), detail::ScatterKind::Fire, 0, true, light);
        }
        for (int i = 0;; ++i) {
            const int inner = random.nextInt(10) + 1, limit = random.nextInt(inner);
            if (i >= limit)
                break;
            detail::generateScatter(world, random, position(120, 4, 8), detail::ScatterKind::Glowstone, 0, true, light);
        }
        for (int i = 0; i < 10; ++i)
            detail::generateScatter(world, random, position(128, 0, 8), detail::ScatterKind::Glowstone, 0, true, light);
        if (random.nextBoolean())
            detail::generateScatter(world, random, position(128, 0, 8), detail::ScatterKind::Mushroom, blockState(39),
                                    true, light);
        if (random.nextBoolean())
            detail::generateScatter(world, random, position(128, 0, 8), detail::ScatterKind::Mushroom, blockState(40),
                                    true, light);
        for (int i = 0; i < 16; ++i)
            detail::generateOre(world, random, position(108, 10, 0), blockState(153), 14, 87);
        for (int i = 0; i < 16; ++i)
            lava(world, random, position(108, 10, 0), true, hooks);
        return true;
    } catch (const std::exception &ex) {
        if (error)
            *error = ex.what();
        return false;
    }
}
} // namespace mc::worldgen
