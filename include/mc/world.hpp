#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mc {

using BlockState = std::uint16_t;
constexpr BlockState blockState(unsigned id, unsigned metadata = 0) {
    return static_cast<BlockState>((id << 4) | (metadata & 15));
}
constexpr unsigned blockId(BlockState state) { return state >> 4; }
constexpr unsigned blockMeta(BlockState state) { return state & 15; }
int floorDiv16(int value);
int floorBlock(double value);
float javaSin(float radians);
float javaCos(float radians);

struct Aabb {
    double minX{}, minY{}, minZ{}, maxX{}, maxY{}, maxZ{};
    Aabb offset(double x, double y, double z) const;
    Aabb addCoord(double x, double y, double z) const;
    Aabb expand(double x, double y, double z) const;
    bool intersects(const Aabb& other) const;
    double clipX(const Aabb& moving, double amount) const;
    double clipY(const Aabb& moving, double amount) const;
    double clipZ(const Aabb& moving, double amount) const;
};

struct ChunkSection {
    // Vanilla indexing: (localY << 8) | (localZ << 4) | localX.
    std::array<BlockState, 4096> blocks{};
    // Low nibble is the even indexed block, high nibble the odd block.
    std::array<std::uint8_t, 2048> blockLight{};
    std::array<std::uint8_t, 2048> skyLight{};
};

struct Chunk {
    int x{}, z{};
    std::uint64_t revision{};
    std::array<std::unique_ptr<ChunkSection>, 16> sections{};
    std::array<std::uint8_t, 256> biomes{}; // (localZ << 4) | localX
    std::array<std::uint16_t, 256> heightMap{}; // first sky-visible Y per column
    bool hasBiomes{}, hasSkyLight{true};
    BlockState getBlock(int localX, int y, int localZ) const;
    std::uint8_t getBlockLight(int localX, int y, int localZ) const;
    std::uint8_t getSkyLight(int localX, int y, int localZ) const;
};

struct BulkChunk {
    int x{}, z{};
    std::uint16_t primaryMask{};
};

struct WorldBorder {
    double minX{-29999984.0}, minZ{-29999984.0};
    double maxX{29999984.0}, maxZ{29999984.0};
};

struct BlockRayHit {
    bool hit{}, miss{};
    int x{}, y{}, z{}, face{-1}; // down/up/north/south/west/east = 0..5
    double hitX{}, hitY{}, hitZ{}; // world coordinates
    BlockState state{};
};

struct WaterAcceleration {
    bool loaded{}, inside{}, hasFlow{};
    double x{}, y{}, z{}; // Vanilla normalized flow multiplied by 0.014.
};

class World {
public:
    using ChunkMap = std::unordered_map<std::uint64_t, Chunk>;
    BlockState getBlock(int x, int y, int z) const;
    std::uint8_t getBlockLight(int x, int y, int z) const;
    std::uint8_t getSkyLight(int x, int y, int z) const;
    std::uint8_t getBiome(int x, int z) const;
    bool setBlock(int x, int y, int z, BlockState state);
    bool hasChunk(int chunkX, int chunkZ) const;
    const Chunk* chunk(int chunkX, int chunkZ) const;
    const ChunkMap& chunks() const { return chunks_; }
    std::uint64_t revision() const { return revision_; }
    void unloadChunk(int chunkX, int chunkZ);
    void clear();
    // Caller interpolates a changing diameter using elapsed real time. clear()
    // removes columns and preserves this setting; a newly constructed world
    // and generatePreview() use the vanilla default border.
    void setWorldBorder(double centerX, double centerZ, double diameter, int portalBoundary = 29999984);
    const WorldBorder& border() const { return border_; }

    // Input excludes the packet header. S21 requires all consumed bytes but
    // ignores bounded trailing payload exactly like Chunk.fillChunk. A full S21
    // with mask zero unloads. S26 bulk data still requires the exact byte count.
    bool applyChunk(int chunkX, int chunkZ, bool groundUp, std::uint16_t primaryMask,
                    bool skyLight, const std::uint8_t* data, std::size_t size,
                    std::string* error = nullptr);
    bool applyBulk(const std::vector<BulkChunk>& columns, bool skyLight,
                   const std::uint8_t* data, std::size_t size,
                   std::string* error = nullptr);
    static std::size_t chunkDataSize(std::uint16_t primaryMask, bool skyLight, bool groundUp);

    std::vector<Aabb> collisions(const Aabb& bounds, bool borderCollision = false) const;
    void blockCollisionBoxes(int x, int y, int z, std::vector<Aabb>& out) const;
    bool isFullCube(int x, int y, int z) const;
    // World.handleMaterialAcceleration for water, using the exact caller's AABB.
    // Apply x/y/z only when hasFlow and the entity's isPushedByWater rule permit it. This
    // query neither changes the world nor shrinks a player-specific bounding box.
    WaterAcceleration waterAcceleration(const Aabb& query) const;
    Aabb selectionBounds(int x, int y, int z) const;
    BlockRayHit raycast(double startX, double startY, double startZ,
                        double endX, double endY, double endZ,
                        bool stopOnLiquid = false, bool ignoreBlocksWithoutBoundingBox = false,
                        bool returnLastMiss = false) const;

    // An offline rendering/physics fixture; this is NOT Minecraft world generation.
    void generatePreview(int radiusChunks = 4, std::uint64_t seed = 189);

private:
    ChunkMap chunks_;
    std::uint64_t revision_{};
    WorldBorder border_{};
    static std::uint64_t key(int x, int z);
    Chunk& ensureChunk(int x, int z);
    void touch(int chunkX, int chunkZ);
    void decodeChunk(int chunkX, int chunkZ, bool groundUp, std::uint16_t mask,
                     bool skyLight, const std::uint8_t* data);
    void updateHeight(Chunk& chunk, int localX, int localZ);
    void relightAfterChange(int x,int y,int z,int oldHeight,BlockState previous);
    void updateLightAt(int x,int y,int z,bool sky);
    bool setLightValue(int x,int y,int z,bool sky,int value);
    Aabb rayBoundsLocal(int x, int y, int z) const;
};

struct Input {
    // Java coordinates: positive forward is +Z at yaw 0, positive strafe is left.
    float forward{}, strafe{};
    bool jump{}, sneak{}, sprint{};
    bool usingItem{};
};

enum PhysicsWarning : std::uint32_t {
    PhysicsCompleteForSupportedBlocks = 0,
    PhysicsMissingChunks = 1u << 0,
    PhysicsEntityInteractionsUnavailable = 1u << 1,
    PhysicsMovingPiston = 1u << 2,
    PhysicsUnknownBlock = 1u << 3,
    PhysicsFluidApproximation = 1u << 4
};

class Player {
public:
    Player();
    double x{0.5}, y{70.0}, z{0.5}; // feet position
    double previousX{0.5}, previousY{70.0}, previousZ{0.5};
    double motionX{}, motionY{}, motionZ{};
    float yaw{}, pitch{}; // degrees; yaw 0 points toward +Z
    bool onGround{}, collidedHorizontally{}, collidedVertically{};
    bool jumpedLastTick{}; // Ground-jump event for statistics/exhaustion; observational only.
    bool outsideBorder{};
    bool sprinting{}, sneaking{}, inWater{}, inLava{}, inWeb{};
    bool allowFlying{}, flying{}, spectator{}, noClip{};
    bool blind{}, dead{};
    int foodLevel{20};
    // Effective movement attribute before the local sprint modifier. Server
    // attribute modifiers other than sprint must already be applied by caller.
    double movementSpeed{0.10000000149011612};
    float flySpeed{0.05f}, speedInAir{0.02f}, stepHeight{0.6f};
    int jumpBoostAmplifier{-1}, depthStriderLevel{};
    float fallDistance{};
    std::uint32_t warnings{};
    std::uint64_t ticks{};
    // Ordered C13 transitions caused by local input this tick, including a
    // flight toggle followed by ground cancellation within the same tick.
    std::vector<bool> flightChangesLastTick;

    void teleport(double feetX, double feetY, double feetZ);
    const Aabb& bounds() const { return bounds_; }
    double eyeY() const { return y + static_cast<double>(sneaking ? 1.54f : 1.62f); }
    // Exactly one vanilla game tick (50 ms), independent of rendering frequency.
    void tick(const World& world, const Input& input);
    // Raw Entity.moveEntity port; exposed for differential fixtures.
    void move(const World& world, double dx, double dy, double dz);
    void setSprinting(bool value);

private:
    Aabb bounds_{};
    int jumpTicks_{}, sprintToggleTimer_{}, sprintTicksLeft_{}, flyToggleTimer_{};
    float previousForward_{}, jumpMovementFactor_{0.02f};
    bool previousJump_{}, previousSneak_{};
    void moveFlying(float strafe, float forward, float acceleration);
    std::vector<Aabb> collisionBoxes(const World& world, const Aabb& query);
    void jump();
    bool onLadder(const World& world) const;
    void pushOutOfBlocks(const World& world, double atX, double atY, double atZ);
    void updateWater(const World& world);
    void updateEnvironment(const World& world);
    void travel(const World& world, float strafe, float forward);
};

} // namespace mc
