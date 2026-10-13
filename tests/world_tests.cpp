#include "mc/world.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance, const char* message) {
    if (std::abs(actual - expected) > tolerance) {
        std::cerr << std::setprecision(17) << "expected " << expected << ", got " << actual << '\n';
        throw std::runtime_error(message);
    }
}
void emptyChunk(mc::World& world, int x, int z) {
    const std::vector<std::uint8_t> bytes(256, 1);
    check(world.applyBulk({{x, z, 0}}, true, bytes.data(), bytes.size()), "load empty column");
}
void ground(mc::World& world, int minX, int maxX, int minZ, int maxZ, unsigned block = 1, int y = 63) {
    for (int x = minX; x <= maxX; ++x) for (int z = minZ; z <= maxZ; ++z) world.setBlock(x, y, z, mc::blockState(block));
}

void storageTests() {
    mc::World world;
    check(mc::floorDiv16(-1) == -1 && mc::floorDiv16(-16) == -1 && mc::floorDiv16(-17) == -2, "negative chunk coordinates");
    check(mc::floorBlock(-.25) == -1 && mc::floorBlock(-1) == -1, "Java block floor");
    world.setBlock(-1, 255, -17, mc::blockState(197, 12));
    check(world.getBlock(-1, 255, -17) == mc::blockState(197, 12), "negative chunk / top section lookup");
    check(!world.setBlock(0, -1, 0, 16) && !world.setBlock(0, 256, 0, 16), "vertical bounds");
    check(world.chunk(-1, -2)->sections[15]->blocks[4095] == mc::blockState(197, 12), "section memory layout");
    const auto revision = world.revision();
    check(!world.setBlock(-1, 255, -17, mc::blockState(197, 12)) && world.revision() == revision, "no-op block mutation");

    constexpr std::uint16_t mask = 0x8005; // deliberately separated bottom and top sections
    std::vector<std::uint8_t> data(mc::World::chunkDataSize(mask, true, true));
    std::size_t offset = 0;
    for (int section : {0, 2, 15}) for (unsigned i = 0; i < 4096; ++i) {
        const std::uint16_t state = static_cast<std::uint16_t>((section * 0x123 + i) & 0xffff);
        data[offset++] = static_cast<std::uint8_t>(state);
        data[offset++] = static_cast<std::uint8_t>(state >> 8);
    }
    for (int section : {0, 2, 15}) for (unsigned i = 0; i < 2048; ++i) data[offset++] = static_cast<std::uint8_t>(section + i);
    for (int section : {0, 2, 15}) for (unsigned i = 0; i < 2048; ++i) data[offset++] = static_cast<std::uint8_t>(0xf0 + section - i);
    for (unsigned i = 0; i < 256; ++i) data[offset++] = static_cast<std::uint8_t>(i);
    std::string error;
    check(world.applyChunk(-3, 2, true, mask, true, data.data(), data.size(), &error), "section decode");
    const auto c = world.chunk(-3, 2);
    check(c && c->hasBiomes && c->hasSkyLight && !c->sections[1], "chunk section sparsity");
    for (int section : {0, 2, 15}) for (unsigned i = 0; i < 4096; ++i) {
        check(c->sections[section]->blocks[i] == static_cast<std::uint16_t>(section * 0x123 + i), "LE state and section ordering");
        const int lx = i & 15, ly = (i >> 8) + section * 16, lz = (i >> 4) & 15;
        const unsigned shift = (i & 1) * 4;
        check(c->getBlockLight(lx, ly, lz) == ((static_cast<std::uint8_t>(section + i / 2) >> shift) & 15), "block-light nibble order");
        check(c->getSkyLight(lx, ly, lz) == ((static_cast<std::uint8_t>(0xf0 + section - i / 2) >> shift) & 15), "sky-light nibble order");
    }
    check(world.getBiome(-48 + 5, 32 + 9) == 149, "biome indexing");
    const auto beforeFailure = world.revision();
    check(!world.applyChunk(-3, 2, true, mask, true, data.data(), data.size() - 1, &error), "reject truncated sections");
    check(world.revision() == beforeFailure && !error.empty(), "malformed payload cannot mutate world");
    {
        const auto minimum = mc::World::chunkDataSize(15, false, true);
        std::vector<std::uint8_t> proxy(minimum + 8192, 0);
        std::fill(proxy.begin() + 40960, proxy.begin() + 40960 + 256, 0x7a);
        std::fill(proxy.end() - 256, proxy.end(), 0x03);
        check(world.applyChunk(10, 1, true, 15, false, proxy.data(), proxy.size()), "vanilla S21 ignores trailing arrays");
        check(world.getBiome(160, 16) == 0x7a && !world.chunk(10, 1)->hasSkyLight, "trailing sky arrays must not alter vanilla End biome offset");
    }

    std::vector<std::uint8_t> partial(mc::World::chunkDataSize(2, false, false));
    partial[0] = 0xbc; partial[1] = 0x0a;
    check(world.applyChunk(-3, 2, false, 2, false, partial.data(), partial.size()), "no-sky partial section");
    check(world.getBlock(-48, 16, 32) == 0x0abc && c->sections[0] && c->sections[15] && c->hasBiomes, "partial preserves absent sections / biomes");
    check(world.getSkyLight(-48, 16, 32) == 0, "dimension without skylight");
    std::vector<std::uint8_t> replacement(mc::World::chunkDataSize(1, true, true));
    check(world.applyChunk(-3, 2, true, 1, true, replacement.data(), replacement.size()), "full replacement");
    check(!c->sections[1] && !c->sections[2] && !c->sections[15], "full replacement discards unlisted sections");
    emptyChunk(world, -2, 2);
    const auto neighborRevision = world.chunk(-2, 2)->revision;
    world.setBlock(-33, 64, 32, mc::blockState(1));
    check(world.chunk(-2, 2)->revision > neighborRevision, "neighbor mesh invalidation");
    check(world.applyChunk(-3, 2, true, 0, true, nullptr, 0), "S21 unload");
    check(!world.hasChunk(-3, 2), "unload removes column");
    emptyChunk(world, -3, 2);
    check(world.hasChunk(-3, 2), "S26 zero mask creates loaded air column");

    std::vector<std::uint8_t> bulk(512, 1);
    const auto bulkBefore = world.revision();
    check(!world.applyBulk({{1, 1, 0}, {2, 2, 1}}, true, bulk.data(), bulk.size()), "reject malformed final bulk column");
    check(world.revision() == bulkBefore && !world.hasChunk(1, 1), "bulk prevalidation is atomic");
    check(world.applyBulk({{1, 1, 0}, {2, 2, 0}}, true, bulk.data(), bulk.size()), "multi-column bulk");
    world.clear();
    check(world.chunks().empty(), "clear world");
}

void physicsTests() {
    const mc::Aabb block{0, 0, 0, 1, 1, 1}, mover{-1, 0, .1, -.1, .9, .9};
    near(block.clipX(mover, .5), .1, 0, "axis clipping");
    check(!block.intersects({1, 0, 0, 2, 1, 1}), "touching boxes do not intersect");
    mc::World world;
    ground(world, -32, 32, -32, 96);
    mc::Player player;
    player.teleport(.5, 64, .5); player.onGround = true;
    player.tick(world, mc::Input{});
    near(player.y, 64, 0, "standing height");
    near(player.motionY, -.0784000015258789, 1e-16, "gravity and vertical drag");
    mc::Input walk; walk.forward = 1;
    for (int i = 0; i < 40; ++i) player.tick(world, walk);
    check(player.z > 8.6 && player.z < 9.1 && player.onGround, "vanilla walking speed range");
    const double startY = player.y;
    mc::Input jump; jump.jump = true;
    player.tick(world, jump);
    near(player.y, startY + static_cast<double>(.42f), 1e-14, "float jump impulse");
    double peak = player.y;
    for (int i = 0; i < 20; ++i) { player.tick(world, {}); peak = std::max(peak, player.y); }
    check(peak - startY > 1.24 && peak - startY < 1.26 && player.onGround, "jump arc and landing");
    player.teleport(.5, 64, .5); player.motionX = player.motionY = player.motionZ = 0; player.onGround = true;
    world.setBlock(0, 64, 2, mc::blockState(44));
    for (int i = 0; i < 10; ++i) player.tick(world, walk);
    near(player.y, 64.5, 1e-12, "half-slab automatic stepping");
    world.setBlock(0, 64, 3, mc::blockState(1));
    for (int i = 0; i < 10; ++i) player.tick(world, walk);
    check(player.y >= 65, "step from slab to cube");
    player.teleport(.5, 64, .5); player.motionX = player.motionY = player.motionZ = 0; player.onGround = true;
    world.setBlock(0, 64, 2, mc::blockState(85));
    for (int i = 0; i < 20; ++i) player.tick(world, walk);
    check(player.z < 2.08 && player.y == 64, "fence is 1.5 blocks high");

    mc::World ledge;
    ground(ledge, 0, 0, 0, 0);
    mc::Player sneak;
    sneak.teleport(.5, 64, .5); sneak.onGround = true;
    walk.sneak = true;
    for (int i = 0; i < 50; ++i) sneak.tick(ledge, walk);
    check(sneak.z > 1.2 && sneak.z < 1.31 && sneak.y == 64, "sneak ledge guard retains support");
    mc::Player absent;
    mc::World empty;
    absent.tick(empty, walk);
    check(absent.ticks == 0 && absent.y == 70 && (absent.warnings & mc::PhysicsMissingChunks), "missing chunk freezes simulation");

    world.setBlock(0, 64, 0, mc::blockState(60));
    std::vector<mc::Aabb> shapes;
    world.blockCollisionBoxes(0, 64, 0, shapes);
    near(shapes.front().maxY, 65, 0, "1.8 farmland collision is a full block");
    shapes.clear(); world.setBlock(0, 64, 0, mc::blockState(78, 0)); world.blockCollisionBoxes(0, 64, 0, shapes);
    near(shapes.front().maxY, 64, 0, "single snow layer has zero collision height");
}

void trace(const std::string& path) {
    std::ifstream input(path);
    check(input.good(), "cannot open differential fixture");
    mc::World world;
    mc::Player player;
    std::string line;
    unsigned tick = 0;
    std::cout << std::setprecision(17);
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream command(line);
        char kind; command >> kind;
        if (kind == 'W') { world.clear(); world.setWorldBorder(0, 0, 60000000); player = mc::Player{}; }
        else if (kind == 'B') {
            int x, y, z, state; command >> x >> y >> z >> state; world.setBlock(x, y, z, static_cast<mc::BlockState>(state));
        } else if (kind == 'C') {
            int x, z; command >> x >> z; emptyChunk(world, x, z);
        } else if (kind == 'P') {
            double x, y, z; int ground; player = mc::Player{};
            command >> x >> y >> z >> player.yaw >> player.pitch >> ground;
            player.teleport(x, y, z); player.onGround = ground != 0;
        } else if (kind == 'V') command >> player.motionX >> player.motionY >> player.motionZ;
        else if (kind == 'A') command >> player.movementSpeed >> player.foodLevel >> player.allowFlying >> player.flying >> player.flySpeed >> player.spectator;
        else if (kind == 'J') command >> player.jumpBoostAmplifier;
        else if (kind == 'Y') command >> player.yaw >> player.pitch;
        else if (kind == 'O') {
            double cx, cz, diameter; int boundary; command >> cx >> cz >> diameter >> boundary;
            world.setWorldBorder(cx, cz, diameter, boundary);
        }
        else if (kind == 'G') {
            int x, y, z; command >> x >> y >> z;
            std::vector<mc::Aabb> boxes; world.blockCollisionBoxes(x, y, z, boxes);
            // Vanilla addCollisionBoxesToList discards zero-volume snow boxes.
            const mc::Aabb probe{x - 1.0, y - 1.0, z - 1.0, x + 2.0, y + 3.0, z + 2.0};
            std::cout << "G";
            for (const auto& b : boxes) if (b.intersects(probe))
                std::cout << ' ' << b.minX << ' ' << b.minY << ' ' << b.minZ << ' ' << b.maxX << ' ' << b.maxY << ' ' << b.maxZ;
            std::cout << '\n';
        } else if (kind == 'S') {
            float angle; command >> angle; std::cout << "S " << mc::javaSin(angle) << ' ' << mc::javaCos(angle) << '\n';
        } else if (kind == 'T' || kind == 'M') {
            if (kind == 'T') {
                mc::Input in; command >> in.forward >> in.strafe >> in.jump >> in.sneak >> in.sprint >> in.usingItem;
                player.tick(world, in);
            } else {
                double dx, dy, dz; command >> dx >> dy >> dz; player.move(world, dx, dy, dz);
            }
            std::cout << "D " << tick++ << ' ' << player.x << ' ' << player.y << ' ' << player.z << ' '
                      << player.motionX << ' ' << player.motionY << ' ' << player.motionZ << ' '
                      << player.onGround << ' ' << player.collidedHorizontally << ' ' << player.collidedVertically << ' '
                      << player.sprinting << ' ' << player.sneaking << ' ' << player.flying << ' ' << player.fallDistance << ' '
                      << player.inWater << ' ' << player.inLava << ' ' << player.inWeb << ' ' << player.outsideBorder << '\n';
        } else throw std::runtime_error("unknown fixture command: " + line);
    }
}

void chunkPacket(int dimension, const std::string& source, const std::string& destination) {
    std::ifstream file(source, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    check(bytes.size() >= 12, "truncated S21 fixture");
    const auto be32 = [&](std::size_t p) {
        return (static_cast<std::uint32_t>(bytes[p]) << 24) | (static_cast<std::uint32_t>(bytes[p + 1]) << 16) |
               (static_cast<std::uint32_t>(bytes[p + 2]) << 8) | bytes[p + 3];
    };
    const int x = static_cast<std::int32_t>(be32(0)), z = static_cast<std::int32_t>(be32(4));
    const bool full = bytes[8] != 0;
    const auto mask = static_cast<std::uint16_t>((static_cast<unsigned>(bytes[9]) << 8) | bytes[10]);
    std::size_t offset = 11;
    std::uint32_t size = 0;
    for (unsigned i = 0; i < 5; ++i) {
        check(offset < bytes.size(), "truncated S21 data size");
        const auto v = bytes[offset++]; size |= static_cast<std::uint32_t>(v & 127) << (7 * i);
        if (!(v & 128)) break;
        check(i != 4, "oversized S21 data size");
    }
    check(size == bytes.size() - offset, "S21 declared byte-array size");
    mc::World world;
    std::string error;
    const bool applied = world.applyChunk(x, z, full, mask, dimension == 0, bytes.data() + offset, size, &error);
    check(applied, error.c_str());
    const auto c = world.chunk(x, z);
    check(c != nullptr, "S21 sample must contain a real column");
    std::ofstream out(destination, std::ios::binary);
    for (unsigned i = 0; i < 16; ++i) if (mask & (1u << i)) for (const auto state : c->sections[i]->blocks) {
        out.put(static_cast<char>(state & 255)); out.put(static_cast<char>(state >> 8));
    }
    for (unsigned i = 0; i < 16; ++i) if (mask & (1u << i)) out.write(reinterpret_cast<const char*>(c->sections[i]->blockLight.data()), 2048);
    if (dimension == 0) for (unsigned i = 0; i < 16; ++i) if (mask & (1u << i)) out.write(reinterpret_cast<const char*>(c->sections[i]->skyLight.data()), 2048);
    if (full) out.write(reinterpret_cast<const char*>(c->biomes.data()), 256);
    check(out.good(), "write normalized native S21");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--trace") { trace(argv[2]); return 0; }
        if (argc == 5 && std::string(argv[1]) == "--chunk47") { chunkPacket(std::stoi(argv[2]), argv[3], argv[4]); return 0; }
        storageTests(); physicsTests();
        std::cout << "world/physics tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "world/physics test failed: " << error.what() << '\n';
        return 1;
    }
}
