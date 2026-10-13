#pragma once

#include <set>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mc {
class Renderer;
class Game;
class Controller;
class Window;
class I18n;
class Settings;
struct ItemStack;

struct InventorySlot {
    int index = -1;
    int x = 0, y = 0; // original GUI pixels, relative to the panel origin
};
struct InventoryLayout {
    std::string texture = "inventory";
    int width = 176, height = 166;
    int containerSlots = 0, chestRows = 0;
    int inventoryLabelX = 8, inventoryLabelY = 72;
    bool player = false, supported = true;
    std::vector<InventorySlot> slots;
    // Returns -1 within the panel but outside a slot, or -999 outside the panel.
    int hit(float x, float y) const noexcept;
};

struct InventoryInput {
    float x = 0, y = 0; // panel-relative original GUI pixels
    bool leftPressed = false, rightPressed = false, middlePressed = false;
    bool leftDown = false, rightDown = false;
    bool leftReleased = false, rightReleased = false;
    bool shift = false, control = false, dropPressed = false;
    int numberSlot = -1;
    double timeSeconds = 0;
    std::u32string textInput;
    bool backspace = false, deletePressed = false, selectAll = false;
    bool leftKey = false, rightKey = false, homeKey = false, endKey = false;
};

enum class InventoryAction { AnvilName, PreviousTrade, NextTrade, Enchant, BeaconPrimary, BeaconSecondary, BeaconConfirm, BeaconCancel };
struct InventoryControl {
    InventoryAction action = InventoryAction::AnvilName;
    int value = 0, x = 0, y = 0, width = 0, height = 0;
    bool enabled = true, selected = false;
    bool hit(float px,float py) const noexcept { return px>=x&&py>=y&&px<x+width&&py<y+height; }
};

class InventoryScreen {
public:
    bool load(const std::string& assetsDirectory, std::string& error);
    bool draw(Renderer&, Game&, Controller&, Window&, I18n&, float scale);
    void reset();
    void setBindings(const Settings* bindings){bindings_=bindings;}
    void setAdvancedTooltips(bool value){advancedTooltips_=value;}
    static InventoryLayout layout(const Game& game);
    static std::vector<InventoryControl> controls(const Game& game);
    // Public deterministic input path; all container mutations go through Controller.
    void interact(Game&, Controller&, const InventoryInput&);
    std::string itemName(const ItemStack&, const I18n&) const;
    std::vector<std::string> itemTooltip(const ItemStack&,const I18n&,bool advanced=false) const;
    bool creativeAvailable() const noexcept;
    std::size_t creativeItemCount(int tab = 5) const noexcept;
    void selectCreativeTab(int tab);
    const std::string& renameText() const noexcept { return renameText_; }
    bool renameFocused() const noexcept { return renameFocused_; }
    bool requestedClose() const noexcept { return closeRequested_; }
private:
    std::string assetsDirectory_;
    const Settings* bindings_=nullptr;
    bool advancedTooltips_=false;
    int windowId_ = -1;
    int pressedSlot_ = -1, pressedButton_ = -1;
    int lastClickSlot_ = -1, lastClickButton_ = -1;
    double lastClickTime_ = -1;
    bool placing_ = false, texturesRegistered_ = false;
    std::set<int> dragSlots_;
    struct Name { std::string key, fallback; };
    std::unordered_map<unsigned,Name> names_;
    struct CreativeData;
    std::shared_ptr<CreativeData> creative_;
    void interactCreative(Game&, Controller&, const InventoryInput&);
    bool drawCreative(Renderer&, Game&, Controller&, Window&, I18n&, float scale);
    void syncAnvil(Game&, Controller&, const I18n* language = nullptr);
    bool interactControls(Game&, Controller&, const InventoryInput&);
    std::string renameText_, renameOriginal_, anvilInputSignature_;
    std::size_t renameCursor_ = 0;
    int anvilWindowId_ = -1;
    bool renameFocused_ = false, renameSelectedAll_ = false, closeRequested_ = false;
    std::array<std::uint8_t,256> galacticWidths_{};
    bool galacticFont_ = false;
};
} // namespace mc
