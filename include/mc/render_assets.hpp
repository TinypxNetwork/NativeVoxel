#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mc {

struct GlyphMetrics {
    int page = -2; // -1 = ascii.rgba, 0..255 = unicode_page_XX.rgba, -2 = no quad
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    float width = 0, height = 7.99f, advance = 0;
    int measuredWidth = 0; // FontRenderer.getCharWidth; section-sign formatting marker is -1
    bool correctedLegacyBounds = false; // packed byte >= 0x80, see docs/RENDER_ASSETS.md
};

class UnicodeFont {
public:
    bool load(const std::string& assetDirectory, std::string& error);
    GlyphMetrics glyph(std::uint16_t codepoint, bool forceUnicode = false) const noexcept;
    int charWidth(std::uint16_t codepoint, bool forceUnicode = false) const noexcept;
    bool hasPage(unsigned page) const noexcept { return page < pages_.size() && pages_[page]; }
    const std::array<std::uint8_t,65536>& glyphSizes() const noexcept { return glyphSizes_; }
private:
    std::array<std::uint8_t,65536> glyphSizes_{};
    std::array<std::uint16_t,256> asciiMap_{};
    std::array<std::uint8_t,256> asciiWidths_{};
    std::array<bool,256> pages_{};
    std::array<std::int16_t,65536> asciiIndices_{};
    bool loaded_ = false;
};

struct ItemIconLayer {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    std::uint32_t color = 0xffffffffu; // ARGB tint applied to this layer
    int tintIndex = -1;
};
struct ItemAppearance {
    std::int32_t color = -1; // optional NBT color override for tint layer 0 (24-bit RGB)
    int frame = -1; // optional bow/rod/clock/compass frame; -1 uses ordinary inventory state
};
struct ItemIcon {
    std::vector<ItemIconLayer> layers; // render in order; all coordinates use ItemIconAtlas
    std::string model;
    bool valid = false;
    bool approximate = false; // built-in model fallback or dynamic special renderer
};
class ItemIconAtlas {
public:
    bool load(const std::string& assetDirectory, std::string& error);
    unsigned atlasWidth() const noexcept { return width_; }
    unsigned atlasHeight() const noexcept { return height_; }
    const std::vector<std::uint8_t>& atlasPixels() const noexcept { return pixels_; }
    ItemIcon icon(int itemId, int metadata = 0, const ItemAppearance& appearance = {}) const;
    std::size_t iconCount() const noexcept { return icons_.size(); }
private:
    struct Entry { int id = 0, metadata = 0; std::uint32_t icon = 0; std::array<std::uint32_t,5> colors{}; };
    std::vector<ItemIcon> icons_;
    std::unordered_map<std::uint32_t, Entry> entries_;
    std::unordered_map<int, std::uint32_t> defaults_;
    std::unordered_map<std::uint32_t, std::uint32_t> frames_;
    unsigned width_ = 0, height_ = 0;
    std::vector<std::uint8_t> pixels_;
};

struct AtlasUpdate {
    std::string texture;
    unsigned x = 0, y = 0, width = 0, height = 0;
    std::vector<std::uint8_t> pixels; // padded RGBA8 rectangle for glTexSubImage2D
};
class AnimationAtlasCache {
public:
    // target="terrain" updates the existing mc::Assets terrain atlas.
    bool load(const std::string& assetDirectory, std::string& error, const std::string& target = "terrain");
    // Original animation time is integer game ticks (20 Hz), not render frames.
    // Returned rectangles include extruded padding and only contain changed pixels.
    const std::vector<AtlasUpdate>& sample(std::uint64_t gameTick);
    std::size_t animationCount() const noexcept { return animations_.size(); }
private:
    struct SequenceFrame { std::uint32_t frame = 0, duration = 1; };
    struct Animation {
        std::string texture;
        unsigned x = 0, y = 0, width = 0, height = 0;
        bool interpolate = false;
        std::vector<std::vector<std::uint8_t>> frames;
        std::vector<SequenceFrame> sequence;
        std::vector<std::uint8_t> previous;
        std::uint64_t duration = 0;
    };
    std::vector<Animation> animations_;
    std::vector<AtlasUpdate> updates_;
};

} // namespace mc
