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
    std::vector<std::string> warnings;
    std::size_t replacedFiles{},decodedImages{},rebakedModels{};
};
bool inspectResourcePack(const std::string& path,ResourcePackInfo& info,std::string& error);
// Paths are ordered from lowest to highest priority (last wins). Builds an
// immutable native cache and publishes it only after validation. baseDirectory
// is never modified; an empty selection returns that base directory directly.
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
