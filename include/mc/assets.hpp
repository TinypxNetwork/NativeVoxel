#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mc {

// Matches EnumFacing indices in the 1.8.9 source.
enum class ModelFace : std::int8_t { Down = 0, Up = 1, North = 2, South = 3, West = 4, East = 5 };
enum class RenderLayer : std::uint8_t { Opaque = 0, Cutout = 1, Translucent = 2 };

struct ModelVertex {
    float x = 0, y = 0, z = 0; // block-local coordinates, 1 = one block
    float u = 0, v = 0;       // normalized atlas coordinates (PNG top = v 0)
};

struct ModelQuad {
    std::array<ModelVertex, 4> vertices{}; // outward counterclockwise winding
    std::int8_t face = -1;                // ModelFace, or -1 for a non-axis normal
    std::int8_t cullFace = -1;            // only cull against this neighbor, -1 = never
    std::int16_t tintIndex = -1;          // -1 = texture already colored
    float nx = 0, ny = 0, nz = 0;
    bool shade = true;
};

struct BlockModel {
    std::vector<ModelQuad> quads;
    RenderLayer layer = RenderLayer::Opaque;
    bool fullCube = false;
    bool occludes = false;
    bool ambientOcclusion = true;
    bool approximate = false; // built-in renderer fallback; see asset report
};

struct AtlasSprite {
    float u0=0,v0=0,u1=1,v1=1;
    bool valid=false;
    float u(float pixel)const{return u0+(u1-u0)*pixel/16.f;}
    float v(float pixel)const{return v0+(v1-v0)*pixel/16.f;}
};

class Assets {
public:
    // Directory made by tools/prepare_assets.py. No Java/Python required at runtime.
    bool load(const std::string& directory, std::string& error);
    unsigned atlasWidth() const noexcept { return atlasWidth_; }
    unsigned atlasHeight() const noexcept { return atlasHeight_; }
    const std::vector<std::uint8_t>& atlasPixels() const noexcept { return atlasPixels_; }
    const BlockModel& model(std::uint16_t state) const noexcept;
    // Selects weighted alternatives with the Java 1.8.9 block-position seed.
    const BlockModel& model(std::uint16_t state, int x, int y, int z) const noexcept;
    // BlockDoublePlant.getActualState: upper metadata stores facing, while its
    // plant variant comes from the lower block. Lower/missing-neighbor cases
    // keep the original metadata-derived model.
    const BlockModel& doublePlantModel(std::uint16_t state,std::uint16_t below,
        int x=0,int y=0,int z=0) const noexcept;
    const std::string& stateName(std::uint16_t state) const noexcept;
    const std::string& stateVariant(std::uint16_t state) const noexcept;
    // Overrides actual-state properties such as fence connections and stair shape.
    // Unspecified properties retain their metadata-derived values.
    const BlockModel& modelWithProperties(std::uint16_t state,
        const std::vector<std::pair<std::string, std::string>>& properties,
        int x = 0, int y = 0, int z = 0) const;
    const BlockModel& missingModel() const noexcept { return missingModel_; }
    const AtlasSprite& sprite(const std::string& resource) const noexcept;
    // RGBA font from vanilla ascii.png; 128 x 128 in the default pack.
    unsigned fontWidth() const noexcept { return fontWidth_; }
    unsigned fontHeight() const noexcept { return fontHeight_; }
    const std::vector<std::uint8_t>& fontPixels() const noexcept { return fontPixels_; }
    std::size_t modelCount() const noexcept { return models_.size(); }

private:
    struct Choice { std::uint32_t model = 0, weight = 1; };
    struct Variant { std::string resource, properties; std::vector<Choice> choices; };
    struct State { std::string name, resource, properties; std::uint32_t variant = 0; };
    const BlockModel& select(std::uint32_t variant, std::uint64_t seed) const noexcept;
    unsigned atlasWidth_ = 0, atlasHeight_ = 0, fontWidth_ = 0, fontHeight_ = 0;
    std::vector<std::uint8_t> atlasPixels_, fontPixels_;
    std::vector<BlockModel> models_;
    std::vector<Variant> variants_;
    std::vector<State> states_;
    std::unordered_map<std::string, std::uint32_t> variantLookup_;
    std::array<std::uint32_t,16> doublePlantUpperVariants_{};
    BlockModel missingModel_;
    std::unordered_map<std::string,AtlasSprite> sprites_;
};

} // namespace mc
