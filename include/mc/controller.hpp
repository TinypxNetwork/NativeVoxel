#pragma once

#include "mc/game.hpp"

#include <cstdint>
#include <set>
#include <string>

namespace mc {

struct ControllerTarget {
    bool block{};
    int x{}, y{}, z{}, face{};
    // Block-local hit coordinates, before the C08 multiplication by 16.
    float hitX{.5f}, hitY{.5f}, hitZ{.5f};
    int entityId{-1};
    // Entity-relative hit coordinates for C02 INTERACT_AT.
    float entityHitX{}, entityHitY{}, entityHitZ{};
};

struct ControllerInput {
    ControllerTarget target;
    bool attackHeld{}, attackPressed{}, useHeld{}, usePressed{};
    bool focused{true};
    int selectedSlot{-1}; // -1 preserves Game::selectedSlot.
};

class Controller {
public:
    float progress{};
    ControllerTarget currentTarget{false,-1,-1,-1,0};
    bool mining{};
    std::string status;
    std::uint64_t unsupportedInteractions{};

    // Call once per game tick before Game::tick. Copy usingItem() into
    // mc::Input::usingItem so movement gets the vanilla slowdown in that tick.
    void tick(Game& game, const ControllerInput& input);
    void reset();
    bool usingItem() const { return itemInUse_; }
    int useTicksRemaining() const { return useTicksRemaining_; }
    int rightClickDelay() const { return rightClickDelay_; }
    bool clickSlot(Game& game, int slot, int button, int mode = 0, bool notifyServer = true);
    bool closeWindow(Game& game);
    bool selectTrade(Game& game,int index);
    bool renameItem(Game& game,const std::string& name);
    bool enchantItem(Game& game,int choice);

    // Source-based hardness calculation is public for differential tests/UI.
    float relativeBlockHardness(const Game& game, int x, int y, int z) const;
    int maxStackSize(const ItemStack& item) const;
    static int enchantment(const ItemStack& item, int id);

private:
    ItemStack hittingItem_, usingStack_;
    int hitDelay_{}, leftClickCounter_{}, rightClickDelay_{};
    float stepSoundCounter_{};
    int useTicksRemaining_{}, useSlot_{-1};
    bool itemInUse_{};
    std::uint64_t useSlotRevision_{}, seenUseCompletions_{}, seenWindowRevision_{};
    std::uint64_t randomState_{(189u ^ 0x5deece66dull) & ((1ull << 48) - 1)};
    std::uint64_t visualRandomState_{(919u ^ 0x5deece66dull) & ((1ull << 48) - 1)};
    int currentWindow_{-1};
    std::uint16_t nextTransaction_{};
    int dragEvent_{}, dragMode_{};
    std::set<int> dragSlots_;
    std::uint64_t lastGameTick_{};
    bool started_{};

    bool sameMiningTarget(const Game& game, const ControllerTarget& target) const;
    bool clickBlock(Game& game, const ControllerTarget& target);
    bool damageBlock(Game& game, const ControllerTarget& target);
    bool destroyBlock(Game& game, const ControllerTarget& target);
    void resetMining(Game& game);
    void clickMouse(Game& game, const ControllerTarget& target);
    void rightClickMouse(Game& game, const ControllerTarget& target);
    bool rightClickBlock(Game& game, const ControllerTarget& target);
    bool beginItemUse(Game& game);
    void stopItemUse(Game& game, bool notifyServer);
    void finishItemUse(Game& game);
    void itemUseEffects(Game& game,int count);
    float visualRandom();
    void damageItem(Game& game, ItemStack& item, int amount);
    int randomInt(int bound);
    void predictAttack(Game& game, int entityId);
};

} // namespace mc
