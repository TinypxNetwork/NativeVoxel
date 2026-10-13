#pragma once

#include "mc/render_assets.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mc::chat {

struct ClickEvent { std::string action,value; };
struct HoverEvent { std::string action,valueJson; };
struct Style {
    char color{}; // 0 inherits the draw color; otherwise 0..9,a..f,r.
    bool bold{},italic{},underlined{},strikethrough{},obfuscated{};
    std::optional<ClickEvent> click;
    std::optional<HoverEvent> hover;
    std::optional<std::string> insertion;
    std::string formatting() const;
    std::uint32_t rgb(std::uint32_t fallback=0xffffff) const;
};
struct Span { std::string text;Style style;std::size_t component{}; };
struct Document {
    std::vector<Span> spans;
    bool valid{true};
    std::string error;
    std::string plain(bool removeFormatting=true) const;
    std::string formatted() const;
};
using Translator=std::function<std::string(const std::string&)>;
using ScoreResolver=std::function<std::string(const std::string&,const std::string&)>;

// Uses the original component inheritance/extra/translate/style/event rules.
// Malformed JSON becomes a visible literal with valid=false; it never executes
// click events. The caller decides what a user's click should do.
Document parse(const std::string& json,const Translator& translate={},const ScoreResolver& score={});
Document literal(const std::string& text,const Style& style={});
std::string stripFormatting(const std::string& text);
std::string escapeJsonString(const std::string& text);

struct Metrics {
    const UnicodeFont* font{};
    bool unicode{};
    int fallbackWidth{6};
    int charWidth(std::uint16_t codepoint) const;
    int width(const std::string& formatted) const;
    std::string trim(const std::string& formatted,int width) const;
};
struct Line {
    std::vector<Span> spans;
    int width{};
    std::string formatted() const;
    std::string plain() const;
};
std::vector<Line> wrap(const Document& text,int width,const Metrics& metrics,
                      bool trimLeadingSpace=false,bool colors=true);
std::vector<std::string> wrapFormatted(const std::string& text,int width,const Metrics& metrics);

struct ActionRequest {
    enum class Kind { None,OpenUrl,RunCommand,SuggestCommand,ChangePage,InsertText } kind{};
    std::string value,error;
};
ActionRequest click(const Style& style,bool shift=false,bool allowLinks=true);

} // namespace mc::chat
