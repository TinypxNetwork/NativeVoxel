#pragma once

#include "mc/game.hpp"
#include "mc/nbt.hpp"
#include "mc/registry.hpp"
#include <algorithm>
#include <string>

namespace mc::item_nbt {

inline std::int64_t integer(const nbt::Tag &tag, const char *name, std::int64_t fallback = 0) {
    const auto *value = tag.get(name);
    return value && value->type >= 1 && value->type <= 4 ? value->integer : fallback;
}

// ItemStack.readFromNBT: accept legacy numeric IDs and ResourceLocation names.
// Native empty stacks are normalized at the inventory boundary.
inline ItemStack read(const nbt::Tag &tag) {
    ItemStack item;
    const auto *id = tag.get("id");
    if (!id)
        return item;
    if (id->type == 8) {
        const auto name = id->string.find(':') == std::string::npos ? "minecraft:" + id->string : id->string;
        for (int i = 0; i <= 2267; ++i) {
            if (registry::item(i).valid && name == registry::item(i).name) {
                item.id = static_cast<std::int16_t>(i);
                break;
            }
        }
        if (item.id < 0) {
            try {
                std::size_t end = 0;
                const int numeric = std::stoi(id->string, &end);
                if (end == id->string.size() && registry::item(numeric).valid)
                    item.id = static_cast<std::int16_t>(numeric);
            } catch (const std::exception &) {
            }
        }
    } else {
        item.id = static_cast<std::int16_t>(integer(tag, "id", -1));
    }
    item.count = static_cast<std::int8_t>(integer(tag, "Count"));
    item.damage = std::max<std::int16_t>(0, static_cast<std::int16_t>(integer(tag, "Damage")));
    if (!registry::item(item.id).valid || item.count <= 0)
        return {};
    if (const auto *extra = tag.get("tag"); extra && extra->type == 10)
        item.nbt = nbt::encode(*extra);
    return item;
}

inline nbt::Tag write(const ItemStack &item, int slot = -1) {
    nbt::Tag tag;
    tag.type = 10;
    tag.compound["id"] = nbt::text(registry::item(item.id).name);
    tag.compound["Count"] = nbt::integer(1, item.count);
    tag.compound["Damage"] = nbt::integer(2, item.damage);
    if (slot >= 0)
        tag.compound["Slot"] = nbt::integer(1, slot);
    if (!item.nbt.empty()) {
        auto extra = nbt::parse(item.nbt);
        if (extra.type == 10)
            tag.compound["tag"] = std::move(extra);
    }
    return tag;
}

template <class Slots> void readSlots(const nbt::Tag &tag, Slots &slots, const char *name = "Items") {
    const auto *items = tag.get(name);
    if (!items || items->type != 9 || items->subtype != 10)
        return;
    for (const auto &value : items->list) {
        const auto slot = static_cast<std::int8_t>(integer(value, "Slot", -1));
        if (slot >= 0 && static_cast<std::size_t>(slot) < slots.size())
            slots[slot] = read(value);
    }
}

template <class Slots> nbt::Tag writeSlots(const Slots &slots) {
    nbt::Tag items;
    items.type = 9;
    items.subtype = 10;
    for (std::size_t i = 0; i < slots.size(); ++i)
        if (!slots[i].empty())
            items.list.push_back(write(slots[i], static_cast<int>(i)));
    return items;
}

} // namespace mc::item_nbt
