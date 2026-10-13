#pragma once
#include "mc/game.hpp"

namespace mc::projectiles {
bool supported(int objectType) noexcept;
void initialize(Entity&);
void velocity(Entity&,double x,double y,double z);
void correctPosition(Entity&); // Arrow/throwables snap; fishing retains its 3-tick interpolation.
void tick(Game&,Entity&);
void finishTick(Game&); // Impacts and removals after entity iteration, without invalidating iterators.
Entity launch(const Game&,int id,const ItemStack&,float bowCharge,std::uint64_t seed);
int fishingHook(const Game&,int ownerId);
void applyPotion(Game&,const ItemStack&,double strength=1,int targetEntity=-1);
} // namespace mc::projectiles
