#pragma once
#include "mc/nbt.hpp"
#include "mc/worldgen_noise.hpp"

namespace mc::worldgen {
// Complete 1.8.9 ItemStack NBT, including typed StoredEnchantments and list order.
nbt::Tag makeEnchantedBook(JavaRandom& random,int power=30);
// Complete fresh tile-entity NBT as produced by WorldGenDungeons. Runtime
// chest/spawner simulation and insertion into the owning save remain external.
nbt::Tag makeDungeonChest(JavaRandom& random,int x,int y,int z);
nbt::Tag makeDungeonSpawner(JavaRandom& random,int x,int y,int z);
}
