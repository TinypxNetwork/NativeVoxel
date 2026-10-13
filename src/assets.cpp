#include "mc/assets.hpp"
#include "mc/gui_json.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>

namespace mc {
namespace {
class InputFile {
public:
    explicit InputFile(const std::string& path) : stream_(path, std::ios::binary) {
        if (!stream_) throw std::runtime_error("Cannot open asset file: " + path);
    }
    void bytes(void* target, std::size_t count) {
        if (!stream_.read(static_cast<char*>(target), static_cast<std::streamsize>(count)))
            throw std::runtime_error("Truncated asset file");
    }
    std::uint8_t u8() { std::uint8_t x; bytes(&x, 1); return x; }
    std::uint16_t u16() { auto a = u8(); auto b = u8(); return std::uint16_t(a | (b << 8)); }
    std::uint32_t u32() {
        auto a = u16(); auto b = u16(); return std::uint32_t(a) | (std::uint32_t(b) << 16);
    }
    float f32() {
        const auto bits = u32(); float result; std::memcpy(&result, &bits, sizeof(result));
        if (!std::isfinite(result)) throw std::runtime_error("Non-finite asset vertex");
        return result;
    }
    std::string str() {
        auto size = u32();
        if (size > 65536) throw std::runtime_error("Asset string exceeds format limit");
        std::string result(size, '\0'); if (size) bytes(&result[0], size); return result;
    }
    void end() {
        if (stream_.peek() != std::ifstream::traits_type::eof())
            throw std::runtime_error("Unexpected trailing asset data");
    }
private:
    std::ifstream stream_;
};

std::vector<std::uint8_t> pixels(const std::string& path, unsigned w, unsigned h) {
    if (!w || !h || w > 16384 || h > 16384)
        throw std::runtime_error("Invalid atlas/font dimensions");
    const auto count = std::uint64_t(w) * h * 4;
    if (count > 512u * 1024u * 1024u) throw std::runtime_error("Asset image too large");
    InputFile input(path); std::vector<std::uint8_t> result(static_cast<std::size_t>(count));
    input.bytes(result.data(), result.size()); input.end(); return result;
}

std::uint64_t positionSeed(int x, int y, int z) noexcept {
    // MathHelper.getCoordinateRandom: x multiplication wraps as Java int first.
    const std::uint32_t xProduct = std::uint32_t(x) * 3129871u;
    const std::int64_t xSigned = xProduct <= 0x7fffffffu ? std::int64_t(xProduct)
        : std::int64_t(xProduct) - 0x100000000ll;
    std::uint64_t seed = std::uint64_t(xSigned) ^ (std::uint64_t(std::int64_t(z)) * 116129781ull)
        ^ std::uint64_t(std::int64_t(y));
    return seed * seed * 42317861ull + seed * 11ull;
}

std::map<std::string, std::string> parseProperties(const std::string& text) {
    std::map<std::string, std::string> props;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const auto end = text.find(',', begin);
        const auto equal = text.find('=', begin);
        if (equal != std::string::npos && (end == std::string::npos || equal < end))
            props[text.substr(begin, equal - begin)] = text.substr(equal + 1,
                end == std::string::npos ? end : end - equal - 1);
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return props;
}
std::string serializeProperties(const std::map<std::string, std::string>& props) {
    std::string result;
    for (const auto& p : props) {
        if (!result.empty()) result += ',';
        result += p.first + '=' + p.second;
    }
    return result.empty() ? "normal" : result;
}
} // namespace

bool Assets::load(const std::string& directory, std::string& error) {
    try {
        Assets next;
        InputFile input(directory + "/assets.bin");
        char magic[8]; input.bytes(magic, 8);
        if (std::memcmp(magic, "NVASSET1", 8)) throw std::runtime_error("Unsupported asset cache format");
        next.atlasWidth_ = input.u32(); next.atlasHeight_ = input.u32();
        next.fontWidth_ = input.u32(); next.fontHeight_ = input.u32();
        const auto modelCount = input.u32(), variantCount = input.u32(), stateCount = input.u32();
        if (!modelCount || modelCount > 100000 || variantCount > 100000 || !variantCount || stateCount > 65536)
            throw std::runtime_error("Invalid asset table counts");
        next.models_.resize(modelCount);
        std::uint64_t totalQuads = 0;
        for (auto& model : next.models_) {
            const auto layer = input.u8(), flags = input.u8(); input.u16();
            if (layer > 2 || (flags & ~15u)) throw std::runtime_error("Invalid model flags");
            model.layer = static_cast<RenderLayer>(layer);
            model.fullCube = (flags & 1) != 0; model.occludes = (flags & 2) != 0;
            model.ambientOcclusion = (flags & 4) != 0; model.approximate = (flags & 8) != 0;
            const auto quadCount = input.u32(); totalQuads += quadCount;
            if (quadCount > 16384 || totalQuads > 2000000) throw std::runtime_error("Too many model quads");
            model.quads.resize(quadCount);
            for (auto& quad : model.quads) {
                const auto face = input.u8(), cull = input.u8();
                quad.face = face == 255 ? -1 : static_cast<std::int8_t>(face);
                quad.cullFace = cull == 255 ? -1 : static_cast<std::int8_t>(cull);
                if ((face != 255 && face > 5) || (cull != 255 && cull > 5))
                    throw std::runtime_error("Invalid model face");
                quad.tintIndex = static_cast<std::int16_t>(input.u16());
                quad.shade = input.u8() != 0; input.u8(); input.u16();
                quad.nx = input.f32(); quad.ny = input.f32(); quad.nz = input.f32();
                for (auto& vertex : quad.vertices) {
                    vertex.x = input.f32(); vertex.y = input.f32(); vertex.z = input.f32();
                    vertex.u = input.f32(); vertex.v = input.f32();
                }
            }
        }
        next.variants_.resize(variantCount);
        for (std::uint32_t i = 0; i < variantCount; ++i) {
            auto& variant = next.variants_[i];
            variant.resource = input.str(); variant.properties = input.str();
            const auto count = input.u32();
            if (!count || count > 1024) throw std::runtime_error("Invalid variant choice count");
            variant.choices.resize(count);
            std::uint64_t totalWeight = 0;
            for (auto& choice : variant.choices) {
                choice.model = input.u32(); choice.weight = input.u32(); totalWeight += choice.weight;
                if (choice.model >= modelCount || !choice.weight || totalWeight > 0x7fffffffu)
                    throw std::runtime_error("Invalid weighted model reference");
            }
            const auto key = variant.resource + '#' + variant.properties;
            if (!next.variantLookup_.emplace(key, i).second)
                throw std::runtime_error("Duplicate model variant");
        }
        next.states_.resize(stateCount);
        for (auto& state : next.states_) {
            state.name = input.str(); state.resource = input.str(); state.properties = input.str();
            state.variant = input.u32();
            if (state.variant >= variantCount) throw std::runtime_error("Invalid block-state variant reference");
        }
        constexpr unsigned plant=175u<<4;
        const auto defaultUpper=next.states_.size()>plant+8?next.states_[plant+8].variant:0u;
        for(unsigned metadata=0;metadata<16;++metadata){
            next.doublePlantUpperVariants_[metadata]=defaultUpper;
            if(next.states_.size()>plant+metadata){const auto& lower=next.states_[plant+metadata];const auto found=next.variantLookup_.find(lower.resource+"#half=upper");
                if(found!=next.variantLookup_.end())next.doublePlantUpperVariants_[metadata]=found->second;
            }
        }
        input.end();
        next.atlasPixels_ = pixels(directory + "/atlas.rgba", next.atlasWidth_, next.atlasHeight_);
        next.fontPixels_ = pixels(directory + "/font.rgba", next.fontWidth_, next.fontHeight_);
        next.missingModel_ = next.models_.front();
        std::ifstream metadata(directory+"/atlas.json",std::ios::binary|std::ios::ate);
        if(metadata){
            const auto length=metadata.tellg();
            if(length<0||length>8*1024*1024)throw std::runtime_error("Atlas metadata size limit");
            std::string text(static_cast<std::size_t>(length),'\0');metadata.seekg(0);
            metadata.read(text.data(),std::streamsize(text.size()));
            if(!metadata)throw std::runtime_error("Truncated atlas metadata");
            const auto json=gui_json::JsonParser(text,8*1024*1024,1048576).parse();
            if(const auto* textures=json.get("textures"))for(const auto& entry:textures->object){
                auto number=[&](const char* key){const auto* value=entry.second.get(key);
                    if(!value||value->kind!=gui_json::Json::Kind::Number)throw std::runtime_error("Invalid atlas sprite");
                    const float f=std::stof(value->value);if(!std::isfinite(f)||f<0||f>1)throw std::runtime_error("Invalid atlas sprite UV");return f;};
                AtlasSprite sprite{number("u0"),number("v0"),number("u1"),number("v1"),true};
                if(sprite.u1<=sprite.u0||sprite.v1<=sprite.v0)throw std::runtime_error("Empty atlas sprite");
                next.sprites_.emplace(entry.first,sprite);
            }
        }
        *this = std::move(next); error.clear(); return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

const AtlasSprite& Assets::sprite(const std::string& resource) const noexcept {
    static const AtlasSprite missing{};
    const auto found=sprites_.find(resource);return found==sprites_.end()?missing:found->second;
}

const BlockModel& Assets::select(std::uint32_t variant, std::uint64_t seed) const noexcept {
    if (variant >= variants_.size()) return missingModel_;
    const auto& choices = variants_[variant].choices;
    std::uint32_t totalWeight = 0;
    for (const auto& choice : choices) totalWeight += choice.weight;
    if (!totalWeight) return missingModel_;
    // Java Math.abs((int)seed >> 16), with an explicit signed arithmetic shift.
    const auto low = std::uint32_t(seed);
    const std::int32_t shifted = low <= 0x7fffffffu ? std::int32_t(low >> 16)
        : std::int32_t(low >> 16) - 65536;
    auto value = std::uint32_t(shifted < 0 ? -shifted : shifted) % totalWeight;
    for (const auto& choice : choices) {
        if (value < choice.weight) return models_[choice.model];
        value -= choice.weight;
    }
    return missingModel_;
}

const BlockModel& Assets::model(std::uint16_t state) const noexcept {
    return state < states_.size() ? select(states_[state].variant, 0) : missingModel_;
}
const BlockModel& Assets::model(std::uint16_t state, int x, int y, int z) const noexcept {
    return state < states_.size() ? select(states_[state].variant, positionSeed(x, y, z)) : missingModel_;
}
const BlockModel& Assets::doublePlantModel(std::uint16_t state,std::uint16_t below,int x,int y,int z) const noexcept {
    if((state>>4)==175&&(state&8)&&(below>>4)==175)return select(doublePlantUpperVariants_[below&15],positionSeed(x,y,z));
    return model(state,x,y,z);
}
const std::string& Assets::stateName(std::uint16_t state) const noexcept {
    static const std::string unknown = "minecraft:unknown";
    return state < states_.size() ? states_[state].name : unknown;
}
const std::string& Assets::stateVariant(std::uint16_t state) const noexcept {
    static const std::string empty;
    return state < states_.size() ? states_[state].properties : empty;
}
const BlockModel& Assets::modelWithProperties(std::uint16_t state,
    const std::vector<std::pair<std::string, std::string>>& properties, int x, int y, int z) const {
    if (state >= states_.size()) return missingModel_;
    const auto& base = states_[state]; auto props = parseProperties(base.properties);
    for (const auto& item : properties) props[item.first] = item.second;
    // BlockModelShapes deliberately removes age from a connected stem's key.
    if ((base.resource == "minecraft:pumpkin_stem" || base.resource == "minecraft:melon_stem") &&
        props.count("facing") && props["facing"] != "up") props.erase("age");
    const auto found = variantLookup_.find(base.resource + '#' + serializeProperties(props));
    return select(found == variantLookup_.end() ? base.variant : found->second, positionSeed(x, y, z));
}

} // namespace mc
