#pragma once

#include "mc/chat_text.hpp"
#include "mc/game.hpp"
#include "mc/i18n.hpp"
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mc { class Renderer; }
namespace mc::hud {

struct Rect { float x{},y{},width{},height{};bool contains(float px,float py) const { return px>=x&&py>=y&&px<x+width&&py<y+height; } };
struct Draw {
    enum class Kind { Rectangle,Text,Sprite,PlayerHead } kind{Kind::Text};
    Rect bounds;
    std::string text,texture,uuid;
    float scale{1},u{},v{},sourceWidth{},sourceHeight{};
    std::uint32_t rgba{0xffffffff};
    bool shadow{true},hat{true},flipped{};
};
struct Hit {Rect bounds;chat::Style style;std::string text;std::size_t component{};std::uint64_t messageTick{};};
struct Layout {
    std::vector<Draw> draws;
    std::vector<Hit> hits;
    Rect bounds;
    void append(Layout other);
    const Hit* hit(float x,float y) const;
};
struct ChatOptions {
    bool open{},hidden{},colors{true},systemOnly{};
    float widthSetting{1},focusedHeight{1},unfocusedHeight{.44366196f},scale{1},opacity{1};
    int scroll{};
    std::string input;
    float cursorX{},selectionStartX{},selectionEndX{};
    bool cursorVisible{},cursorAtEnd{true};
    std::string completionHint;
};
struct TabOptions {bool visible{},showHeads{};std::uint64_t nowMilliseconds{};};
struct NameTagInput {
    bool player{true},sameAsLocalPlayer{},sameAsViewEntity{},guiEnabled{true},ridden{},invisible{},viewerSpectator{},sneaking{},child{};
    std::string entityTeam,viewerTeam;
    std::string visibility{"always"};
    bool seeFriendlyInvisibles{};
    double distanceSquared{};
    float entityHeight{1.8f};
};
struct NameTagPass {bool depthTest{},depthWrite{},background{};float alpha{};};
struct NameTagPolicy {bool visible{};float bodyAlpha{1},scale{.02666667f},heightOffset{},pixelOffsetY{};std::vector<NameTagPass> passes;};
NameTagPolicy nameTagPolicy(const NameTagInput& input);
const ScoreTeam* playerTeam(const Game& game,const std::string& name,std::string* registeredName=nullptr);
std::string teamName(const Game& game,const std::string& name);
int titleAlpha(const Game& game,float partialTick);
int pingIcon(int milliseconds);

class Hud {
public:
    bool load(const std::string& assets,const std::string& language,std::string& error);
    bool registerTextures(Renderer& renderer) const;
    void setUnicode(bool value) { unicode_=value; }
    chat::Metrics metrics() const { return {fontReady_?&font_:nullptr,unicode_,6}; }
    chat::Document text(const std::string& json) const;
    Layout chat(const Game& game,int width,int height,const ChatOptions& options) const;
    Layout sidebar(const Game& game,int width,int height,const std::string& localName={}) const;
    Layout tab(const Game& game,int width,const TabOptions& options);
    Layout titles(const Game& game,int width,int height,float partialTick) const;
    Layout experience(const Game& game,int width,int height) const;
    Layout actionBar(const Game& game,int width,int height,float partialTick) const;
    Layout tooltip(const Hit& hit,float mouseX,float mouseY,int width,int height) const;
    Layout tooltip(const Hit& hit,float mouseX,float mouseY,int width,int height,bool advanced) const;
    // Layout coordinates are Minecraft GUI pixels. Pass the actual GUI scale
    // here; hit-test inputs are physical mouse coordinates divided by that scale.
    static void draw(Renderer& renderer,const Layout& layout,float pixelScale=1,
                     const std::function<void(const Draw&,float)>& playerHead={});
    void rememberSent(std::string text);
    const std::vector<std::string>& sentHistory() const { return sent_; }
    static int chatWidth(float setting);
    static int chatHeight(float setting);
private:
    struct Hearts {int last{},displayed{};std::uint64_t changeAt{},blinkUntil{},openedAt{};};
    I18n translations_;
    UnicodeFont font_;
    bool fontReady_{},unicode_{},tabOpen_{};
    std::string assets_;
    std::uint64_t tabOpenedAt_{};
    std::map<std::string,Hearts> hearts_;
    std::vector<std::string> sent_;
    struct CachedChatLine { mc::chat::Line line;std::uint64_t tick{}; };
    mutable std::vector<CachedChatLine> chatLines_;
    mutable const Game* chatGame_{};
    mutable std::uint64_t chatRevision_{~std::uint64_t(0)};
    mutable std::size_t chatCount_{};
    mutable int chatWrapWidth_{};
    mutable bool chatColors_{},chatUnicode_{},chatSystemOnly_{};
};

} // namespace mc::hud
