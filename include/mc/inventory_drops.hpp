#pragma once

#include "mc/game.hpp"
#include "mc/worldgen_noise.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace mc::inventory_drops {

// InventoryHelper owns a java.util.Random separate from the world's RNG. Keep
// its cached Gaussian as well as the LCG seed across calls and native saves.
struct Random {
    worldgen::JavaRandom uniform{0};
    bool hasGaussian{};
    double gaussian{};

    void setSeed(std::uint64_t seed) {
        uniform.setSeed(static_cast<std::int64_t>(seed));
        hasGaussian = false;
        gaussian = 0;
    }
    double nextGaussian() {
        if (hasGaussian) {
            hasGaussian = false;
            return gaussian;
        }
        double a, b, s;
        do {
            a = 2 * uniform.nextDouble() - 1;
            b = 2 * uniform.nextDouble() - 1;
            s = a * a + b * b;
        } while (s >= 1 || s == 0);
        const double multiplier = std::sqrt(-2 * std::log(s) / s);
        gaussian = b * multiplier;
        hasGaussian = true;
        return a * multiplier;
    }
};

struct Drop {
    ItemStack item;
    double x{}, y{}, z{}, motionX{}, motionY{}, motionZ{};
};

// Unmodified MCP InventoryHelper.spawnItemStack: offsets are drawn once per
// occupied slot, split sizes before each entity, then X/Y/Z Gaussian velocity.
// The caller owns validation and commits this preview only after capacity checks.
inline std::vector<Drop> split(const std::vector<ItemStack> &slots, Random &random) {
    std::vector<Drop> result;
    for (const auto &stack : slots) {
        if (stack.empty())
            continue;
        const double x = random.uniform.nextFloat() * .8f + .1f;
        const double y = random.uniform.nextFloat() * .8f + .1f;
        const double z = random.uniform.nextFloat() * .8f + .1f;
        for (int remaining = stack.count; remaining > 0;) {
            const int count = std::min(random.uniform.nextInt(21) + 10, remaining);
            remaining -= count;
            Drop drop;
            drop.item = stack;
            drop.item.count = static_cast<std::int8_t>(count);
            drop.x = x;
            drop.y = y;
            drop.z = z;
            drop.motionX = random.nextGaussian() * static_cast<double>(.05f);
            drop.motionY = random.nextGaussian() * static_cast<double>(.05f) + .20000000298023224;
            drop.motionZ = random.nextGaussian() * static_cast<double>(.05f);
            result.push_back(std::move(drop));
        }
    }
    return result;
}
} // namespace mc::inventory_drops
