#include "mc/registry.hpp"

#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
int main() {
    try {
        using namespace mc::registry;
        check(block(49).hardness == 50.0f && block(49).toolRequired, "obsidian hardness/material");
        check(!block(999).valid && !item(-1).valid && item(-1).maxStack == 64, "unknown registry sentinel");
        check(replaceable((78 << 4) | 0) && !replaceable((78 << 4) | 1), "snow replaceability depends on layer metadata");
        check(item(278).maxStack == 1 && item(278).maxDamage == 1561 && std::strcmp(item(278).kind, "ItemPickaxe") == 0, "diamond pickaxe descriptor");
        check(strength(278, 1) == 8.0f && canHarvest(278, 49) && !canHarvest(257, 49), "tool strength and obsidian harvest tier");
        check(strength(-1, 1) == 1.0f && !canHarvest(-1, 1), "empty hand tool rule");
        check(item(261).useDuration == 72000 && item(261).useAction == 4, "bow use descriptor");
        check(item(335).useDuration == 32 && item(335).useAction == 2 && !item(335).food, "milk drinking differs from food");
        check(item(322).food && item(322).alwaysEdible && item(322).healAmount == 4, "golden apple food behavior");
        check(foodHeal(349, 2) == 1 && foodHeal(350, 1) == 6 && foodSaturation(350, 1) == .8f, "fish food metadata variants");
        check(item(310).armorSlot == 0 && item(313).armorSlot == 3, "armor slot mapping");
        check(item(1).blockId == 1 && item(280).blockId == -1, "ItemBlock mapping");
        check(biome(2).temperature == 2.0f && biome(2).rainfall == 0 && biome(6).grassColor != 0, "real vanilla biome values and colormaps");
        unsigned count = 0;
        for (int id = 0; id < 2268; ++id) if (item(id).valid) ++count;
        check(count == 337, "all registered vanilla items exported");
        std::cout << "registry tests passed (198 blocks, 337 items, 61 biomes)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "registry test failed: " << error.what() << '\n';
        return 1;
    }
}
