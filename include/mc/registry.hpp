#pragma once

#include <array>
#include <cstdint>

namespace mc::registry {

struct BlockInfo {
    bool valid{};
    const char* name{""};
    const char* kind{""};
    const char* material{""};
    float hardness{};
    bool toolRequired{};
    std::uint16_t replaceableMask{}, validMetadataMask{};
    const char* stepSound{""};
    const char* breakSound{""};
    const char* placeSound{""};
    float soundVolume{}, soundPitch{}, slipperiness{.6f};
    int lightOpacity{};
    bool fullCube{}, normalCube{}, fullBlock{};
    bool materialOpaque{}, materialBlocksMovement{}, materialSolid{};
};

struct ItemInfo {
    bool valid{};
    const char* name{""};
    const char* kind{""};
    int maxStack{64}, maxDamage{}, useDuration{}, useAction{};
    bool hasSubtypes{};
    int armorSlot{-1}; // ItemArmor armorType: 0 helmet, 1 chest, 2 legs, 3 boots.
    int blockId{-1}; // Block.getBlockFromItem, otherwise -1 (special item logic remains separate).
    bool food{}, alwaysEdible{};
    int healAmount{};
    float saturationModifier{};
    double attackDamage{}; // Equipment operation-0 modifier, excluding player base damage.
    std::uint16_t toolRule{};
    std::uint16_t foodRule{};
};

struct FoodRule {
    std::array<int, 16> heal{};
    std::array<float, 16> saturation{};
};

struct ToolRule {
    std::array<float, 198> strength{};
    std::array<bool, 198> harvest{};
};

struct BiomeInfo {
    bool valid{};
    const char* name{""};
    float temperature{}, rainfall{};
    std::uint32_t grassColor{}, foliageColor{}, waterColor{0xffffff};
    bool snow{};
};

} // namespace mc::registry

#include "mc/registry_data.hpp"

namespace mc::registry {
// Block.registerBlocks plus BlockEndPortal/BlockRedstoneLight constructors.
inline int lightValue(unsigned id) {
    switch(id) {
    case 10:case 11:case 51:case 89:case 91:case 119:case 124:case 138:case 169:return 15;
    case 50:return 14;
    case 62:return 13;
    case 90:return 11;
    case 74:case 150:return 9;
    case 76:case 130:return 7;
    case 39:case 117:case 120:case 122:return 1;
    default:return 0;
    }
}
inline const BlockInfo& block(unsigned id) {
    static constexpr BlockInfo unknown{};
    return id < detail::blocks.size() ? detail::blocks[id] : unknown;
}
inline bool translucent(unsigned id) {
    return id==166||(!block(id).materialBlocksMovement&&(id<8||id>11));
}
inline const ItemInfo& item(int id) {
    static constexpr ItemInfo unknown{};
    return id >= 0 && static_cast<unsigned>(id) < detail::items.size() ? detail::items[id] : unknown;
}
inline float strength(int itemId, unsigned blockId) {
    return blockId < 198 ? detail::tools[item(itemId).toolRule].strength[blockId] : 1.0f;
}
inline float digSpeed(int itemId, unsigned blockId) { return strength(itemId, blockId); }
inline bool canHarvest(int itemId, unsigned blockId) {
    return blockId < 198 && detail::tools[item(itemId).toolRule].harvest[blockId];
}
inline bool replaceable(std::uint16_t state) {
    return (block(state >> 4).replaceableMask & (1u << (state & 15))) != 0;
}
inline int foodHeal(int id, int metadata) {
    const auto& info = item(id);
    return info.foodRule && metadata >= 0 && metadata < 16 ? detail::foods[info.foodRule].heal[metadata] : info.healAmount;
}
inline float foodSaturation(int id, int metadata) {
    const auto& info = item(id);
    return info.foodRule && metadata >= 0 && metadata < 16 ? detail::foods[info.foodRule].saturation[metadata] : info.saturationModifier;
}
inline const BiomeInfo& biome(unsigned id) {
    return id < detail::biomes.size() && detail::biomes[id].valid ? detail::biomes[id] : detail::biomes[1];
}
} // namespace mc::registry
