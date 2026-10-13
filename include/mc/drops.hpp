#pragma once

#include "mc/game.hpp"
#include "mc/worldgen_noise.hpp"

#include <vector>

namespace mc::drops {

using Random = worldgen::JavaRandom;

struct ItemDrop {
    ItemStack item;
    double x{},y{},z{}; // Offsets within the destroyed block, before entity motion.
};

struct Result {
    bool supported{};
    float exhaustion{};
    BlockState replacement{};
    std::vector<ItemDrop> items;
    std::vector<int> experience; // Vanilla EntityXPOrb.getXPSplit order.
};

struct Context {
    BlockState state{};
    bool tileDrops{true};
    bool waterVaporizes{};
    bool supportedBelow{}; // Below material blocks movement or is liquid (ice).
};

struct ToolDamage {
    int amount{};
    bool broken{};
};

// These kernels invoke no world mutation. The caller captures harvest eligibility
// before damageOnBreak(), then passes the resulting held stack to harvest().
// World and player random streams are separate, just as in the Java server.
bool knownState(BlockState state);
bool hasDropRule(BlockState state);
bool canHarvest(BlockState state,const ItemStack& held);
ToolDamage damageOnBreak(BlockState state,ItemStack& held,bool creative,
                         Random& worldRandom,Random& playerRandom);
Result harvest(const Context& context,const ItemStack& held,Random& worldRandom);
Result dropWithChance(const Context& context,int fortune,float chance,Random& worldRandom);
int experienceSplit(int amount);

} // namespace mc::drops
