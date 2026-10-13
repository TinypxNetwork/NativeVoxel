#include "mc/worldgen_decoration.hpp"
#include "worldgen_fixture.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

using namespace mc;
using namespace mc::worldgen;
using namespace mc::worldgen::test;
namespace {
void halo(World &world, int pattern, int cx, int cz) {
    std::vector<std::uint8_t> wire;
    std::vector<BulkChunk> columns{{cx - 1, cz, 65535}, {cx, cz - 1, 65535}};
    for (const auto &column : columns) {
        for (int section = 0; section < 16; ++section)
            for (int i = 0; i < 4096; ++i) {
                const auto state = base(pattern, 63, column.x * 16 + (i & 15), section * 16 + (i >> 8),
                                        column.z * 16 + ((i >> 4) & 15));
                wire.push_back(static_cast<std::uint8_t>(state));
                wire.push_back(static_cast<std::uint8_t>(state >> 8));
            }
        wire.insert(wire.end(), 16 * 2048, 0);
        wire.insert(wire.end(), 256, 1);
    }
    std::string error;
    if (!world.applyBulk(columns, false, wire.data(), wire.size(), &error))
        throw std::runtime_error(error);
}
void changes(std::ostream &out, const World &world, int pattern, int cx, int cz) {
    for (int x = cx * 16; x < (cx + 2) * 16; ++x)
        for (int z = cz * 16; z < (cz + 2) * 16; ++z)
            for (int y = 0; y < 256; ++y) {
                const auto value = world.getBlock(x, y, z);
                if (value != base(pattern, 63, x, y, z))
                    out << "B " << x << ' ' << y << ' ' << z << ' ' << value << '\n';
            }
    if (world.chunks().size() != 6)
        throw std::runtime_error("Nether feature exceeded supplied columns and read halo");
    for (const auto coordinate : {std::pair<int, int>{cx - 1, cz}, {cx, cz - 1}})
        for (int x = coordinate.first * 16; x < (coordinate.first + 1) * 16; ++x)
            for (int z = coordinate.second * 16; z < (coordinate.second + 1) * 16; ++z)
                for (int y = 0; y < 256; ++y)
                    if (world.getBlock(x, y, z) != base(pattern, 63, x, y, z))
                        throw std::runtime_error("Nether population modified its read-only halo");
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Nether population test requires fixture/output paths");
        std::ifstream in(argv[1]);
        std::ofstream out(argv[2]);
        if (!in || !out)
            throw std::runtime_error("cannot open Nether fixture/output");
        std::string line, error;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#')
                continue;
            std::istringstream row(line);
            std::int64_t seed;
            int pattern, cx, cz, light;
            row >> seed >> pattern >> cx >> cz >> light;
            const int x = cx * 16, z = cz * 16;
            World world;
            load(world, pattern, 63, x + 16, z + 16);
            JavaRandom random(seed);
            struct Tick {
                int x, y, z;
                BlockState state;
                std::uint64_t seed;
            };
            std::vector<Tick> ticks;
            DecorationHooks hooks;
            hooks.light = [&](int x, int, int z) { return light - ((x + z) & 1); };
            hooks.forceBlockTick = [&](int x, int y, int z, BlockState state, JavaRandom &r) {
                ticks.push_back({x, y, z, state, r.internalSeed()});
            };
            const auto revision = world.revision(), randomSeed = random.internalSeed();
            if (populateNetherFeatures(world, cx, cz, random, hooks, &error) || world.revision() != revision ||
                random.internalSeed() != randomSeed)
                throw std::runtime_error("Missing query neighbors must fail before population changes state");
            halo(world, pattern, cx, cz);
            if (!populateNetherFeatures(world, cx, cz, random, hooks, &error))
                throw std::runtime_error(error);
            out << "R " << random.nextLong() << '\n';
            for (const auto &tick : ticks)
                out << "F " << tick.x << ' ' << tick.y << ' ' << tick.z << ' ' << tick.state << ' ' << tick.seed
                    << '\n';
            changes(out, world, pattern, cx, cz);
            out << "END\n";
        }
        if (!out)
            throw std::runtime_error("failed writing Nether population result");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
