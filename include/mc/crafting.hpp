#pragma once

#include "mc/game.hpp"

#include <cstddef>
#include <vector>

namespace mc::crafting {

struct Result {
    ItemStack output;
    std::vector<ItemStack> remaining;
    int recipeIndex{-1};
    bool matched{};
};
struct RepairResult { ItemStack output; int cost{},materialCost{}; };

// Original 1.8.9 shaped/shapeless matching, with metadata wildcard 32767.
// Ordinary recipes intentionally ignore ingredient NBT, as the Java source
// does; copyIngredientNBT and output/remainder tags retain their semantics.
Result match(const std::vector<ItemStack>& grid, int width, int height);
void refresh(Game& game);
ItemStack smeltingResult(const ItemStack& input);
ItemStack containerItem(const ItemStack& input);
int fuelBurnTime(const ItemStack& input);
bool potionIngredient(const ItemStack& input);
bool repairIngredient(int itemId, int ingredientId);
int merchantResult(const ContainerWindow& window,const std::vector<ItemStack>& slots,ItemStack& result);
void refreshMerchant(Game& game);
RepairResult anvilResult(const ItemStack& first,const ItemStack& second,const std::string& name,bool creative);
void refreshAnvil(Game& game);
bool enchantable(const ItemStack& item);
void refreshEnchantment(Game& game);
std::size_t ordinaryRecipeCount();
std::size_t specialRecipeCount();

} // namespace mc::crafting
