#pragma once

#include <set>
#include <string>
#include <vector>

namespace mc {
class Settings;
class Renderer;
class Window;
class I18n;

enum class OptionsPage { Controls, Sounds, Main, Video, Skin, Chat, Snooper, Stream };
enum class OptionsAction { None, Done, ResetAll, BindKey, ResetKey, InvertMouse, Sensitivity, Touchscreen, SoundLevel,
    Fov, RealmsNotifications, Difficulty, OpenSkin, SuperSecret, OpenSounds, OpenStream, OpenVideo, OpenControls,
    OpenLanguage, OpenChat, OpenResourcePacks, OpenSnooper, SkinPart, ChatSetting, SnooperEnabled, VideoSetting, StreamSetting };
struct OptionsWidget {
    OptionsAction action = OptionsAction::None;
    int index = -1; // Settings.keys index or SoundCategory ordinal
    int x = 0, y = 0, width = 0, height = 20;
    bool enabled = true, slider = false, listRow = false;
    float value = 0;
    std::string text;
    bool hit(float px,float py) const noexcept {return px>=x&&py>=y&&px<x+width&&py<y+height;}
};
struct OptionsRow {
    int keyIndex = -1; // -1 denotes a translated category heading
    int y = 0;
    std::string label;
};
struct OptionsLayout {
    int width = 0, height = 0, listTop = 63, listBottom = 0;
    float scroll = 0, maxScroll = 0;
    int scrollbarX = 0, thumbY = 0, thumbHeight = 0;
    std::vector<OptionsRow> rows;
    std::vector<OptionsWidget> widgets;
};
struct OptionsInput {
    int width = 320, height = 240; // logical GUI pixels, not framebuffer pixels
    float x = 0, y = 0;
    bool leftPressed = false, leftDown = false, leftReleased = false;
    bool focused = true;
    int wheel = 0; // positive scrolls upward; sign follows Window::wheel
    std::vector<int> pressedKeys; // event order; Win32 keys including mouse and left/right modifiers
};

class OptionsScreen {
public:
    // The caller owns beginUI/endUI, applies changed Settings, and saves them.
    bool draw(OptionsPage, Settings&, Renderer&, Window&, I18n&, float scale);
    bool capturingKey() const noexcept {return !capturedKey_.empty();}
    void reset();
    // Keep actions without an implemented main-loop binding visibly disabled.
    void setBindingEnabled(const std::string& name,bool enabled);
    // Deterministic, device-free input and hit-layout interfaces for tests.
    bool interact(OptionsPage,Settings&,const OptionsInput&,const I18n* language=nullptr);
    OptionsLayout layout(OptionsPage,const Settings&,int width,int height,const I18n* language=nullptr) const;
    static bool conflict(const Settings&,int keyIndex);
    float scroll() const noexcept {return scroll_;}
    OptionsAction takeAction() {auto value=action_;action_=OptionsAction::None;return value;}
    bool takePressSound() {bool value=pressSound_;pressSound_=false;return value;}
    void setWorldContext(bool active,bool remote,int difficulty,bool hardcore=false) {
        worldActive_=active;remoteWorld_=remote;difficulty_=difficulty;hardcore_=hardcore;
    }
private:
    std::set<std::string> disabledBindings_;
    std::string capturedKey_;
    OptionsPage page_ = OptionsPage::Controls;
    bool initialized_ = false, draggingScroll_ = false, draggingList_ = false;
    float scroll_ = 0, dragY_ = 0, dragScroll_ = 0;
    OptionsAction slider_ = OptionsAction::None;
    int sliderIndex_ = -1;
    OptionsAction action_ = OptionsAction::None;
    bool pressSound_=false,worldActive_=false,remoteWorld_=false,hardcore_=false;
    int difficulty_=2;
};
} // namespace mc
