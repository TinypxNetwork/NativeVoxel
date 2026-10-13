#pragma once

#include "mc/game.hpp"
#include "mc/nbt.hpp"
#include "mc/brewing.hpp"
#include "mc/drop_entities.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <string>

namespace mc {

struct SimulationSettings {
    int difficulty{2}; // 0 peaceful, 1 easy, 2 normal, 3 hard.
    int gameMode{};
    bool naturalRegeneration{true};
    bool daylightCycle{true};
    bool weatherCycle{true};
    bool tileDrops{true};
    std::string playerName{"NativeCpp"};
};

struct FoodState {
    float exhaustion{};
    int timer{};
    int previousFoodLevel{20};
};

struct TimeWeatherState {
    std::int64_t totalTime{}, dayTime{};
    int cleanWeatherTime{}, rainTime{}, thunderTime{};
    bool raining{}, thundering{};
    float previousRainStrength{}, rainStrength{};
    float previousThunderStrength{}, thunderStrength{};
};

struct FurnaceState {
    std::array<ItemStack, 3> slots; // input, fuel, result; same order as ContainerFurnace.
    int burnTime{}, currentItemBurnTime{}, cookTime{}, totalCookTime{};
    bool dirty{};
};

enum class SimulationEventKind {
    BlockChanged,
    ItemDropped,
    ExperienceDropped,
    PlayerDamaged,
    PlayerHealed,
    ItemCollected
};
struct SimulationEvent {
    SimulationEventKind kind{SimulationEventKind::BlockChanged};
    int x{}, y{}, z{};
    BlockState before{}, after{};
    ItemStack item;
    float amount{};
    int entityId{};
};

struct SimulationCoverage {
    bool food{}, time{}, weather{}, furnace{}, brewing{};
    bool blockDrops{}, scheduledBlocks{}, fluids{}, fallingBlocks{}, redstone{};
    bool simpleBlockDrops{}, itemEntities{}, experience{};
};

// Native singleplayer authority. It reuses Game only as the world/player/UI
// state carrier and never creates a network connection. Online Game instances
// are rejected. tick() owns the entire 50 ms step, including one Game::tick;
// callers must not also tick the same Game for that step.
class Simulation {
  public:
    SimulationSettings settings;
    FoodState food;
    TimeWeatherState weather;
    std::deque<SimulationEvent> events;
    std::string lastInteractionError;
    std::array<ItemStack, 27> enderItems;

    void start(Game &game, std::uint64_t seed = 189, bool resetState = true);
    void tick(Game &game, const Input &input);
    bool running() const { return running_; }
    std::uint64_t ticks() const { return ticks_; }
    SimulationCoverage coverage() const;

    // Exact FoodStats operations are independently testable. Health, food and
    // saturation live in Game; FoodState retains the server's internal counters.
    void addExhaustion(float amount);
    void tickFood(Game &game);
    void tickTimeWeather(Game &game);

    // Pure server furnace machine, independent of rendering or packet framing.
    static void tickFurnace(FurnaceState &furnace);
    FurnaceState &furnaceAt(int x, int y, int z);
    const std::map<std::uint64_t, FurnaceState> &furnaces() const { return furnaces_; }
    static void tickBrewing(BrewingState &stand) { brewing::tick(stand); }
    BrewingState &brewingAt(int x, int y, int z);
    const std::map<std::uint64_t, BrewingState> &brewingStands() const { return brewingStands_; }

    // The UI bridge must commit before closing/changing a local container.
    // interactBlock returns false for a block not supported by this stage.
    bool interactBlock(Game &game, int x, int y, int z);
    void commitContainer(Game &game);
    void beforeContainerClose(Game &game);
    // Import newly loaded Anvil machines without replacing authoritative state
    // already restored from the native sidecar. Storage inventory stays in NBT.
    void importTileEntity(const Game &game, const TileEntityData &tile);
    void publishTileEntities(Game &game) const;
    // Authoritative offline break. A false result rejects the operation without
    // modifying terrain/tool/RNG. Complex tile/paired callbacks are not guessed.
    bool breakBlock(Game &game, int x, int y, int z, int face);
    int spawnItem(Game &game, const ItemStack &stack, double x, double y, double z, int pickupDelay = 10);
    int spawnExperience(Game &game, int amount, double x, double y, double z);
    int dropFromPlayer(Game &game, const ItemStack &stack, bool trace = false, bool around = false);
    bool dropFromPlayerBatch(Game &game, const std::vector<ItemStack> &stacks, bool trace = false);
    bool dropHeldItem(Game &game, bool all = false);
    bool creativeDrop(Game &game, const ItemStack &stack);
    int useProjectile(Game &game,const ItemStack &stack,float charge=1);
    int creativeDropThreshold() const { return creativeDropThreshold_; }
    const std::map<int, drop_entities::State> &droppedEntities() const { return droppedEntities_; }

    // Native simulation state accompanies the terrain save; no original save
    // file is changed implicitly. The tag also permits integration into level.dat.
    nbt::Tag saveState(const Game &game) const;
    void loadState(Game &game, const nbt::Tag &state);
    bool saveStateFile(const std::string &path, const Game &game, std::string *error = nullptr) const;
    bool loadStateFile(const std::string &path, Game &game, std::string *error = nullptr);

  private:
    bool running_{};
    std::uint64_t ticks_{}, seed_{189}, randomState_{};
    std::uint64_t playerRandomState_{}, mathRandomState_{}, entitySeedState_{};
    int nextEntityId_{1000000}, xpCooldown_{};
    int creativeDropThreshold_{}; // Connection-scoped C10 limiter; not a world-save field.
    std::map<int, drop_entities::State> droppedEntities_;
    std::map<std::uint64_t, FurnaceState> furnaces_;
    std::map<std::uint64_t, BrewingState> brewingStands_;
    std::uint64_t openFurnace_{};
    std::uint64_t openBrewing_{};
    bool furnaceOpen_{};
    bool brewingOpen_{};
    struct StoragePart {
        int x{}, y{}, z{}, blockId{}, slots{};
    };
    std::vector<StoragePart> openStorage_;
    std::vector<ItemStack> storageSnapshot_;
    bool enderOpen_{};
    int nextWindowId_{1};
    int nextInt(int bound);
    static std::uint64_t key(int x, int y, int z);
    void syncGame(Game &game);
    void tickDroppedEntities(Game &game);
    void publishDroppedEntity(Game &game, const drop_entities::State &state) const;
    void publishDroppedEntities(Game &game) const;
    bool prepareCreativeItem(Game &game, ItemStack &stack);
    bool interactStorage(Game &game, int x, int y, int z);
    void commitStorage(Game &game);
    void closeStorage(Game &game);
};

} // namespace mc
