#include "mc/controller.hpp"
#include "mc/crafting.hpp"
#include "mc/nbt.hpp"
#include "mc/registry.hpp"
#include "mc/projectiles.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace mc {
namespace {
using net::ProtocolError;
using net::Reader;

struct OfflineClickRollback {
    Game& game;
    std::array<ItemStack,45> inventory;
    std::array<std::uint64_t,45> revisions;
    ItemStack cursor;
    ContainerWindow window;
    int level,total;
    float progress;
    bool accepted{};
    explicit OfflineClickRollback(Game& value):game(value),inventory(value.inventory),revisions(value.inventorySlotRevision),cursor(value.cursor),window(value.window),level(value.experienceLevel),total(value.totalExperience),progress(value.experienceProgress){}
    ~OfflineClickRollback(){if(!accepted){game.inventory=std::move(inventory);game.inventorySlotRevision=revisions;game.cursor=std::move(cursor);game.window=std::move(window);game.experienceLevel=level;game.totalExperience=total;game.experienceProgress=progress;}}
};

using Tag=nbt::Tag;
Tag itemTag(const ItemStack& stack) { return nbt::parse(stack.nbt); }
bool sameTags(const ItemStack& left, const ItemStack& right) {
    if (left.nbt == right.nbt) return true;
    return itemTag(left) == itemTag(right);
}
bool sameItem(const ItemStack& left, const ItemStack& right, bool damage = true) {
    return left.empty() && right.empty() ? true : (!left.empty() && !right.empty() && left.id == right.id &&
        (!damage || left.damage == right.damage) && sameTags(left, right));
}
bool unbreakable(const ItemStack& stack) {
    const auto root = itemTag(stack); const auto* tag = root.get("Unbreakable");
    return tag && tag->type >= 1 && tag->type <= 4 && tag->integer != 0;
}
bool damageable(const ItemStack& stack) { return !stack.empty() && registry::item(stack.id).maxDamage > 0 && !unbreakable(stack); }
ItemStack& held(Game& game) { return game.inventory[36 + static_cast<std::size_t>(std::clamp(game.selectedSlot, 0, 8))]; }
const ItemStack& held(const Game& game) { return game.inventory[36 + static_cast<std::size_t>(std::clamp(game.selectedSlot, 0, 8))]; }
void normalize(ItemStack& stack) { if (stack.count <= 0 || stack.id < 0) stack = ItemStack(); }
bool kindIs(const registry::ItemInfo& item, const char* kind) { return std::strcmp(item.kind, kind) == 0; }
bool blockKindIs(const registry::BlockInfo& block, const char* kind) { return std::strcmp(block.kind, kind) == 0; }
bool isSword(const ItemStack& stack) { return !stack.empty() && kindIs(registry::item(stack.id), "ItemSword"); }
bool isTool(const ItemStack& stack) {
    if (stack.empty()) return false;
    const auto& info = registry::item(stack.id);
    return kindIs(info, "ItemPickaxe") || kindIs(info, "ItemSpade") || kindIs(info, "ItemAxe") || kindIs(info, "ItemTool");
}
bool tagAllowsBlock(const ItemStack& stack, const char* listName, unsigned blockId) {
    const auto root = itemTag(stack); const auto* list = root.get(listName);
    if (!list || list->type != 9 || list->subtype != 8) return false;
    const std::string fullName = registry::block(blockId).name;
    const auto colon = fullName.find(':'); const auto shortName = colon == std::string::npos ? fullName : fullName.substr(colon + 1);
    for (const auto& value : list->list) if (value.string == fullName || value.string == shortName || value.string == std::to_string(blockId)) return true;
    return false;
}
bool borderContains(const Game& game, int x, int z) {
    const auto& border = game.world.border();
    return static_cast<double>(x) + 1 > border.minX && x < border.maxX && static_cast<double>(z) + 1 > border.minZ && z < border.maxZ;
}
constexpr int offsets[6][3] = {{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}};
ControllerTarget adjacent(const ControllerTarget& target) {
    auto result = target;
    if (target.face >= 0 && target.face < 6) { result.x += offsets[target.face][0]; result.y += offsets[target.face][1]; result.z += offsets[target.face][2]; }
    return result;
}
bool insideWater(const Game& game) {
    const auto eyeY = game.player.eyeY(); const int y = floorBlock(eyeY);
    const auto state = game.world.getBlock(floorBlock(game.player.x), y, floorBlock(game.player.z));
    if (blockId(state) != 8 && blockId(state) != 9) return false;
    unsigned level = blockMeta(state); if (level >= 8) level = 0;
    const float surfaceOffset = static_cast<float>(level + 1) / 9.0f - .11111111f;
    const float surface = static_cast<float>(y + 1) - surfaceOffset;
    return eyeY < static_cast<double>(surface);
}
void sound(Game& game, const char* name, double x, double y, double z, float volume, float pitch) {
    if (!name || !*name) return;
    game.soundEvents.push_back({name,x,y,z,volume,pitch});
    if (game.soundEvents.size() > 256) game.soundEvents.pop_front();
}
bool hasItem(const Game& game, int id) {
    for (std::size_t i = 9; i < 45; ++i) if (game.inventory[i].id == id && !game.inventory[i].empty()) return true;
    return false;
}
bool consumeOne(Game& game, int id) {
    // InventoryPlayer's mainInventory order is hotbar first, then backpack.
    for (int i = 0; i < 36; ++i) {
        const std::size_t slot = static_cast<std::size_t>(i < 9 ? 36 + i : i);
        if (game.inventory[slot].id == id && !game.inventory[slot].empty()) { --game.inventory[slot].count; normalize(game.inventory[slot]); return true; }
    }
    return false;
}
bool giveItem(Game& game, ItemStack stack) {
    if (stack.empty()) return true;
    const int maximum = registry::item(stack.id).maxStack;
    for (int i = 0; i < 36 && !stack.empty(); ++i) {
        const auto slot = static_cast<std::size_t>(i < 9 ? 36 + i : i);
        auto& current = game.inventory[slot];
        if (sameItem(current, stack, registry::item(stack.id).hasSubtypes) && !current.empty()) {
            const int moved = std::max(0, std::min<int>(stack.count, maximum - current.count));
            current.count = static_cast<std::int8_t>(current.count + moved); stack.count = static_cast<std::int8_t>(stack.count - moved); normalize(stack);
        }
    }
    for (int i = 0; i < 36 && !stack.empty(); ++i) {
        const auto slot = static_cast<std::size_t>(i < 9 ? 36 + i : i);
        if (game.inventory[slot].empty()) { game.inventory[slot] = stack; return true; }
    }
    return stack.empty();
}
int horizontalFacing(float yaw) {
    const int index = static_cast<int>(std::floor(static_cast<double>(yaw / 90.0f) + .5)) & 3;
    static constexpr int faces[] = {3,4,2,5}; return faces[index];
}
int opposite(int face) { return face ^ 1; }

bool storageWindow(const Game& game) {
    if (!game.window.open) return true;
    const auto& type = game.window.type;
    return type == "minecraft:container" || type == "minecraft:chest" || type == "minecraft:dispenser" ||
        type == "minecraft:dropper" || type == "minecraft:hopper" || type == "minecraft:crafting_table" ||
        type == "minecraft:furnace" || type == "minecraft:brewing_stand" || type == "minecraft:enchanting_table" ||
        type == "minecraft:beacon" || type == "minecraft:villager" || type=="minecraft:anvil" || type == "EntityHorse";
}
int craftingSlots(const Game& game) { return !game.window.open ? 4 : (game.window.type=="minecraft:crafting_table" ? 9 : 0); }
bool craftingOutput(const Game& game,int slot) { return slot==0 && craftingSlots(game)>0; }
bool slotAccepts(const Game& game, int slot, const ItemStack& stack) {
    if (stack.empty()) return true;
    if (!game.window.open) {
        if (slot == 0) return false;
        if (slot >= 5 && slot < 9) {
            const int armor = registry::item(stack.id).armorSlot;
            return armor == slot - 5 || (slot == 5 && (stack.id == 86 || stack.id == 397));
        }
    } else {
        const auto& type=game.window.type;
        if (type=="minecraft:crafting_table" && slot==0) return false;
        if((type=="minecraft:villager"||type=="minecraft:anvil")&&slot==2)return false;
        if (type=="minecraft:furnace") {
            if (slot==2) return false;
            if (slot==1) return crafting::fuelBurnTime(stack)>0 || stack.id==325;
        }
        if (type=="minecraft:brewing_stand") {
            if (slot>=0 && slot<3) return stack.id==373 || stack.id==374;
            if (slot==3) return crafting::potionIngredient(stack);
        }
        if (type=="minecraft:enchanting_table" && slot==1) return stack.id==351 && stack.damage==4;
        if (type=="minecraft:beacon" && slot==0) return stack.id==264 || stack.id==265 || stack.id==266 || stack.id==388;
        if (type=="EntityHorse") {
            if (slot==0) return stack.id==329 && (game.window.slots.empty() || game.window.slots[0].empty());
            if (slot==1) {
                const auto horse=game.entities.find(game.window.entityId);
                if (horse!=game.entities.end()) {
                    const auto kind=horse->second.metadata.find(19);
                    if (kind!=horse->second.metadata.end() && kind->second.integer!=0) return false;
                }
                return stack.id==417 || stack.id==418 || stack.id==419;
            }
        }
    }
    return true;
}
int slotLimit(const Game& game,int slot,const ItemStack& stack=ItemStack{}) {
    if (!game.window.open) return slot>=5 && slot<9 ? 1 : 64;
    const auto& type=game.window.type;
    if (type=="minecraft:furnace" && slot==1 && stack.id==325) return 1;
    if (type=="minecraft:brewing_stand" && slot>=0 && slot<3) return 1;
    if (type=="minecraft:enchanting_table" && slot==0) return 1;
    if (type=="minecraft:beacon" && slot==0) return 1;
    return 64;
}

} // namespace

void Controller::reset() {
    progress = 0; currentTarget = {}; currentTarget.x = currentTarget.y = currentTarget.z = -1;
    mining = false; status.clear(); unsupportedInteractions = 0;
    hittingItem_ = usingStack_ = ItemStack(); hitDelay_ = leftClickCounter_ = rightClickDelay_ = 0;
    stepSoundCounter_ = 0; useTicksRemaining_ = 0; useSlot_ = -1; itemInUse_ = false;
    useSlotRevision_ = seenUseCompletions_ = seenWindowRevision_ = 0;
    currentWindow_ = -1; nextTransaction_ = 0; dragEvent_ = dragMode_ = 0; dragSlots_.clear();
    lastGameTick_ = 0; started_ = false;
}
int Controller::enchantment(const ItemStack& item, int id) {
    const auto root = itemTag(item); const auto* enchantments = root.get("ench");
    if (!enchantments || enchantments->type != 9 || enchantments->subtype != 10) return 0;
    for (const auto& tag : enchantments->list) {
        const auto* key = tag.get("id"); const auto* level = tag.get("lvl");
        if (key && level && key->integer == id) return static_cast<std::int16_t>(level->integer);
    }
    return 0;
}
int Controller::maxStackSize(const ItemStack& item) const { return registry::item(item.id).maxStack; }
int Controller::randomInt(int bound) {
    if (bound <= 0) return 0;
    auto next = [&] { randomState_ = (randomState_ * 0x5deece66dull + 0xb) & ((1ull << 48) - 1); return static_cast<std::uint32_t>(randomState_ >> 17); };
    if ((bound & -bound) == bound) return static_cast<int>((static_cast<std::uint64_t>(bound) * next()) >> 31);
    for (;;) {
        const auto bits = next(); const auto value = bits % static_cast<std::uint32_t>(bound);
        if (static_cast<std::uint64_t>(bits) - value + static_cast<unsigned>(bound - 1) < 0x80000000ull) return static_cast<int>(value);
    }
}
void Controller::damageItem(Game& game, ItemStack& item, int amount) {
    if (game.creativeMode || !damageable(item)) return;
    const int unbreakingLevel = enchantment(item, 34);
    int damage = 0;
    for (int i = 0; i < amount; ++i) if (unbreakingLevel <= 0 || randomInt(unbreakingLevel + 1) == 0) ++damage;
    const int nextDamage = static_cast<int>(item.damage) + damage;
    if (nextDamage > registry::item(item.id).maxDamage) { sound(game,"random.break",game.player.x,game.player.y,game.player.z,.8f,.8f+visualRandom()*.4f);--item.count; item.damage = 0; normalize(item); }
    else item.damage = static_cast<std::int16_t>(nextDamage);
}
float Controller::relativeBlockHardness(const Game& game, int x, int y, int z) const {
    const auto id = blockId(game.world.getBlock(x,y,z)); const auto& info = registry::block(id);
    if (!info.valid || info.hardness < 0) return 0;
    const auto& stack = held(game);
    float speed = registry::strength(stack.empty() ? -1 : stack.id, id);
    if (speed > 1 && !stack.empty()) { const int efficiency = enchantment(stack, 32); if (efficiency > 0) speed += static_cast<float>(efficiency * efficiency + 1); }
    const auto haste = game.effects.find(3);
    if (haste != game.effects.end()) speed *= 1.0f + static_cast<float>(haste->second.amplifier + 1) * .2f;
    const auto fatigue = game.effects.find(4);
    if (fatigue != game.effects.end()) {
        float multiplier;
        switch (fatigue->second.amplifier) { case 0: multiplier = .3f; break; case 1: multiplier = .09f; break; case 2: multiplier = .0027f; break; default: multiplier = 8.1e-4f; }
        speed *= multiplier;
    }
    bool aquaAffinity = false;
    for (std::size_t slot = 5; slot < 9; ++slot) if (enchantment(game.inventory[slot], 6) > 0) aquaAffinity = true;
    if (insideWater(game) && !aquaAffinity) speed /= 5.0f;
    if (!game.player.onGround) speed /= 5.0f;
    const bool harvest = !info.toolRequired || (!stack.empty() && registry::canHarvest(stack.id, id));
    return speed / info.hardness / (harvest ? 30.0f : 100.0f);
}
bool Controller::sameMiningTarget(const Game& game, const ControllerTarget& target) const {
    if (target.x != currentTarget.x || target.y != currentTarget.y || target.z != currentTarget.z) return false;
    const auto& item = held(game);
    return sameItem(item, hittingItem_, !damageable(item));
}
void Controller::resetMining(Game& game) {
    if (!mining) return;
    game.digBlock(1, currentTarget.x,currentTarget.y,currentTarget.z,0);
    mining = false; progress = 0;
}
bool Controller::destroyBlock(Game& game, const ControllerTarget& target) {
    const auto state = game.world.getBlock(target.x,target.y,target.z); const unsigned id = blockId(state);
    if (game.gameMode == 3 || (game.gameMode == 2 && !tagAllowsBlock(held(game), "CanDestroy", id)) ||
        (game.gameMode == 1 && isSword(held(game))) || id == 0) return false;
    if(!game.online&&game.offlineBlockBreak){
        const bool changed=game.offlineBlockBreak(target.x,target.y,target.z,target.face);
        currentTarget.y=-1;return changed;
    }
    const auto& block = registry::block(id);
    sound(game,block.breakSound,target.x+.5,target.y+.5,target.z+.5,(block.soundVolume+1.0f)/2.0f,block.soundPitch*.8f);
    const bool changed = game.world.setBlock(target.x,target.y,target.z,0);
    if(changed)game.invalidateTile(target.x,target.y,target.z);
    // Paired-block callbacks are explicit for the common vanilla structures.
    if (blockKindIs(block,"BlockDoor")) game.world.setBlock(target.x,target.y + ((blockMeta(state)&8) ? -1 : 1),target.z,0);
    if (blockKindIs(block,"BlockDoublePlant")) game.world.setBlock(target.x,target.y + ((blockMeta(state)&8) ? -1 : 1),target.z,0);
    currentTarget.y = -1;
    if (game.gameMode != 1 && !held(game).empty()) {
        if (isSword(held(game)) && block.hardness != 0) damageItem(game,held(game),2);
        else if (isTool(held(game)) && block.hardness != 0) damageItem(game,held(game),1);
        else if (kindIs(registry::item(held(game).id),"ItemShears") &&
            (std::strcmp(block.material,"leaves")==0 || id==30 || id==31 || id==106 || id==132 || id==35)) damageItem(game,held(game),1);
    }
    return changed;
}
bool Controller::clickBlock(Game& game, const ControllerTarget& target) {
    const auto id = blockId(game.world.getBlock(target.x,target.y,target.z));
    if (target.face < 0 || target.face >= 6 || game.gameMode == 3 ||
        (game.gameMode == 2 && !tagAllowsBlock(held(game),"CanDestroy",id)) || !borderContains(game,target.x,target.z)) return false;
    if (game.gameMode == 1) {
        game.digBlock(0,target.x,target.y,target.z,target.face);
        const auto fire = adjacent(target);
        if (blockId(game.world.getBlock(fire.x,fire.y,fire.z)) == 51) game.world.setBlock(fire.x,fire.y,fire.z,0);
        else destroyBlock(game,target);
        hitDelay_ = 5;
    } else if (!mining || !sameMiningTarget(game,target)) {
        if (mining) game.digBlock(1,currentTarget.x,currentTarget.y,currentTarget.z,target.face);
        game.digBlock(0,target.x,target.y,target.z,target.face);
        if (id && relativeBlockHardness(game,target.x,target.y,target.z) >= 1.0f) destroyBlock(game,target);
        else {
            mining = true; currentTarget = target; hittingItem_ = held(game);
            progress = 0; stepSoundCounter_ = 0;
        }
    }
    return true;
}
bool Controller::damageBlock(Game& game, const ControllerTarget& target) {
    game.syncSelectedSlot();
    if (hitDelay_ > 0) { --hitDelay_; return true; }
    if (game.gameMode == 1 && borderContains(game,target.x,target.z)) {
        hitDelay_ = 5; game.digBlock(0,target.x,target.y,target.z,target.face);
        const auto fire = adjacent(target);
        if (blockId(game.world.getBlock(fire.x,fire.y,fire.z)) == 51) game.world.setBlock(fire.x,fire.y,fire.z,0);
        else destroyBlock(game,target);
        return true;
    }
    if (sameMiningTarget(game,target)) {
        const auto id = blockId(game.world.getBlock(target.x,target.y,target.z));
        if (!id) { mining = false; return false; }
        progress += relativeBlockHardness(game,target.x,target.y,target.z);
        const auto& info = registry::block(id);
        if (std::fmod(stepSoundCounter_,4.0f) == 0) sound(game,info.stepSound,target.x+.5,target.y+.5,target.z+.5,(info.soundVolume+1.0f)/8.0f,info.soundPitch*.5f);
        ++stepSoundCounter_;
        if (progress >= 1) {
            mining = false; game.digBlock(2,target.x,target.y,target.z,target.face); destroyBlock(game,target);
            progress = stepSoundCounter_ = 0; hitDelay_ = 5;
        }
        return true;
    }
    return clickBlock(game,target);
}
void Controller::predictAttack(Game& game, int id) {
    if (game.gameMode == 3) return;
    const auto found = game.entities.find(id); if (found == game.entities.end()) return;
    auto& target = found->second;
    // EntityOtherPlayerMP.attackEntityFrom returns true unconditionally. Most
    // EntityLivingBase mobs instead return false in a remote client world.
    const bool accepted = target.kind == EntityKind::Player ||
        (target.kind == EntityKind::Object && (target.type == 1 || target.type == 10));
    if (!accepted) { if (target.kind != EntityKind::Mob) ++unsupportedInteractions; return; }
    double attack = 1.0 + registry::item(held(game).id).attackDamage;
    const auto attribute = game.playerAttributes.find("generic.attackDamage");
    if (attribute != game.playerAttributes.end()) attack = attribute->second.value;
    if (attack <= 0 && enchantment(held(game),16) <= 0) return;
    const auto atFeet=blockId(game.world.getBlock(floorBlock(game.player.x),floorBlock(game.player.bounds().minY),floorBlock(game.player.z)));
    const bool critical=game.player.fallDistance>0&&!game.player.onGround&&atFeet!=65&&atFeet!=106&&!game.player.inWater&&!game.player.blind&&game.vehicleId<0&&target.kind==EntityKind::Player;
    if(critical&&attack>0)game.animationEvents.push_back({id,4,game.ticks});
    if(target.kind==EntityKind::Player&&enchantment(held(game),16)>0)game.animationEvents.push_back({id,5,game.ticks});
    while(game.animationEvents.size()>512)game.animationEvents.pop_front();
    int knockback = enchantment(held(game),19); if (game.player.sprinting) ++knockback;
    if (knockback > 0) {
        const float radians = game.player.yaw * static_cast<float>(3.14159265358979323846) / 180.0f;
        target.motionX += static_cast<double>(-javaSin(radians) * static_cast<float>(knockback) * .5f);
        target.motionY += .1;
        target.motionZ += static_cast<double>(javaCos(radians) * static_cast<float>(knockback) * .5f);
        game.player.motionX *= .6; game.player.motionZ *= .6; game.player.setSprinting(false);
    }
    if (target.kind == EntityKind::Player) {
        if (isSword(held(game))) damageItem(game,held(game),1);
        else if (isTool(held(game))) damageItem(game,held(game),2);
    }
}
void Controller::clickMouse(Game& game, const ControllerTarget& target) {
    if (leftClickCounter_ > 0) return;
    game.swingArm();
    if (target.entityId >= 0) { game.syncSelectedSlot(); game.attackEntity(target.entityId); predictAttack(game,target.entityId); }
    else if (target.block && blockId(game.world.getBlock(target.x,target.y,target.z))) clickBlock(game,target);
    else if (game.gameMode != 1) leftClickCounter_ = 10;
}

bool Controller::rightClickBlock(Game& game, const ControllerTarget& target) {
    if (target.face < 0 || target.face >= 6 || !borderContains(game,target.x,target.z)) return false;
    game.syncSelectedSlot();
    auto& stack = held(game); const auto oldState = game.world.getBlock(target.x,target.y,target.z);
    const auto& clicked = registry::block(blockId(oldState));
    bool activated = false;
    if (game.gameMode != 3 && (!game.player.sneaking || stack.empty())) {
        if(!game.online&&game.offlineBlockInteraction)activated=game.offlineBlockInteraction(target.x,target.y,target.z);
        if(!activated) {
        const std::string kind = clicked.kind;
        static const std::set<std::string> always = {"BlockChest","BlockEnderChest","BlockFurnace","BlockWorkbench","BlockAnvil","BlockEnchantmentTable",
            "BlockBrewingStand","BlockDispenser","BlockDropper","BlockHopper","BlockBeacon","BlockBed","BlockJukebox","BlockNote",
            "BlockCommandBlock","BlockDaylightDetector","BlockComparator","BlockRedstoneRepeater","BlockButtonStone","BlockButtonWood","BlockLever","BlockFenceGate","BlockTrapDoor","BlockDoor"};
        activated = always.count(kind) != 0;
        unsigned metadata = blockMeta(oldState);
        if (kind == "BlockJukebox") { activated = (metadata & 1) != 0; if (activated) metadata = 0; }
        else if (kind == "BlockLever") metadata ^= 8;
        else if (kind == "BlockButtonStone" || kind == "BlockButtonWood") metadata |= 8;
        else if (kind == "BlockFenceGate" || (kind == "BlockTrapDoor" && std::strcmp(clicked.material,"iron") != 0)) metadata ^= 4;
        else if (kind == "BlockDoor" && std::strcmp(clicked.material,"iron") != 0) {
            const int bottomY = target.y - ((metadata & 8) ? 1 : 0);
            const auto bottom = game.world.getBlock(target.x,bottomY,target.z);
            game.world.setBlock(target.x,bottomY,target.z,static_cast<BlockState>(bottom ^ 4));
        } else if (kind == "BlockCake") {
            activated = game.food < 20 && !game.invulnerable;
            if (activated) { game.food = std::min(20,game.food+2); game.saturation = std::min(static_cast<float>(game.food),game.saturation+.4f);
                game.world.setBlock(target.x,target.y,target.z,metadata >= 6 ? 0 : blockState(blockId(oldState),metadata+1)); }
        }
        if (metadata != blockMeta(oldState)) game.world.setBlock(target.x,target.y,target.z,blockState(blockId(oldState),metadata));
        }
    }
    const auto& item = registry::item(stack.id);
    const bool itemBlock = !stack.empty() && item.blockId >= 0;
    auto destination = target;
    if (!registry::replaceable(oldState)) destination = adjacent(target);
    unsigned placedMeta = item.hasSubtypes ? static_cast<unsigned>(stack.damage) & 15 : 0;
    unsigned placedId = item.blockId >= 0 ? static_cast<unsigned>(item.blockId) : 0;
    bool mergingSlab = false;
    if (itemBlock && (placedId == 44 || placedId == 126 || placedId == 182)) {
        const auto mergeAt = [&](const ControllerTarget& at, bool clicked) {
            const auto current = game.world.getBlock(at.x,at.y,at.z);
            return blockId(current) == placedId && (blockMeta(current)&7) == (placedMeta&7) &&
                (!clicked || (target.face == 1 && !(blockMeta(current)&8)) || (target.face == 0 && (blockMeta(current)&8)));
        };
        if (mergeAt(target,true)) { destination = target; mergingSlab = true; }
        else { const auto next = adjacent(target); if (mergeAt(next,false)) { destination = next; mergingSlab = true; } }
        if (mergingSlab) { placedId = placedId == 44 ? 43 : (placedId == 126 ? 125 : 181); placedMeta &= 7; }
        else placedMeta = (placedMeta&7) | ((target.face == 0 || (target.face != 1 && target.hitY > .5f)) ? 8 : 0);
    }
    if (!activated && itemBlock && game.gameMode != 3) {
        const auto destinationState = game.world.getBlock(destination.x,destination.y,destination.z);
        if (destination.y < 0 || destination.y >= 256 || (!mergingSlab && !registry::replaceable(destinationState)) ||
            !borderContains(game,destination.x,destination.z)) return false;
        const auto& block = registry::block(placedId);
        const int facing = horizontalFacing(game.player.yaw);
        if (blockKindIs(block,"BlockLog") || blockKindIs(block,"BlockOldLog") || blockKindIs(block,"BlockNewLog"))
            placedMeta = (placedMeta&3) | ((target.face <= 1) ? 0 : (target.face <= 3 ? 8 : 4));
        else if (blockKindIs(block,"BlockStairs")) placedMeta = static_cast<unsigned>(5-facing) | ((target.face == 0 || (target.face != 1 && target.hitY > .5f)) ? 4u : 0u);
        else if (blockKindIs(block,"BlockFurnace") || blockKindIs(block,"BlockChest") || blockKindIs(block,"BlockEnderChest")) placedMeta = static_cast<unsigned>(opposite(facing));
        else if (blockKindIs(block,"BlockHay")) placedMeta = target.face <= 1 ? 0 : (target.face <= 3 ? 8 : 4);
        const auto placed = blockState(placedId,placedMeta);
        // Query actual native collision boxes without exposing a lasting
        // prediction before checking player/entity collision.
        const auto original = game.world.getBlock(destination.x,destination.y,destination.z);
        game.world.setBlock(destination.x,destination.y,destination.z,placed);
        std::vector<Aabb> boxes; game.world.blockCollisionBoxes(destination.x,destination.y,destination.z,boxes);
        game.world.setBlock(destination.x,destination.y,destination.z,original);
        bool collides = false;
        for (const auto& box : boxes) if (box.intersects(game.player.bounds())) collides = true;
        if (collides) return false;
    }
    game.placeBlock(target.x,target.y,target.z,target.face,target.hitX,target.hitY,target.hitZ);
    if (activated || game.gameMode == 3) return true;
    if (stack.empty()) return false;
    if (game.gameMode == 2 && !tagAllowsBlock(stack,"CanPlaceOn",blockId(oldState))) return false;
    if (kindIs(item,"ItemRecord") && blockKindIs(clicked,"BlockJukebox") && !(blockMeta(oldState)&1)) return true;
    if (itemBlock) {
        game.world.setBlock(destination.x,destination.y,destination.z,blockState(placedId,placedMeta));
        const auto& block = registry::block(placedId);
        sound(game,block.placeSound,destination.x+.5,destination.y+.5,destination.z+.5,(block.soundVolume+1.0f)/2.0f,block.soundPitch*.8f);
        if (game.gameMode != 1) { --stack.count; normalize(stack); }
        return true;
    }
    ++unsupportedInteractions;
    status = "This item's block-use behavior is not fully ported";
    return false;
}

bool Controller::beginItemUse(Game& game) {
    if (game.gameMode == 3 || held(game).empty()) return false;
    game.syncSelectedSlot(); game.useHeldItem();
    auto& stack = held(game); const auto& item = registry::item(stack.id);
    if (item.armorSlot >= 0 && item.armorSlot < 4) {
        auto& armor = game.inventory[static_cast<std::size_t>(5+item.armorSlot)];
        if (armor.empty()) { armor = stack; stack = ItemStack(); return true; }
        return false;
    }
    bool begin = item.useDuration > 0;
    if (item.food) begin = begin && (item.alwaysEdible || game.food < 20) && !game.invulnerable;
    if (kindIs(item,"ItemBow")) begin = begin && (game.creativeMode || hasItem(game,262));
    if (kindIs(item,"ItemPotion") && (static_cast<unsigned>(stack.damage)&0x4000u)) {
        if(!game.online&&(!game.offlineProjectileUse||game.offlineProjectileUse(stack,1)<0))return false;
        sound(game,"random.bow",game.player.x,game.player.y,game.player.z,.5f,.4f/(float(randomInt(1<<24))/16777216.f*.4f+.8f));
        if (!game.creativeMode) { --stack.count; normalize(stack); }
        return true;
    }
    if(kindIs(item,"ItemFishingRod")){
        const bool retract=projectiles::fishingHook(game,game.entityId)>=0;
        const int cost=game.online?0:game.offlineProjectileUse?game.offlineProjectileUse(stack,1):-1;
        if(cost<0)return false;
        if(retract)damageItem(game,stack,cost);
        else sound(game,"random.bow",game.player.x,game.player.y,game.player.z,.5f,.4f/(float(randomInt(1<<24))/16777216.f*.4f+.8f));
        game.swingArm();return true;
    }
    if (begin) {
        itemInUse_ = true; useTicksRemaining_ = item.useDuration; useSlot_ = game.selectedSlot;
        usingStack_ = stack; useSlotRevision_ = game.inventorySlotRevision[36 + static_cast<std::size_t>(useSlot_)];
        return false; // Vanilla sendUseItem returns false when the same stack starts use.
    }
    if (kindIs(item,"ItemSnowball") || kindIs(item,"ItemEgg") || kindIs(item,"ItemExpBottle") || kindIs(item,"ItemEnderPearl")) {
        if (kindIs(item,"ItemEnderPearl") && game.creativeMode) return false;
        if(!game.online&&(!game.offlineProjectileUse||game.offlineProjectileUse(stack,1)<0))return false;
        sound(game,"random.bow",game.player.x,game.player.y,game.player.z,.5f,.4f/(float(randomInt(1<<24))/16777216.f*.4f+.8f));
        if (!game.creativeMode) { --stack.count; normalize(stack); }
        return true;
    }
    if (item.useDuration == 0 && item.blockId < 0) { // Unknown immediate use has no local prediction.
        ++unsupportedInteractions;
    }
    return false;
}
void Controller::stopItemUse(Game& game, bool notifyServer) {
    if (!itemInUse_) return;
    if (notifyServer) { game.syncSelectedSlot(); game.digBlock(5,0,0,0,0); }
    if (notifyServer && kindIs(registry::item(usingStack_.id),"ItemBow") && game.selectedSlot == useSlot_) {
        const bool infinite = game.creativeMode || enchantment(held(game),51) > 0;
        if (infinite || hasItem(game,262)) {
            const int elapsed = registry::item(usingStack_.id).useDuration - useTicksRemaining_;
            float charge = static_cast<float>(elapsed) / 20.0f; charge = (charge*charge+charge*2.0f)/3.0f;
            if (static_cast<double>(charge) >= .1&&(game.online||(game.offlineProjectileUse&&game.offlineProjectileUse(held(game),std::min(charge,1.f))>=0))) {
                charge=std::min(charge,1.f);
                sound(game,"random.bow",game.player.x,game.player.y,game.player.z,1,1.f/(float(randomInt(1<<24))/16777216.f*.4f+1.2f)+charge*.5f);
                damageItem(game,held(game),1); if (!infinite) consumeOne(game,262);
            }
        }
    }
    itemInUse_ = false; useTicksRemaining_ = 0; useSlot_ = -1; usingStack_ = ItemStack();
}
void Controller::finishItemUse(Game& game) {
    if (!itemInUse_) return;
    itemUseEffects(game,16);
    if (game.selectedSlot == useSlot_ && game.inventorySlotRevision[36 + static_cast<std::size_t>(useSlot_)] == useSlotRevision_) {
        auto& stack = held(game); const auto& item = registry::item(stack.id);
        if (item.food) {
            const int amount = registry::foodHeal(stack.id,stack.damage); const float saturation = registry::foodSaturation(stack.id,stack.damage);
            game.food = std::min(20,game.food+amount); game.saturation = std::min(static_cast<float>(game.food),game.saturation+static_cast<float>(amount)*saturation*2.0f);
            --stack.count;
            if (kindIs(item,"ItemSoup")) { stack = ItemStack(); stack.id = 281; stack.count = 1; }
            else normalize(stack);
        } else if (kindIs(item,"ItemPotion")) {
            if(!game.online)projectiles::applyPotion(game,stack);
            if(!game.creativeMode){--stack.count; if (stack.count <= 0) { stack = ItemStack(); stack.id = 374; stack.count = 1; }
                else { ItemStack bottle; bottle.id = 374; bottle.count = 1; giveItem(game,bottle); }}
        } else if (kindIs(item,"ItemBucketMilk")) {
            if(!game.online)game.effects.clear();
            if (!game.creativeMode) --stack.count;
            if (stack.count <= 0) { stack = ItemStack(); stack.id = 325; stack.count = 1; }
        }
    }
    stopItemUse(game,false);
}
float Controller::visualRandom(){visualRandomState_=(visualRandomState_*0x5deece66dULL+11)&((1ULL<<48)-1);return float(visualRandomState_>>24)/16777216.f;}
void Controller::itemUseEffects(Game& game,int count) {
    const auto& item=registry::item(usingStack_.id);
    if(item.useAction==2)sound(game,"random.drink",game.player.x,game.player.y,game.player.z,.5f,visualRandom()*.1f+.9f);
    if(item.useAction!=1)return;
    const float pitch=-game.player.pitch*3.1415927f/180.f,yaw=-game.player.yaw*3.1415927f/180.f;
    const double pc=javaCos(pitch),ps=javaSin(pitch),yc=javaCos(yaw),ys=javaSin(yaw);
    auto rotate=[&](double x,double y,double z){const double ry=y*pc+z*ps,rz=z*pc-y*ps;return std::array<double,3>{{x*yc+rz*ys,ry,rz*yc-x*ys}};};
    for(int i=0;i<count;++i){
        const double vx=(double(visualRandom())-.5)*.1,vy=double(visualRandom())*.1+.1;
        const auto velocity=rotate(vx,vy,0);const double localY=double(-visualRandom())*.6-.3,localX=(double(visualRandom())-.5)*.3;
        const auto point=rotate(localX,localY,.6);ParticleEvent particle;particle.type=particle.wireType=36;
        particle.x=float(game.player.x+point[0]);particle.y=float(game.player.eyeY()+point[1]);particle.z=float(game.player.z+point[2]);
        particle.offsetX=float(velocity[0]);particle.offsetY=float(velocity[1]+.05);particle.offsetZ=float(velocity[2]);particle.speed=1;
        particle.data={usingStack_.id,item.hasSubtypes?usingStack_.damage:0};particle.tick=game.ticks;game.particleEvents.push_back(std::move(particle));
    }
    while(game.particleEvents.size()>1024)game.particleEvents.pop_front();
    const float volume=.5f+.5f*float(visualRandom()>=.5f),first=visualRandom(),second=visualRandom();
    sound(game,"random.eat",game.player.x,game.player.y,game.player.z,volume,(first-second)*.2f+1.f);
}
void Controller::rightClickMouse(Game& game, const ControllerTarget& target) {
    if (mining) return;
    rightClickDelay_ = 4;
    bool useItem = true;
    if (target.entityId >= 0) {
        game.syncSelectedSlot();
        game.interactEntity(target.entityId,2,target.entityHitX,target.entityHitY,target.entityHitZ);
        game.interactEntity(target.entityId,0);
        ++unsupportedInteractions; // Entity-specific interaction results are not all ported.
    } else if (target.block && blockId(game.world.getBlock(target.x,target.y,target.z))) {
        const bool wasEmpty = held(game).empty();
        if (rightClickBlock(game,target)) { useItem = false; game.swingArm(); }
        if (wasEmpty) return;
    }
    if (useItem && !held(game).empty()) beginItemUse(game);
}
void Controller::tick(Game& game, const ControllerInput& input) {
    if (started_ && game.ticks < lastGameTick_) reset();
    started_ = true; lastGameTick_ = game.ticks;
    if (!game.joined || !game.spawned) { mining = false; progress = 0; stopItemUse(game,false); return; }
    if (input.selectedSlot >= 0) game.selectSlot(input.selectedSlot);
    game.syncSelectedSlot();
    if (rightClickDelay_ > 0) --rightClickDelay_;
    if (leftClickCounter_ > 0) --leftClickCounter_;
    if (game.itemUseCompletions != seenUseCompletions_) { finishItemUse(game); seenUseCompletions_ = game.itemUseCompletions; }
    if (itemInUse_ && (game.selectedSlot != useSlot_ || held(game).empty() || !sameItem(held(game),usingStack_) ||
        game.inventorySlotRevision[36 + static_cast<std::size_t>(useSlot_)] != useSlotRevision_)) stopItemUse(game,false);
    if (itemInUse_) {
        if (!input.useHeld) stopItemUse(game,true);
    } else if (input.focused) {
        if (input.attackPressed) clickMouse(game,input.target);
        if (input.usePressed) rightClickMouse(game,input.target);
    }
    if (input.focused && input.useHeld && rightClickDelay_ == 0 && !itemInUse_) rightClickMouse(game,input.target);
    const bool attack = input.focused && input.attackHeld;
    if (!attack) leftClickCounter_ = 0;
    if (leftClickCounter_ <= 0 && !itemInUse_) {
        if (attack && input.target.block) {
            if (blockId(game.world.getBlock(input.target.x,input.target.y,input.target.z)) && damageBlock(game,input.target)) game.swingArm();
        } else resetMining(game);
    }
    if (itemInUse_) {
        if(useTicksRemaining_<=25&&useTicksRemaining_%4==0)itemUseEffects(game,5);
        --useTicksRemaining_;
        if (!game.online && useTicksRemaining_ == 0) finishItemUse(game);
    }
}

bool Controller::clickSlot(Game& game, int slot, int button, int mode, bool notifyServer) {
    if (!game.joined || !game.spawned || !storageWindow(game)) {
        status = "This container's item rules are not ported"; ++unsupportedInteractions; return false;
    }
    std::unique_ptr<OfflineClickRollback> rollback;
    if(!game.online&&(game.offlineItemDrop||game.offlineItemDrops))rollback=std::make_unique<OfflineClickRollback>(game);
    std::vector<ItemStack> requestedDrops;
    const int windowId = game.window.open ? game.window.id : 0;
    if (currentWindow_ != windowId || seenWindowRevision_ != game.windowRevision) {
        currentWindow_ = windowId; seenWindowRevision_ = game.windowRevision; dragEvent_ = dragMode_ = 0; dragSlots_.clear();
    }
    std::vector<ItemStack> slots = game.window.open ? game.window.slots : std::vector<ItemStack>(game.inventory.begin(),game.inventory.end());
    if (game.window.open) {
        const auto& type=game.window.type;
        const std::size_t minimum=type=="minecraft:crafting_table"?46:((type=="minecraft:furnace"||type=="minecraft:villager"||type=="minecraft:anvil")?39:
            (type=="minecraft:brewing_stand"?40:(type=="minecraft:enchanting_table"?38:(type=="minecraft:beacon"?37:(type=="EntityHorse"?38:36)))));
        if (slots.size()<minimum) { status="Incomplete container slot data"; return false; }
    }
    if ((slot < 0 && slot != -999) || slot >= static_cast<int>(slots.size()) || button < 0 || mode < 0 || mode > 6) return false;
    if ((mode == 0 || mode == 1 || mode == 4 || mode == 6) && button > 1) return false;
    if (mode == 2 && button > 8) return false;
    ItemStack cursor = game.cursor, returned;
    const int gridCount=craftingSlots(game);
    const int gridWidth=gridCount==4?2:3;
    const bool merchant=game.window.open&&game.window.type=="minecraft:villager";
    const bool anvil=game.window.open&&game.window.type=="minecraft:anvil";
    const bool enchanting=game.window.open&&game.window.type=="minecraft:enchanting_table";
    int activeTrade=merchant?game.window.activeTrade:-1;
    int repairCost=anvil?game.window.properties[0]:0;
    int materialCost=anvil?game.window.repairMaterialCost:0;
    std::array<ItemStack,2> originalMerchantInputs{};
    if(merchant||anvil||enchanting){originalMerchantInputs[0]=slots[0];originalMerchantInputs[1]=slots[1];}
    auto refreshTrade=[&]{if(merchant){ItemStack result;activeTrade=crafting::merchantResult(game.window,slots,result);slots[2]=std::move(result);}};
    auto refreshRepair=[&]{if(anvil){const auto result=crafting::anvilResult(slots[0],slots[1],game.window.repairName,game.creativeMode);slots[2]=result.output;repairCost=result.cost;if(!slots[0].empty())materialCost=result.materialCost;}};
    auto canTake=[&](int index){return !(anvil&&index==2)||((game.creativeMode||game.experienceLevel>=repairCost)&&repairCost>0&&!slots[2].empty());};
    std::vector<ItemStack> originalGrid;
    if (gridCount>0) originalGrid.assign(slots.begin()+1,slots.begin()+1+gridCount);
    auto refreshGrid=[&] {
        if (gridCount>0) slots[0]=crafting::match(std::vector<ItemStack>(slots.begin()+1,slots.begin()+1+gridCount),gridWidth,gridWidth).output;
    };
    auto inventoryIndex=[&](int index) {
        return game.window.open?static_cast<int>(slots.size())-36+(index<9?27+index:index-9):(index<9?36+index:index);
    };
    auto giveWorking=[&](ItemStack stack) {
        if (stack.empty()) return;
        const int maximum=std::min(64,maxStackSize(stack));
        for (int i=0;i<36 && !stack.empty();++i) {
            auto& destination=slots[static_cast<std::size_t>(inventoryIndex(i))];
            if (destination.empty() || maximum<=1 || !sameItem(destination,stack,registry::item(stack.id).hasSubtypes)) continue;
            const int amount=std::max(0,std::min<int>(stack.count,maximum-destination.count));
            destination.count=static_cast<std::int8_t>(destination.count+amount);
            stack.count=static_cast<std::int8_t>(stack.count-amount); normalize(stack);
        }
        for (int i=0;i<36 && !stack.empty();++i) {
            auto& destination=slots[static_cast<std::size_t>(inventoryIndex(i))];
            if (!destination.empty()) continue;
            const int amount=std::min<int>(stack.count,maximum);
            destination=stack; destination.count=static_cast<std::int8_t>(amount);
            stack.count=static_cast<std::int8_t>(stack.count-amount); normalize(stack);
        }
        // The server performs any remainder drop; no extra packet is sent by
        // vanilla SlotCrafting when InventoryPlayer cannot retain a container.
        if(!stack.empty())requestedDrops.push_back(stack);
    };
    auto pickup=[&](int index,const ItemStack&) {
        if(anvil) {
            if(index<2){refreshRepair();return;}
            if(index==2){if(!game.creativeMode){game.experienceLevel-=repairCost;if(game.experienceLevel<0){game.experienceLevel=0;game.totalExperience=0;game.experienceProgress=0;}}
                slots[0]=ItemStack{};
                if(materialCost>0&&!slots[1].empty()&&slots[1].count>materialCost)slots[1].count=static_cast<std::int8_t>(slots[1].count-materialCost);
                else slots[1]=ItemStack{};
                refreshRepair();repairCost=0;return;}
        }
        if(merchant) {
            if(index<2){refreshTrade();return;}
            if(index==2&&activeTrade>=0&&static_cast<std::size_t>(activeTrade)<game.window.trades.size()) {
                auto& offer=game.window.trades[static_cast<std::size_t>(activeTrade)];
                auto trade=[&](ItemStack& first,ItemStack& second){
                    if(first.empty()||first.id!=offer.first.id)return false;
                    if(!offer.second.empty()&&(!second.empty()&&second.id==offer.second.id)){
                        first.count=static_cast<std::int8_t>(first.count-offer.first.count);second.count=static_cast<std::int8_t>(second.count-offer.second.count);return true;}
                    if(offer.second.empty()&&second.empty()){first.count=static_cast<std::int8_t>(first.count-offer.first.count);return true;}return false;};
                if(trade(slots[0],slots[1])||trade(slots[1],slots[0])){++offer.uses;offer.disabled=offer.uses>=offer.maxUses;normalize(slots[0]);normalize(slots[1]);refreshTrade();}
                return;
            }
        }
        if (!craftingOutput(game,index)) return;
        const auto result=crafting::match(std::vector<ItemStack>(slots.begin()+1,slots.begin()+1+gridCount),gridWidth,gridWidth);
        for (int i=0;i<gridCount;++i) {
            auto& ingredient=slots[static_cast<std::size_t>(i+1)];
            if (!ingredient.empty()) { --ingredient.count; normalize(ingredient); }
            const auto& remainder=result.remaining[static_cast<std::size_t>(i)];
            if (!remainder.empty()) {
                if (ingredient.empty()) ingredient=remainder;
                else giveWorking(remainder);
            }
        }
        refreshGrid();
    };
    auto takeStack=[&](int index,int count) {
        auto& source=slots[static_cast<std::size_t>(index)];
        if (source.empty()) return ItemStack{};
        ItemStack result=source;
        // InventoryCraftResult.decrStackSize ignores the requested amount.
        const bool whole=craftingOutput(game,index) || ((merchant||anvil)&&index==2) || (game.window.open && game.window.type=="minecraft:brewing_stand" && index<4);
        const int amount=whole?source.count:std::min<int>(count,source.count);
        if (game.window.open && game.window.type=="minecraft:beacon" && index==0 && amount<source.count) result.nbt.clear();
        result.count=static_cast<std::int8_t>(amount); source.count=static_cast<std::int8_t>(source.count-amount); normalize(source);
        if(merchant&&index<2)refreshTrade();
        if(anvil&&index<2)refreshRepair();
        return result;
    };
    auto resetDrag = [&] { dragEvent_ = 0; dragSlots_.clear(); };
    auto merge = [&](ItemStack& stack, int begin, int end, bool reverse) {
        bool changed = false; const int step = reverse ? -1 : 1;
        const auto& info = registry::item(stack.id);
        if (info.maxStack > 1) for (int i = reverse ? end-1 : begin; !stack.empty() && i >= begin && i < end; i += step) {
            auto& destination = slots[static_cast<std::size_t>(i)];
            if (destination.empty() || !sameItem(destination,stack,info.hasSubtypes)) continue;
            const int amount = std::min<int>(stack.count,info.maxStack-destination.count);
            if (amount <= 0) continue;
            destination.count = static_cast<std::int8_t>(destination.count+amount); stack.count = static_cast<std::int8_t>(stack.count-amount); changed = true;
            normalize(stack);
        }
        for (int i = reverse ? end-1 : begin; !stack.empty() && i >= begin && i < end; i += step) {
            auto& destination = slots[static_cast<std::size_t>(i)];
            if (!destination.empty()) continue;
            destination = stack; stack = ItemStack(); changed = true; break;
        }
        return changed;
    };
    if (mode == 5) {
        const int previous = dragEvent_; dragEvent_ = button & 3;
        if (((previous != 1 || dragEvent_ != 2) && previous != dragEvent_) || cursor.empty()) resetDrag();
        else if (dragEvent_ == 0) {
            dragMode_ = (button >> 2) & 3;
            if (dragMode_ == 0 || dragMode_ == 1 || (dragMode_ == 2 && game.creativeMode)) { dragEvent_ = 1; dragSlots_.clear(); }
            else resetDrag();
        } else if (dragEvent_ == 1) {
            if (slot >= 0 && slotAccepts(game,slot,cursor) && (slots[static_cast<std::size_t>(slot)].empty() ||
                sameItem(slots[static_cast<std::size_t>(slot)],cursor)) && cursor.count > static_cast<int>(dragSlots_.size())) dragSlots_.insert(slot);
        } else if (dragEvent_ == 2) {
            const ItemStack original = cursor; int remaining = cursor.count;
            for (const auto index : dragSlots_) {
                auto& destination = slots[static_cast<std::size_t>(index)];
                if (!slotAccepts(game,index,cursor) || (!destination.empty() && !sameItem(destination,cursor)) || cursor.count < static_cast<int>(dragSlots_.size())) continue;
                const int old = destination.empty() ? 0 : destination.count;
                int count = dragMode_ == 0 ? original.count / static_cast<int>(dragSlots_.size()) : (dragMode_ == 1 ? 1 : maxStackSize(original));
                count = std::min(count+old,std::min(maxStackSize(original),slotLimit(game,index,original)));
                remaining -= count-old; destination = original; destination.count = static_cast<std::int8_t>(count);
            }
            cursor.count = static_cast<std::int8_t>(std::clamp(remaining,0,127)); normalize(cursor); resetDrag();
        } else resetDrag();
    } else if (dragEvent_ != 0) resetDrag();
    else if ((mode == 0 || mode == 1) && (button == 0 || button == 1)) {
        if (slot == -999) {
            if (!cursor.empty()) {auto dropped=cursor;if(button==1)dropped.count=1;requestedDrops.push_back(dropped);if (button == 0) cursor = ItemStack(); else { --cursor.count; normalize(cursor); } }
        } else if (mode == 1) {
            // Container.slotClick retries transferStackInSlot when the result
            // slot refills with the same item (e.g. repeated shift crafting).
            for (int retry=0;retry<512;++retry) {
                auto& source=slots[static_cast<std::size_t>(slot)];
                if (source.empty()||!canTake(slot)) break;
                const ItemStack original=source; bool changed=false;
                if (!game.window.open) {
                    const int armor=registry::item(source.id).armorSlot;
                    if (slot==0) changed=merge(source,9,45,true);
                    else if (slot>=1 && slot<9) changed=merge(source,9,45,false);
                    else if (armor>=0 && slots[static_cast<std::size_t>(5+armor)].empty()) changed=merge(source,5+armor,6+armor,false);
                    else changed=slot<36?merge(source,36,45,false):merge(source,9,36,false);
                } else {
                    const auto& type=game.window.type;
                    const int containerSlots=static_cast<int>(slots.size())-36;
                    if (type=="minecraft:crafting_table") {
                        if (slot==0) changed=merge(source,10,46,true);
                        else if (slot<10) changed=merge(source,10,46,false);
                        else changed=slot<37?merge(source,37,46,false):merge(source,10,37,false);
                    } else if (type=="minecraft:furnace") {
                        if (slot==2) changed=merge(source,3,39,true);
                        else if (slot<2) changed=merge(source,3,39,false);
                        else if (!crafting::smeltingResult(source).empty()) changed=merge(source,0,1,false);
                        else if (crafting::fuelBurnTime(source)>0) changed=merge(source,1,2,false);
                        else changed=slot<30?merge(source,30,39,false):merge(source,3,30,false);
                    } else if (type=="minecraft:brewing_stand") {
                        if (slot<4) changed=merge(source,4,40,true);
                        else if (slots[3].empty() && crafting::potionIngredient(source)) changed=merge(source,3,4,false);
                        else if (source.id==373 || source.id==374) changed=merge(source,0,3,false);
                        else changed=slot<31?merge(source,31,40,false):merge(source,4,31,false);
                    } else if (type=="minecraft:enchanting_table") {
                        if (slot<2) changed=merge(source,2,38,true);
                        else if (source.id==351 && source.damage==4) changed=merge(source,1,2,true);
                        else if (slots[0].empty() && slotAccepts(game,0,source)) {
                            slots[0]=source;
                            if (!source.nbt.empty() && source.nbt[0]!=0 && source.count==1) source=ItemStack{};
                            else { slots[0].count=1; slots[0].nbt.clear(); --source.count; normalize(source); }
                            changed=true;
                        }
                    } else if (type=="minecraft:beacon") {
                        if (slot==0) changed=merge(source,1,37,true);
                        else if (slots[0].empty() && slotAccepts(game,0,source) && source.count==1) changed=merge(source,0,1,false);
                        else changed=slot<28?merge(source,28,37,false):merge(source,1,28,false);
                    } else if(type=="minecraft:villager") {
                        if(slot==2)changed=merge(source,3,39,true);
                        else if(slot<2)changed=merge(source,3,39,false);
                        else changed=slot<30?merge(source,30,39,false):merge(source,3,30,false);
                    } else if(type=="minecraft:anvil") {
                        if(slot==2)changed=merge(source,3,39,true);
                        else if(slot<2)changed=merge(source,3,39,false);
                        else changed=merge(source,0,2,false);
                    } else if (type=="EntityHorse") {
                        if (slot<containerSlots) changed=merge(source,containerSlots,static_cast<int>(slots.size()),true);
                        else if (slots[1].empty() && slotAccepts(game,1,source)) changed=merge(source,1,2,false);
                        else if (slotAccepts(game,0,source)) changed=merge(source,0,1,false);
                        else if (containerSlots>2) changed=merge(source,2,containerSlots,false);
                    } else changed=slot<containerSlots?merge(source,containerSlots,static_cast<int>(slots.size()),true):merge(source,0,containerSlots,false);
                }
                if (!changed) break;
                if (retry==0) returned=original;
                pickup(slot,original);
                if (gridCount>0 && slot>=1 && slot<=gridCount) refreshGrid();
                const auto& next=slots[static_cast<std::size_t>(slot)];
                if (next.empty() || next.id!=original.id) break;
            }
        } else {
            auto& source = slots[static_cast<std::size_t>(slot)]; returned = source;
            if (source.empty()) {
                if (!cursor.empty() && slotAccepts(game,slot,cursor)) {
                    const int count = std::min(button == 0 ? static_cast<int>(cursor.count) : 1,slotLimit(game,slot,cursor));
                    source = cursor; source.count = static_cast<std::int8_t>(count); cursor.count = static_cast<std::int8_t>(cursor.count-count); normalize(cursor);
                }
            } else if(!canTake(slot)) {
                // The original slotClick still returns the old result stack.
            } else if (cursor.empty()) {
                const int count = button == 0 ? source.count : (source.count+1)/2;
                cursor=takeStack(slot,count); pickup(slot,cursor);
            } else if (slotAccepts(game,slot,cursor)) {
                if (sameItem(source,cursor)) {
                    const int count = std::max(0,std::min({button == 0 ? static_cast<int>(cursor.count) : 1,
                        slotLimit(game,slot,cursor)-source.count,maxStackSize(cursor)-source.count}));
                    cursor.count = static_cast<std::int8_t>(cursor.count-count); source.count = static_cast<std::int8_t>(source.count+count); normalize(cursor);
                } else if (cursor.count <= slotLimit(game,slot,cursor)) std::swap(source,cursor);
            } else if (source.id == cursor.id && maxStackSize(cursor) > 1 &&
                (!registry::item(source.id).hasSubtypes || source.damage == cursor.damage) && sameTags(source,cursor) && source.count+cursor.count <= maxStackSize(cursor)) {
                const auto taken=takeStack(slot,source.count); cursor.count=static_cast<std::int8_t>(cursor.count+taken.count); pickup(slot,taken);
            }
        }
    } else if (mode == 2 && slot >= 0 && canTake(slot)) {
        const auto hotbar = static_cast<std::size_t>(game.window.open ? static_cast<int>(slots.size())-9+button : 36+button);
        auto& source = slots[static_cast<std::size_t>(slot)]; auto& hot = slots[hotbar];
        const bool playerSlot = !game.window.open ? slot>=5 : slot >= static_cast<int>(slots.size())-36;
        bool allowed = hot.empty() || (playerSlot && slotAccepts(game,slot,hot));
        int empty = -1;
        if (!allowed) for (int i = 0; i < 36; ++i) {
            const int index = game.window.open ? static_cast<int>(slots.size())-36+(i < 9 ? 27+i : i-9) : (i < 9 ? 36+i : i);
            if (slots[static_cast<std::size_t>(index)].empty()) { empty = index; allowed = true; break; }
        }
        if (!source.empty() && allowed) {
            const ItemStack old = hot; const ItemStack taken=source; hot = source;
            if ((!playerSlot || !slotAccepts(game,slot,old)) && !old.empty()) {
                if (empty >= 0) {
                    ItemStack displaced = old;
                    if (damageable(displaced) && displaced.damage > 0) {
                        slots[static_cast<std::size_t>(empty)] = displaced;
                        displaced = ItemStack();
                    } else {
                        const int maximum = maxStackSize(displaced);
                        // InventoryPlayer.addItemStackToInventory first merges
                        // partial stacks in hotbar-first order, including the
                        // item just copied into the chosen hotbar slot.
                        for (int i = 0; i < 36 && !displaced.empty(); ++i) {
                            const int index = game.window.open ? static_cast<int>(slots.size())-36+(i < 9 ? 27+i : i-9) : (i < 9 ? 36+i : i);
                            auto& destination = slots[static_cast<std::size_t>(index)];
                            if (destination.empty() || maximum <= 1 || !sameItem(destination,displaced,registry::item(displaced.id).hasSubtypes)) continue;
                            const int count = std::max(0,std::min<int>(displaced.count,std::min(64,maximum)-destination.count));
                            destination.count = static_cast<std::int8_t>(destination.count+count);
                            displaced.count = static_cast<std::int8_t>(displaced.count-count); normalize(displaced);
                        }
                        for (int i = 0; i < 36 && !displaced.empty(); ++i) {
                            const int index = game.window.open ? static_cast<int>(slots.size())-36+(i < 9 ? 27+i : i-9) : (i < 9 ? 36+i : i);
                            auto& destination = slots[static_cast<std::size_t>(index)];
                            if (!destination.empty()) continue;
                            const int count = std::min<int>(displaced.count,std::min(64,maximum));
                            destination = displaced; destination.count = static_cast<std::int8_t>(count);
                            displaced.count = static_cast<std::int8_t>(displaced.count-count); normalize(displaced);
                        }
                    }
                    source = ItemStack();
                }
            } else source = old;
            pickup(slot,taken);
        } else if (source.empty() && !hot.empty() && slotAccepts(game,slot,hot)) { source = hot; hot = ItemStack(); }
    } else if (mode == 3 && slot >= 0 && game.creativeMode && cursor.empty() && !slots[static_cast<std::size_t>(slot)].empty()) {
        cursor = slots[static_cast<std::size_t>(slot)]; cursor.count = static_cast<std::int8_t>(maxStackSize(cursor));
    } else if (mode == 4 && slot >= 0 && cursor.empty() && canTake(slot)) {
        auto& source=slots[static_cast<std::size_t>(slot)];
        if (!source.empty()) { const auto taken=takeStack(slot,button==0?1:source.count); pickup(slot,taken);requestedDrops.push_back(taken); }
    } else if (mode == 6 && slot >= 0 && !cursor.empty() && (slots[static_cast<std::size_t>(slot)].empty()||!canTake(slot))) {
        for (int pass = 0; pass < 2; ++pass) for (int i = button == 0 ? 0 : static_cast<int>(slots.size())-1;
            i >= 0 && i < static_cast<int>(slots.size()) && cursor.count < maxStackSize(cursor); i += button == 0 ? 1 : -1) {
            if (craftingOutput(game,i)||!canTake(i)) continue;
            auto& source = slots[static_cast<std::size_t>(i)];
            if (source.empty() || !sameItem(source,cursor) || (pass == 0 && source.count == maxStackSize(source))) continue;
            const int count = std::min(maxStackSize(cursor)-cursor.count,static_cast<int>(source.count));
            const auto taken=takeStack(i,count); cursor.count=static_cast<std::int8_t>(cursor.count+count); pickup(i,taken);
        }
    }
    if (gridCount>0) {
        bool changed=false;
        for (int i=0;i<gridCount;++i) if (!sameItem(originalGrid[static_cast<std::size_t>(i)],slots[static_cast<std::size_t>(i+1)]) ||
            originalGrid[static_cast<std::size_t>(i)].count!=slots[static_cast<std::size_t>(i+1)].count) changed=true;
        if (changed) refreshGrid();
    }
    bool windowInputsChanged=false;
    if(merchant||anvil||enchanting){for(int i=0;i<2;++i)if(!sameItem(originalMerchantInputs[static_cast<std::size_t>(i)],slots[static_cast<std::size_t>(i)])||
        originalMerchantInputs[static_cast<std::size_t>(i)].count!=slots[static_cast<std::size_t>(i)].count)windowInputsChanged=true;}
    if(merchant){if(windowInputsChanged)refreshTrade();game.window.activeTrade=activeTrade;}
    if(anvil){if(windowInputsChanged)refreshRepair();game.window.properties[0]=repairCost;game.window.repairMaterialCost=materialCost;}
    if(rollback&&!requestedDrops.empty()){
        const bool accepted=game.offlineItemDrops?game.offlineItemDrops(requestedDrops,false):requestedDrops.size()==1&&game.offlineItemDrop&&game.offlineItemDrop(requestedDrops.front(),false);
        if(!accepted){status="Offline item drop was rejected";return false;}
    }
    if (game.window.open) {
        game.window.slots = slots;
        if (slots.size() >= 36) for (std::size_t i = 0; i < 36; ++i) game.inventory[9+i] = slots[slots.size()-36+i];
    } else std::copy(slots.begin(),slots.end(),game.inventory.begin());
    game.cursor = cursor;
    if(enchanting&&windowInputsChanged)crafting::refreshEnchantment(game);
    if(rollback)rollback->accepted=true;
    if(!notifyServer)return true;
    const auto transaction = game.nextWindowTransaction();
    return !game.online || game.sendWindowClick(windowId,slot,button,mode,transaction,returned);
}
bool Controller::closeWindow(Game& game) { dragEvent_ = 0; dragSlots_.clear(); return game.closeContainer(); }
bool Controller::selectTrade(Game& game,int index) {
    if(!game.window.open||game.window.type!="minecraft:villager"||index<0||static_cast<std::size_t>(index)>=game.window.trades.size())return false;
    game.window.selectedTrade=index;crafting::refreshMerchant(game);
    return !game.online||game.sendCustomPayload("MC|TrSel",net::Writer().i32(index).data);
}
bool Controller::renameItem(Game& game,const std::string& name) {
    if(!game.window.open||game.window.type!="minecraft:anvil")return false;
    try{net::Writer checked;checked.string(name,30);game.window.repairName=name;crafting::refreshAnvil(game);return !game.online||game.sendCustomPayload("MC|ItemName",checked.data);}
    catch(const net::ProtocolError&){return false;}
}
bool Controller::enchantItem(Game& game,int choice) {
    if(!game.window.open||game.window.type!="minecraft:enchanting_table"||game.window.slots.size()<2||choice<0||choice>2)return false;
    const auto found=game.window.properties.find(choice);const int cost=found==game.window.properties.end()?0:found->second;
    const auto& lapis=game.window.slots[1];
    if((lapis.empty()||lapis.count<choice+1)&&!game.creativeMode)return false;
    if(cost<=0||game.window.slots[0].empty()||(!game.creativeMode&&(game.experienceLevel<choice+1||game.experienceLevel<cost)))return false;
    return !game.online||game.sendWindowButton(choice);
}

} // namespace mc
