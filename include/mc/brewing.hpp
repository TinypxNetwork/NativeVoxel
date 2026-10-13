#pragma once
#include "mc/game.hpp"
#include <array>

namespace mc {
struct BrewingState {
    std::array<ItemStack,4> slots;
    int brewTime{},ingredientId{-1},filledMask{-1};
    bool dirty{};
};
namespace brewing {
int result(int metadata,const ItemStack& ingredient);
bool changes(int before,int after);
bool canBrew(const BrewingState& state);
void tick(BrewingState& state);
}
}
