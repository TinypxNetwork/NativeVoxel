#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mc {
struct TileTexture;

using FirstPersonMatrix = std::array<float,16>; // OpenGL column-major
enum class FirstPersonUse { None, Eat, Drink, Block, Bow };
enum class FirstPersonTexture { ItemAtlas, PlayerSkin, MapData };
struct FirstPersonPose {
    int itemId = -1, metadata = 0;
    float swing = 0; // player's interpolated swing progress [0,1]
    float equip = 0; // 1 - interpolated equippedProgress: 0 raised, 1 lowered
    bool usingItem = false, invisible = false, slim = false;
    FirstPersonUse use = FirstPersonUse::None;
    int useTicksRemaining = 0, maxUseTicks = 0;
    float partialTicks = 0;
    float cameraPitch = 0, cameraYaw = 0, armPitch = 0, armYaw = 0; // degrees
    int colorOverride = -1; // optional primary item tint, RGB
    int textureFrame = -1; // original clock/compass frame, otherwise automatic/default
    bool fishingCast = false, rightSleeve = true, leftSleeve = true;
    bool mapDataAvailable = false; // renderer can bind the current map's 128x128 pixels
    bool enchanted=false;
    std::int64_t renderTimeMillis=0;
};
struct FirstPersonVertex {
    float x=0,y=0,z=0,u=0,v=0,nx=0,ny=0,nz=0;
};
struct FirstPersonMesh {
    std::vector<FirstPersonVertex> vertices; // outward CCW triangles
};
struct FirstPersonDraw {
    std::shared_ptr<const FirstPersonMesh> mesh;
    FirstPersonMatrix matrix{};
    FirstPersonTexture texture = FirstPersonTexture::ItemAtlas;
    std::uint32_t color = 0xffffffffu; // ARGB
    bool translucent = false;
    bool glint = false;
    FirstPersonMatrix textureMatrix{{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
    std::shared_ptr<const TileTexture> dynamicTexture; // takes precedence over texture enum
};
struct FirstPersonFrame {
    std::vector<FirstPersonDraw> draws;
    bool supported = false, approximate = false;
};
enum class ItemModelTransform { None, Ground, Fixed, ThirdPerson, Gui, Head };
struct ItemModelFrame {
    std::vector<FirstPersonDraw> draws; // model transform + RenderItem scale/centering
    std::array<float,3> groundScale{{1,1,1}};
    bool supported = false, approximate = false, gui3d = true;
    bool builtIn=false;
};

class FirstPersonAssets {
public:
    FirstPersonAssets();
    ~FirstPersonAssets();
    FirstPersonAssets(const FirstPersonAssets&) = delete;
    FirstPersonAssets& operator=(const FirstPersonAssets&) = delete;
    bool load(const std::string& assetsDirectory, std::string& error);
    unsigned atlasWidth() const noexcept;
    unsigned atlasHeight() const noexcept;
    const std::vector<std::uint8_t>& atlasPixels() const noexcept;
    FirstPersonFrame build(const FirstPersonPose& pose) const;
    // Shared item meshes for world renders. GROUND/FIXED come from transforms.bin;
    // first-person cache format/API remain usable when that sidecar is absent.
    bool worldModelsReady() const noexcept;
    bool equipmentModelsReady() const noexcept;
    ItemModelFrame itemModel(int itemId,int metadata,ItemModelTransform transform,
                             int colorOverride=-1,int textureFrame=-1) const;
    // Source ItemRenderer transformations without a graphics context/assets.
    static FirstPersonMatrix itemTransform(const FirstPersonPose& pose);
    static FirstPersonMatrix armTransform(const FirstPersonPose& pose);
    static void appendGlint(std::vector<FirstPersonDraw>& draws,std::int64_t renderTimeMillis);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mc
