#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mc {
struct ResourceImage {
    unsigned width{},height{};
    std::vector<std::uint8_t> pixels; // Unassociated RGBA8, top-left origin.
};
struct ResourcePackInfo {
    std::string path,name,description;
    int format{};
    bool directory{};
    std::size_t files{},bytes{};
    unsigned iconWidth{},iconHeight{};
    std::vector<std::uint8_t> iconPixels;
};
struct PreparedResourcePacks {
    std::string assetDirectory;
    std::vector<ResourcePackInfo> packs;
    // Non-fatal problems encountered while loading. Mirrors the 1.8.9 per-pack /
    // per-resource isolation: corrupt ZIP entries, unsafe paths, unreadable
    // images/JSON/models and unopenable packs are skipped here instead of
    // invalidating the selection. Capped at ~200 entries plus one summary line.
    std::vector<std::string> warnings;
    std::size_t replacedFiles{},decodedImages{},rebakedModels{};
};
// Returns false only when the pack cannot be enumerated at all (missing path,
// unreadable container). A partially corrupt pack that still exposes entries is
// inspectable: its format defaults to 1 and its description to the file name.
bool inspectResourcePack(const std::string& path,ResourcePackInfo& info,std::string& error);
// Paths are ordered from lowest to highest priority (last wins). Builds an
// immutable native cache and publishes it only after validation. baseDirectory
// is never modified; an empty selection returns that base directory directly.
// If base assets are valid and the cache/staging directory is usable this
// succeeds and publishes a cache even when packs are corrupt or unopenable:
// broken packs/entries/resources are dropped with a warning and the remaining
// (higher-priority then base) resources are used. It fails only for fatal
// conditions: invalid base assets, a cache directory inside the asset/pack
// tree, and unwritable/unpublishable staging. On failure the previous output
// and the live assets are left untouched and no .staging_* directory remains.
bool prepareResourcePacks(const std::string& baseDirectory,const std::vector<std::string>& paths,
                          const std::string& cacheDirectory,PreparedResourcePacks& prepared,std::string& error);
// Native PNG decode; no Python/JVM and no player-skin size restrictions.
// On failure neither output image nor an existing resource cache is changed.
bool decodeResourcePng(const std::vector<std::uint8_t>& bytes,ResourceImage& image,std::string& error);
// resourcePath is relative to <assetDirectory>/resources, e.g.
// assets/minecraft/textures/environment/sun.png.
bool readResourceImage(const std::string& assetDirectory,const std::string& resourcePath,
                       ResourceImage& image,std::string& error);
}
