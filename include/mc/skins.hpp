#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mc {

struct SkinDescriptor {
    std::string url; // validated, canonical HTTPS textures.minecraft.net URL
    bool slim = false;
};
struct SkinImage {
    unsigned width = 64, height = 64;
    std::vector<std::uint8_t> pixels; // top-down RGBA8, legacy layout normalized
};
struct PlayerSkin {
    SkinImage image;
    std::string textureKey; // stable key for renderer GPU texture caching
    bool slim = false, downloaded = false;
    std::uint64_t revision = 0;
};

class SkinCache {
public:
    SkinCache();
    ~SkinCache();
    SkinCache(const SkinCache&) = delete;
    SkinCache& operator=(const SkinCache&) = delete;
    // Network=false is a deterministic, device-free test/offline mode.
    bool initialize(const std::string& assetsDirectory, std::string& error, bool network = true);
    // Takes the server's base64 `textures` property. Signature is retained as
    // metadata; signature verification status is documented separately.
    bool request(const std::string& uuid, const std::string& texturesValue,
                 const std::string& signature = {});
    std::shared_ptr<const PlayerSkin> get(const std::string& uuid) const;
    std::size_t pending() const noexcept;
    std::string lastError(const std::string& uuid) const;
    void clear();
    // Pure parsing and image helpers. They never perform an HTTP request.
    static bool parseProperty(const std::string& base64Value, SkinDescriptor& descriptor, std::string& error);
    static bool canonicalTextureUrl(const std::string& input, std::string& httpsUrl, std::string& error);
    static bool decodePng(const std::vector<std::uint8_t>& encoded, SkinImage& image, std::string& error);
    static bool normalize(unsigned width, unsigned height, const std::vector<std::uint8_t>& rgba,
                          SkinImage& image, std::string& error);
    static bool defaultSlim(const std::string& uuid) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mc
