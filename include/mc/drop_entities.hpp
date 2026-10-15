#pragma once

#include "mc/game.hpp"
#include "mc/nbt.hpp"
#include "mc/worldgen_noise.hpp"

#include <string>

namespace mc::drop_entities {

enum Warning : std::uint32_t {
    MissingChunks=1u<<0,
    PortalTransferUnavailable=1u<<1,
    BlockTriggerUnavailable=1u<<2,
    MovingPistonUnavailable=1u<<3
};

// Item/XP authority is distinct from interpolated Game::Entity render state.
// Construction motion uses an independent Java-style Math.random stream, never
// the world's random stream. Oracle tests start from explicit entity state.
struct State {
    int id{};
    bool orb{};
    ItemStack item;
    int value{},age{},pickupDelay{},health{5},fire{},ticks{},color{},targetColor{};
    bool dead{},onGround{},noClip{},inWeb{},inWater{},firstUpdate{true},outsideBorder{};
    bool hasTarget{},searchForMerge{};
    double x{},y{},z{},previousX{},previousY{},previousZ{},motionX{},motionY{},motionZ{};
    float width{.25f},height{.25f},yaw{},hoverStart{},fallDistance{};
    std::string owner,thrower;
    nbt::Tag anvilData; // Original entity NBT, retaining UUIDs and unknown fields.
    std::uint32_t warnings{};
    Aabb bounds;
    worldgen::JavaRandom random{0};

    void setPosition(double x,double y,double z);
};

State item(int id,const ItemStack& stack,double x,double y,double z,
           worldgen::JavaRandom& mathRandom,std::int64_t entitySeed);
State experience(int id,int amount,double x,double y,double z,
                 worldgen::JavaRandom& mathRandom,std::int64_t entitySeed);
void throwFromPlayer(State& state,const Player& player,bool around,bool trace,
                     const std::string& playerName,worldgen::JavaRandom& playerRandom);
void tick(State& state,const World& world,const Player* attractPlayer=nullptr,bool wetFromRain=false,
          const std::function<void(State&)>& searchForMerge={});
bool combine(State& self,State& other);

// Returns vanilla's boolean, including its partial-insertion behavior. Residual
// stack count may shrink even if false is returned when all remaining slots fill.
bool insertInventory(std::array<ItemStack,45>& inventory,ItemStack& stack,bool creative);
bool collect(State& state,Game& game,int& xpCooldown,const std::string& playerName={});
int experienceBarCap(int level);
void addExperience(Game& game,int amount);

} // namespace mc::drop_entities
