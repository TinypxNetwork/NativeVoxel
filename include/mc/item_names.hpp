#pragma once
#include "mc/i18n.hpp"
#include "mc/potions.hpp"
#include <optional>

namespace mc {
inline const char* entityNameKey(int id){
    struct Name {int id;const char* name;};
    static constexpr Name names[]={{1,"Item"},{2,"XPOrb"},{7,"ThrownEgg"},{8,"LeashKnot"},{9,"Painting"},{10,"Arrow"},{11,"Snowball"},{12,"Fireball"},{13,"SmallFireball"},{14,"ThrownEnderpearl"},{15,"EyeOfEnderSignal"},{16,"ThrownPotion"},{17,"ThrownExpBottle"},{18,"ItemFrame"},{19,"WitherSkull"},{20,"PrimedTnt"},{21,"FallingSand"},{22,"FireworksRocketEntity"},{30,"ArmorStand"},{40,"MinecartCommandBlock"},{41,"Boat"},{42,"MinecartRideable"},{43,"MinecartChest"},{44,"MinecartFurnace"},{45,"MinecartTNT"},{46,"MinecartHopper"},{47,"MinecartSpawner"},{48,"Mob"},{49,"Monster"},{50,"Creeper"},{51,"Skeleton"},{52,"Spider"},{53,"Giant"},{54,"Zombie"},{55,"Slime"},{56,"Ghast"},{57,"PigZombie"},{58,"Enderman"},{59,"CaveSpider"},{60,"Silverfish"},{61,"Blaze"},{62,"LavaSlime"},{63,"EnderDragon"},{64,"WitherBoss"},{65,"Bat"},{66,"Witch"},{67,"Endermite"},{68,"Guardian"},{90,"Pig"},{91,"Sheep"},{92,"Cow"},{93,"Chicken"},{94,"Squid"},{95,"Wolf"},{96,"MushroomCow"},{97,"SnowMan"},{98,"Ozelot"},{99,"VillagerGolem"},{100,"EntityHorse"},{101,"Rabbit"},{120,"Villager"},{200,"EnderCrystal"}};
    for(const auto& value:names)if(value.id==id)return value.name;return nullptr;
}
// ItemPotion and ItemMonsterPlacer compose multiple translation keys. A single
// generic registry key loses the potion effect or spawned creature's name.
inline std::optional<std::string> variantItemName(const ItemStack& item,const I18n& tr){
    if(item.id==383){auto name=tr("item.monsterPlacer.name");if(const auto* entity=entityNameKey(item.damage))name+=" "+tr("entity."+std::string(entity)+".name");return name;}
    if(item.id!=373)return std::nullopt;
    if(item.damage==0)return tr("item.emptyPotion.name");
    const auto effects=potions::effects(item);
    if(!effects.empty())return std::string((item.damage&16384)?tr("potion.prefix.grenade")+" ":"")+tr(std::string(potions::key(effects.front().id))+".postfix");
    return tr("potion.prefix."+std::string(potions::prefix(item.damage)))+" "+tr("item.potion.name");
}
} // namespace mc
