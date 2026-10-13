#include "worldgen_features.hpp"
#include "mc/registry.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace mc::worldgen::detail {
namespace {
unsigned id(const World &world, Position at) { return blockId(world.getBlock(at.x, at.y, at.z)); }
bool material(const World &world, Position at, const char *name) {
    return std::strcmp(registry::block(id(world, at)).material, name) == 0;
}
bool airOrLeaves(const World &world, Position at) { return id(world, at) == 0 || material(world, at, "leaves"); }
bool replaceable(const World &world, Position at) {
    const unsigned block = id(world, at);
    return airOrLeaves(world, at) || block == 2 || block == 3 || block == 17 || block == 162 || block == 6 ||
           block == 106;
}
bool fullBlock(const World &world, Position at) { return registry::block(id(world, at)).fullBlock; }
void set(World &world, Position at, BlockState state) { world.setBlock(at.x, at.y, at.z, state); }
void dirt(World &world, Position at) {
    if (id(world, at) != 3)
        set(world, at, blockState(3));
}
bool soil(unsigned block, bool farmland) { return block == 2 || block == 3 || (farmland && block == 60); }
void hangingVine(World &world, Position at, unsigned metadata) {
    set(world, at, blockState(106, metadata));
    --at.y;
    for (int remaining = 4; id(world, at) == 0 && remaining > 0; --remaining, --at.y)
        set(world, at, blockState(106, metadata));
}
void leafVines(World &world, JavaRandom &random, Position at, int height) {
    for (int y = at.y - 3 + height; y <= at.y + height; ++y) {
        const int level = y - (at.y + height), radius = 2 - level / 2;
        for (int x = at.x - radius; x <= at.x + radius; ++x)
            for (int z = at.z - radius; z <= at.z + radius; ++z) {
                const Position p{x, y, z};
                if (material(world, p, "leaves")) {
                    if (random.nextInt(4) == 0 && id(world, p.add(-1, 0, 0)) == 0)
                        hangingVine(world, p.add(-1, 0, 0), 8);
                    if (random.nextInt(4) == 0 && id(world, p.add(1, 0, 0)) == 0)
                        hangingVine(world, p.add(1, 0, 0), 2);
                    if (random.nextInt(4) == 0 && id(world, p.add(0, 0, -1)) == 0)
                        hangingVine(world, p.add(0, 0, -1), 1);
                    if (random.nextInt(4) == 0 && id(world, p.add(0, 0, 1)) == 0)
                        hangingVine(world, p.add(0, 0, 1), 4);
                }
            }
    }
}
bool broadleaf(World &world, JavaRandom &random, Position at, int height, TreeOptions options, bool birch, bool swamp) {
    if (at.y < 1 || at.y + height + 1 > 256)
        return false;
    for (int y = at.y; y <= at.y + height + 1; ++y) {
        int radius = y == at.y ? 0 : 1;
        if (y >= at.y + height - 1)
            radius = swamp ? 3 : 2;
        for (int x = at.x - radius; x <= at.x + radius; ++x)
            for (int z = at.z - radius; z <= at.z + radius; ++z) {
                const Position p{x, y, z};
                if (y < 0 || y >= 256)
                    return false;
                if (swamp) {
                    if (!airOrLeaves(world, p) && (!(id(world, p) == 8 || id(world, p) == 9) || y > at.y))
                        return false;
                } else if (!replaceable(world, p))
                    return false;
            }
    }
    if (!soil(id(world, at.add(0, -1, 0)), !swamp) || at.y >= 256 - height - 1)
        return false;
    dirt(world, at.add(0, -1, 0));
    for (int y = at.y - 3 + height; y <= at.y + height; ++y) {
        const int level = y - (at.y + height), radius = (swamp ? 2 : 1) - level / 2;
        for (int x = at.x - radius; x <= at.x + radius; ++x)
            for (int z = at.z - radius; z <= at.z + radius; ++z) {
                const int dx = x - at.x, dz = z - at.z;
                if (std::abs(dx) != radius || std::abs(dz) != radius || (random.nextInt(2) != 0 && level != 0)) {
                    const Position p{x, y, z};
                    if (swamp ? !fullBlock(world, p) : airOrLeaves(world, p) || (!birch && material(world, p, "vine")))
                        set(world, p, options.leaves);
                }
            }
    }
    for (int y = 0; y < height; ++y) {
        const auto p = at.add(0, y, 0);
        if (airOrLeaves(world, p) || (!birch && !swamp && material(world, p, "vine")) ||
            (swamp && (id(world, p) == 8 || id(world, p) == 9))) {
            set(world, p, options.log);
            if (options.vines && y > 0) {
                if (random.nextInt(3) > 0 && id(world, p.add(-1, 0, 0)) == 0)
                    set(world, p.add(-1, 0, 0), blockState(106, 8));
                if (random.nextInt(3) > 0 && id(world, p.add(1, 0, 0)) == 0)
                    set(world, p.add(1, 0, 0), blockState(106, 2));
                if (random.nextInt(3) > 0 && id(world, p.add(0, 0, -1)) == 0)
                    set(world, p.add(0, 0, -1), blockState(106, 1));
                if (random.nextInt(3) > 0 && id(world, p.add(0, 0, 1)) == 0)
                    set(world, p.add(0, 0, 1), blockState(106, 4));
            }
        }
    }
    if (options.vines || swamp)
        leafVines(world, random, at, height);
    if (options.vines && random.nextInt(5) == 0 && height > 5) {
        constexpr Position directions[] = {{0, 0, -1}, {1, 0, 0}, {0, 0, 1}, {-1, 0, 0}};
        constexpr unsigned metadata[] = {2, 3, 0, 1};
        for (int level = 0; level < 2; ++level)
            for (int side = 0; side < 4; ++side)
                if (random.nextInt(4 - level) == 0) {
                    const int age = random.nextInt(3);
                    set(world, at.add(-directions[side].x, height - 5 + level, -directions[side].z),
                        blockState(127, (age << 2) | metadata[side]));
                }
    }
    return true;
}
bool taiga(World &world, JavaRandom &random, Position at, bool pine) {
    const int height = random.nextInt(pine ? 5 : 4) + (pine ? 7 : 6);
    const int bare = pine ? height - random.nextInt(2) - 3 : 1 + random.nextInt(2), crown = height - bare;
    const int maxRadius = pine ? 1 + random.nextInt(crown + 1) : 2 + random.nextInt(2);
    if (at.y < 1 || at.y + height + 1 > 256)
        return false;
    for (int y = at.y; y <= at.y + height + 1; ++y) {
        const int radius = y - at.y < bare ? 0 : maxRadius;
        for (int x = at.x - radius; x <= at.x + radius; ++x)
            for (int z = at.z - radius; z <= at.z + radius; ++z) {
                const Position p{x, y, z};
                if (y < 0 || y >= 256 || !(pine ? replaceable(world, p) : airOrLeaves(world, p)))
                    return false;
            }
    }
    if (!soil(id(world, at.add(0, -1, 0)), !pine) || at.y >= 256 - height - 1)
        return false;
    dirt(world, at.add(0, -1, 0));
    int radius = pine ? 0 : random.nextInt(2), nextRadius = 1, restartRadius = 0;
    for (int step = 0; step <= crown; ++step) {
        const int y = at.y + height - step;
        for (int x = at.x - radius; x <= at.x + radius; ++x)
            for (int z = at.z - radius; z <= at.z + radius; ++z) {
                if (std::abs(x - at.x) != radius || std::abs(z - at.z) != radius || radius <= 0) {
                    const Position p{x, y, z};
                    if (!fullBlock(world, p))
                        set(world, p, blockState(18, 1));
                }
            }
        if (pine) {
            if (radius >= 1 && y == at.y + bare + 1)
                --radius;
            else if (radius < maxRadius)
                ++radius;
        } else if (radius >= nextRadius) {
            radius = restartRadius;
            restartRadius = 1;
            ++nextRadius;
            if (nextRadius > maxRadius)
                nextRadius = maxRadius;
        } else
            ++radius;
    }
    const int stop = pine ? height - 1 : height - random.nextInt(3);
    for (int y = 0; y < stop; ++y)
        if (airOrLeaves(world, at.add(0, y, 0)))
            set(world, at.add(0, y, 0), blockState(17, 1));
    return true;
}
constexpr Position horizontal[] = {{0, 0, -1}, {1, 0, 0}, {0, 0, 1}, {-1, 0, 0}};
bool normalSpace(const World &world, Position at, int height) {
    for (int y = 0; y <= height + 1; ++y) {
        int radius = y == 0 ? 0 : 1;
        if (y >= height - 1)
            radius = 2;
        for (int x = -radius; x <= radius; ++x)
            for (int z = -radius; z <= radius; ++z)
                if (at.y + y < 0 || at.y + y >= 256 || !replaceable(world, at.add(x, y, z)))
                    return false;
    }
    return true;
}
void leaf(World &world, Position at, BlockState state, bool airOnly = false) {
    if (airOnly ? id(world, at) == 0 : airOrLeaves(world, at))
        set(world, at, state);
}
bool acacia(World &world, JavaRandom &random, Position at) {
    const int a = random.nextInt(3), b = random.nextInt(3), height = a + b + 5;
    if (at.y < 1 || at.y + height + 1 > 256 || !normalSpace(world, at, height) ||
        !soil(id(world, at.add(0, -1, 0)), false) || at.y >= 256 - height - 1)
        return false;
    dirt(world, at.add(0, -1, 0));
    const int direction = random.nextInt(4), bend = height - random.nextInt(4) - 1;
    int reach = 3 - random.nextInt(3), x = at.x, z = at.z, lastY = 0;
    for (int y = 0; y < height; ++y) {
        if (y >= bend && reach > 0) {
            x += horizontal[direction].x;
            z += horizontal[direction].z;
            --reach;
        }
        const Position p{x, at.y + y, z};
        if (airOrLeaves(world, p)) {
            set(world, p, blockState(162));
            lastY = p.y;
        }
    }
    Position crown{x, lastY, z};
    for (int dx = -3; dx <= 3; ++dx)
        for (int dz = -3; dz <= 3; ++dz)
            if (std::abs(dx) != 3 || std::abs(dz) != 3)
                leaf(world, crown.add(dx, 0, dz), blockState(161));
    ++crown.y;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz)
            leaf(world, crown.add(dx, 0, dz), blockState(161));
    leaf(world, crown.add(2, 0, 0), blockState(161));
    leaf(world, crown.add(-2, 0, 0), blockState(161));
    leaf(world, crown.add(0, 0, 2), blockState(161));
    leaf(world, crown.add(0, 0, -2), blockState(161));
    x = at.x;
    z = at.z;
    const int other = random.nextInt(4);
    if (other != direction) {
        const int start = bend - random.nextInt(2) - 1;
        int length = 1 + random.nextInt(3);
        lastY = 0;
        for (int y = start; y < height && length > 0; --length, ++y)
            if (y >= 1) {
                x += horizontal[other].x;
                z += horizontal[other].z;
                const Position p{x, at.y + y, z};
                if (airOrLeaves(world, p)) {
                    set(world, p, blockState(162));
                    lastY = p.y;
                }
            }
        if (lastY > 0) {
            crown = {x, lastY, z};
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                    if (std::abs(dx) != 2 || std::abs(dz) != 2)
                        leaf(world, crown.add(dx, 0, dz), blockState(161));
            ++crown.y;
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    leaf(world, crown.add(dx, 0, dz), blockState(161));
        }
    }
    return true;
}
bool darkOak(World &world, JavaRandom &random, Position at) {
    const int a = random.nextInt(3), b = random.nextInt(2), height = a + b + 6;
    if (at.y < 1 || at.y + height + 1 >= 256 || !soil(id(world, at.add(0, -1, 0)), false) ||
        !normalSpace(world, at, height))
        return false;
    dirt(world, at.add(0, -1, 0));
    dirt(world, at.add(1, -1, 0));
    dirt(world, at.add(0, -1, 1));
    dirt(world, at.add(1, -1, 1));
    const int direction = random.nextInt(4), bend = height - random.nextInt(4);
    int reach = 2 - random.nextInt(3), x = at.x, z = at.z;
    const int top = at.y + height - 1;
    auto log = [&](Position p) {
        if (replaceable(world, p))
            set(world, p, blockState(162, 1));
    };
    auto leaves = [&](int dx, int y, int dz) { leaf(world, {x + dx, y, z + dz}, blockState(161, 1), true); };
    for (int y = 0; y < height; ++y) {
        if (y >= bend && reach > 0) {
            x += horizontal[direction].x;
            z += horizontal[direction].z;
            --reach;
        }
        const Position p{x, at.y + y, z};
        if (airOrLeaves(world, p)) {
            log(p);
            log(p.add(1, 0, 0));
            log(p.add(0, 0, 1));
            log(p.add(1, 0, 1));
        }
    }
    for (int dx = -2; dx <= 0; ++dx)
        for (int dz = -2; dz <= 0; ++dz) {
            leaves(dx, top - 1, dz);
            leaves(1 - dx, top - 1, dz);
            leaves(dx, top - 1, 1 - dz);
            leaves(1 - dx, top - 1, 1 - dz);
            if ((dx > -2 || dz > -1) && (dx != -1 || dz != -2)) {
                leaves(dx, top + 1, dz);
                leaves(1 - dx, top + 1, dz);
                leaves(dx, top + 1, 1 - dz);
                leaves(1 - dx, top + 1, 1 - dz);
            }
        }
    if (random.nextBoolean()) {
        leaves(0, top + 2, 0);
        leaves(1, top + 2, 0);
        leaves(1, top + 2, 1);
        leaves(0, top + 2, 1);
    }
    for (int dx = -3; dx <= 4; ++dx)
        for (int dz = -3; dz <= 4; ++dz)
            if ((dx != -3 || dz != -3) && (dx != -3 || dz != 4) && (dx != 4 || dz != -3) && (dx != 4 || dz != 4) &&
                (std::abs(dx) < 3 || std::abs(dz) < 3))
                leaves(dx, top, dz);
    for (int dx = -1; dx <= 2; ++dx)
        for (int dz = -1; dz <= 2; ++dz)
            if ((dx < 0 || dx > 1 || dz < 0 || dz > 1) && random.nextInt(3) <= 0) {
                const int length = random.nextInt(3) + 2;
                for (int y = 0; y < length; ++y)
                    log({at.x + dx, top - y - 1, at.z + dz});
                for (int sx = -1; sx <= 1; ++sx)
                    for (int sz = -1; sz <= 1; ++sz)
                        leaves(dx + sx, top, dz + sz);
                for (int sx = -2; sx <= 2; ++sx)
                    for (int sz = -2; sz <= 2; ++sz)
                        if (std::abs(sx) != 2 || std::abs(sz) != 2)
                            leaves(dx + sx, top - 1, dz + sz);
            }
    return true;
}
bool hugeSpaceAndSoil(World &world, Position at, int height) {
    if (at.y < 1 || at.y + height + 1 > 256)
        return false;
    for (int y = 0; y <= height + 1; ++y) {
        const int radius = y == 0 ? 1 : 2;
        for (int x = -radius; x <= radius; ++x)
            for (int z = -radius; z <= radius; ++z)
                if (at.y + y < 0 || at.y + y >= 256 || !replaceable(world, at.add(x, y, z)))
                    return false;
    }
    if (at.y < 2 || !soil(id(world, at.add(0, -1, 0)), false))
        return false;
    dirt(world, at.add(0, -1, 0));
    dirt(world, at.add(1, -1, 0));
    dirt(world, at.add(0, -1, 1));
    dirt(world, at.add(1, -1, 1));
    return true;
}
void hugeLeafLayer(World &world, Position at, int radius, BlockState state, bool wide) {
    const int square = radius * radius;
    for (int x = -radius; x <= radius + (wide ? 1 : 0); ++x)
        for (int z = -radius; z <= radius + (wide ? 1 : 0); ++z) {
            const int x1 = x - 1, z1 = z - 1;
            if (x * x + z * z <= square ||
                (wide && (x1 * x1 + z1 * z1 <= square || x * x + z1 * z1 <= square || x1 * x1 + z * z <= square)))
                leaf(world, at.add(x, 0, z), state);
        }
}
bool megaJungle(World &world, JavaRandom &random, Position at) {
    const int low = random.nextInt(3) + 10, height = low + random.nextInt(20);
    if (!hugeSpaceAndSoil(world, at, height))
        return false;
    for (int y = -2; y <= 0; ++y)
        hugeLeafLayer(world, at.add(0, height + y, 0), 3 - y, blockState(18, 3), true);
    for (int y = at.y + height - 2 - random.nextInt(4); y > at.y + height / 2; y -= 2 + random.nextInt(4)) {
        const float angle = random.nextFloat() * 3.1415927f * 2.0f;
        int x = 0, z = 0;
        for (int step = 0; step < 5; ++step) {
            x = at.x + int(1.5f + javaCos(angle) * float(step));
            z = at.z + int(1.5f + javaSin(angle) * float(step));
            set(world, {x, y - 3 + step / 2, z}, blockState(17, 3));
        }
        const int depth = 1 + random.nextInt(2);
        for (int sy = y - depth; sy <= y; ++sy)
            hugeLeafLayer(world, {x, sy, z}, 1 - (sy - y), blockState(18, 3), false);
    }
    auto vine = [&](Position p, unsigned meta) {
        if (random.nextInt(3) > 0 && id(world, p) == 0)
            set(world, p, blockState(106, meta));
    };
    for (int y = 0; y < height; ++y) {
        const Position p = at.add(0, y, 0);
        if (replaceable(world, p)) {
            set(world, p, blockState(17, 3));
            if (y > 0) {
                vine(p.add(-1, 0, 0), 8);
                vine(p.add(0, 0, -1), 1);
            }
        }
        if (y < height - 1) {
            const Position p1 = p.add(1, 0, 0);
            if (replaceable(world, p1)) {
                set(world, p1, blockState(17, 3));
                if (y > 0) {
                    vine(p1.add(1, 0, 0), 2);
                    vine(p1.add(0, 0, -1), 1);
                }
            }
            const Position p2 = p.add(1, 0, 1);
            if (replaceable(world, p2)) {
                set(world, p2, blockState(17, 3));
                if (y > 0) {
                    vine(p2.add(1, 0, 0), 2);
                    vine(p2.add(0, 0, 1), 4);
                }
            }
            const Position p3 = p.add(0, 0, 1);
            if (replaceable(world, p3)) {
                set(world, p3, blockState(17, 3));
                if (y > 0) {
                    vine(p3.add(-1, 0, 0), 8);
                    vine(p3.add(0, 0, 1), 4);
                }
            }
        }
    }
    return true;
}
bool megaPine(World &world, JavaRandom &random, Position at, bool spruce) {
    const int low = random.nextInt(3) + 13, height = low + random.nextInt(15);
    if (!hugeSpaceAndSoil(world, at, height))
        return false;
    const int length = random.nextInt(5) + (spruce ? 13 : 3), top = at.y + height;
    int previousRadius = 0;
    for (int y = top - length; y <= top; ++y) {
        const int fromTop = top - y, radius = floorBlock(double(float(fromTop) / float(length) * 3.5f));
        hugeLeafLayer(world, {at.x, y, at.z},
                      radius + (fromTop > 0 && radius == previousRadius && (y & 1) == 0 ? 1 : 0), blockState(18, 1),
                      true);
        previousRadius = radius;
    }
    for (int y = 0; y < height; ++y) {
        const Position p = at.add(0, y, 0);
        if (airOrLeaves(world, p))
            set(world, p, blockState(17, 1));
        if (y < height - 1) {
            for (auto offset : {Position{1, 0, 0}, Position{1, 0, 1}, Position{0, 0, 1}}) {
                const auto part = p.add(offset.x, 0, offset.z);
                if (airOrLeaves(world, part))
                    set(world, part, blockState(17, 1));
            }
        }
    }
    return true;
}
} // namespace
bool generateTree(World &world, JavaRandom &random, Position at, TreeKind kind, TreeOptions options) {
    if (kind == TreeKind::Acacia)
        return acacia(world, random, at);
    if (kind == TreeKind::DarkOak)
        return darkOak(world, random, at);
    if (kind == TreeKind::MegaJungle)
        return megaJungle(world, random, at);
    if (kind == TreeKind::MegaPine || kind == TreeKind::MegaSpruce)
        return megaPine(world, random, at, kind == TreeKind::MegaSpruce);
    if (kind == TreeKind::Pine || kind == TreeKind::Spruce)
        return taiga(world, random, at, kind == TreeKind::Pine);
    if (kind == TreeKind::Shrub) {
        while (airOrLeaves(world, at) && at.y > 0)
            --at.y;
        if (id(world, at) == 3 || id(world, at) == 2) {
            ++at.y;
            set(world, at, options.log);
            for (int y = at.y; y <= at.y + 2; ++y) {
                const int radius = 2 - (y - at.y);
                for (int x = at.x - radius; x <= at.x + radius; ++x)
                    for (int z = at.z - radius; z <= at.z + radius; ++z)
                        if (std::abs(x - at.x) != radius || std::abs(z - at.z) != radius || random.nextInt(2) != 0) {
                            const Position p{x, y, z};
                            if (!fullBlock(world, p))
                                set(world, p, options.leaves);
                        }
            }
        }
        return true;
    }
    if (kind == TreeKind::Swamp) {
        const int height = random.nextInt(4) + 5;
        while (material(world, at.add(0, -1, 0), "water"))
            --at.y;
        return broadleaf(world, random, at, height, {}, false, true);
    }
    const bool birch = kind == TreeKind::Birch || kind == TreeKind::TallBirch;
    int height = random.nextInt(3) + (birch ? 5 : options.minimumHeight);
    if (kind == TreeKind::TallBirch)
        height += random.nextInt(7);
    if (birch) {
        options.log = blockState(17, 2);
        options.leaves = blockState(18, 2);
        options.vines = false;
    }
    return broadleaf(world, random, at, height, options, birch, false);
}
void decorateTreeBase(World &world, JavaRandom &random, Position at, TreeKind kind) {
    if (kind != TreeKind::MegaPine && kind != TreeKind::MegaSpruce)
        return;
    auto patch = [&](Position p) {
        for (int x = -2; x <= 2; ++x)
            for (int z = -2; z <= 2; ++z)
                if (std::abs(x) != 2 || std::abs(z) != 2) {
                    for (int y = 2; y >= -3; --y) {
                        const auto q = p.add(x, y, z);
                        const unsigned block = id(world, q);
                        if (block == 2 || block == 3) {
                            set(world, q, blockState(3, 2));
                            break;
                        }
                        if (block != 0 && y < 0)
                            break;
                    }
                }
    };
    patch(at.add(-1, 0, -1));
    patch(at.add(2, 0, -1));
    patch(at.add(-1, 0, 2));
    patch(at.add(2, 0, 2));
    for (int i = 0; i < 5; ++i) {
        const int value = random.nextInt(64), x = value % 8, z = value / 8;
        if (x == 0 || x == 7 || z == 0 || z == 7)
            patch(at.add(-3 + x, 0, -3 + z));
    }
}
namespace {
class BigTreeOperation {
    World &world;
    JavaRandom random;
    Position base;
    BigTreeState &state;
    int trunkHeight{};
    struct Foliage {
        Position at;
        int branchY;
    };
    std::vector<Foliage> nodes;
    static Position add(Position p, double x, double y, double z) {
        return {floorBlock(double(p.x) + x), floorBlock(double(p.y) + y), floorBlock(double(p.z) + z)};
    }
    static Position step(Position p, int index, float x, float y, float z) {
        return add(p, double(.5f + float(index) * x), double(.5f + float(index) * y), double(.5f + float(index) * z));
    }
    int line(Position from, Position to, bool draw) {
        const Position delta{to.x - from.x, to.y - from.y, to.z - from.z};
        const int distance = std::max({std::abs(delta.x), std::abs(delta.y), std::abs(delta.z)});
        if (distance == 0 && !draw)
            return -1;
        const float dx = float(delta.x) / float(distance), dy = float(delta.y) / float(distance),
                    dz = float(delta.z) / float(distance);
        for (int i = 0; i <= distance; ++i) {
            const auto p = step(from, i, dx, dy, dz);
            if (draw) {
                const int ax = std::abs(p.x - from.x), az = std::abs(p.z - from.z), greatest = std::max(ax, az);
                const unsigned axis = greatest > 0 ? (ax == greatest ? 4u : 8u) : 0u;
                set(world, p, blockState(17, axis));
            } else if (!replaceable(world, p))
                return i;
        }
        return -1;
    }
    float layerSize(int level) const {
        if (float(level) < float(state.heightLimit) * .3f)
            return -1.0f;
        const float half = float(state.heightLimit) / 2.0f, delta = half - float(level);
        float radius = float(std::sqrt(double(half * half - delta * delta)));
        if (delta == 0.0f)
            radius = half;
        else if (std::abs(delta) >= half)
            return 0.0f;
        return radius * .5f;
    }
    void nodeList() {
        trunkHeight = int(double(state.heightLimit) * .618);
        if (trunkHeight >= state.heightLimit)
            trunkHeight = state.heightLimit - 1;
        int perLayer = int(1.382 + std::pow(1.0 * double(state.heightLimit) / 13.0, 2.0));
        if (perLayer < 1)
            perLayer = 1;
        const int trunkTop = base.y + trunkHeight;
        int level = state.heightLimit - state.leafDistance;
        nodes.push_back({base.add(0, level, 0), trunkTop});
        for (; level >= 0; --level) {
            const float size = layerSize(level);
            if (size >= 0.0f) {
                for (int index = 0; index < perLayer; ++index) {
                    const double radius = 1.0 * double(size) * (double(random.nextFloat()) + .328);
                    const double angle = double(random.nextFloat() * 2.0f) * 3.14159265358979323846;
                    const auto at =
                        add(base, radius * std::sin(angle) + .5, double(level - 1), radius * std::cos(angle) + .5);
                    if (line(at, at.add(0, state.leafDistance, 0), false) == -1) {
                        const int dx = base.x - at.x, dz = base.z - at.z;
                        const double branch = double(at.y) - std::sqrt(double(dx * dx + dz * dz)) * .381;
                        const int branchY = branch > double(trunkTop) ? trunkTop : int(branch);
                        if (line({base.x, branchY, base.z}, at, false) == -1)
                            nodes.push_back({at, branchY});
                    }
                }
            }
        }
    }
    void leaves() {
        for (const auto &node : nodes)
            for (int level = 0; level < state.leafDistance; ++level) {
                const float radius = level != 0 && level != state.leafDistance - 1 ? 3.0f : 2.0f;
                const int bounds = int(double(radius) + .618);
                const auto center = node.at.add(0, level, 0);
                for (int x = -bounds; x <= bounds; ++x)
                    for (int z = -bounds; z <= bounds; ++z) {
                        const double dx = double(std::abs(x)) + .5, dz = double(std::abs(z)) + .5;
                        if (dx * dx + dz * dz <= double(radius * radius)) {
                            const auto p = center.add(x, 0, z);
                            if (airOrLeaves(world, p))
                                set(world, p, blockState(18));
                        }
                    }
            }
    }

  public:
    BigTreeOperation(World &w, std::int64_t seed, Position p, BigTreeState &s)
        : world(w), random(seed), base(p), state(s) {}
    bool generate() {
        if (state.heightLimit == 0)
            state.heightLimit = 5 + random.nextInt(12);
        if (!soil(id(world, base.add(0, -1, 0)), true))
            return false;
        const int obstruction = line(base, base.add(0, state.heightLimit - 1, 0), false);
        if (obstruction != -1) {
            if (obstruction < 6)
                return false;
            state.heightLimit = obstruction;
        }
        nodeList();
        leaves();
        line(base, base.add(0, trunkHeight, 0), true);
        for (const auto &node : nodes) {
            const Position branch{base.x, node.branchY, base.z};
            if ((branch.x != node.at.x || branch.y != node.at.y || branch.z != node.at.z) &&
                double(node.branchY - base.y) >= double(state.heightLimit) * .2)
                line(branch, node.at, true);
        }
        return true;
    }
};
} // namespace
bool generateBigTree(World &world, JavaRandom &random, Position at, BigTreeState &state, bool prepareForDecoration) {
    if (prepareForDecoration)
        state.leafDistance = 5;
    return BigTreeOperation(world, random.nextLong(), at, state).generate();
}
bool generateHugeMushroom(World &world, JavaRandom &random, Position at, unsigned &selectedType) {
    if (selectedType == 0)
        selectedType = random.nextBoolean() ? 99 : 100;
    const int height = random.nextInt(3) + 4;
    if (at.y < 1 || at.y + height + 1 >= 256)
        return false;
    for (int y = at.y; y <= at.y + height + 1; ++y) {
        const int radius = y <= at.y + 3 ? 0 : 3;
        for (int x = at.x - radius; x <= at.x + radius; ++x)
            for (int z = at.z - radius; z <= at.z + radius; ++z)
                if (y < 0 || y >= 256 || !airOrLeaves(world, {x, y, z}))
                    return false;
    }
    const unsigned ground = id(world, at.add(0, -1, 0));
    if (ground != 3 && ground != 2 && ground != 110)
        return false;
    const int firstY = selectedType == 100 ? at.y + height - 3 : at.y + height;
    for (int y = firstY; y <= at.y + height; ++y) {
        const int radius = selectedType == 99 ? 3 : y < at.y + height ? 2 : 1;
        const int minX = at.x - radius, maxX = at.x + radius, minZ = at.z - radius, maxZ = at.z + radius;
        for (int x = minX; x <= maxX; ++x)
            for (int z = minZ; z <= maxZ; ++z) {
                int type = 5;
                if (x == minX)
                    --type;
                else if (x == maxX)
                    ++type;
                if (z == minZ)
                    type -= 3;
                else if (z == maxZ)
                    type += 3;
                if (selectedType == 99 || y < at.y + height) {
                    if ((x == minX || x == maxX) && (z == minZ || z == maxZ))
                        continue;
                    if (x == at.x - (radius - 1) && z == minZ)
                        type = 1;
                    if (x == minX && z == at.z - (radius - 1))
                        type = 1;
                    if (x == at.x + (radius - 1) && z == minZ)
                        type = 3;
                    if (x == maxX && z == at.z - (radius - 1))
                        type = 3;
                    if (x == at.x - (radius - 1) && z == maxZ)
                        type = 7;
                    if (x == minX && z == at.z + (radius - 1))
                        type = 7;
                    if (x == at.x + (radius - 1) && z == maxZ)
                        type = 9;
                    if (x == maxX && z == at.z + (radius - 1))
                        type = 9;
                }
                if (type == 5 && y < at.y + height)
                    type = 0;
                if (at.y >= at.y + height - 1 || type != 0) {
                    const Position p{x, y, z};
                    if (!fullBlock(world, p))
                        set(world, p, blockState(selectedType, type));
                }
            }
    }
    for (int y = 0; y < height; ++y)
        if (!fullBlock(world, at.add(0, y, 0)))
            set(world, at.add(0, y, 0), blockState(selectedType, 10));
    return true;
}
} // namespace mc::worldgen::detail
