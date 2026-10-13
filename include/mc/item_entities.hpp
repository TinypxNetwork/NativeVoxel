#pragma once

#include "mc/first_person.hpp"

namespace mc {
class TileEntityVisuals;class SkinCache;

enum class ItemEntityTexture { ItemAtlas, ExperienceOrb, ParticleAtlas };
struct ThrownItemPose {
    int itemId=-1,metadata=0;
    bool fishingHook=false;
    double x=0,y=0,z=0;
    float viewYaw=0,viewPitch=0;
    std::uint32_t packedLight=0;
    const std::vector<std::uint8_t>* nbt=nullptr;
    std::int64_t renderTimeMillis=0;
};
struct DroppedItemPose {
    int itemId=-1,metadata=0,count=1;
    int ageTicks=0;
    float partialTicks=0,hoverStart=0; // original EntityItem hoverStart, radians
    double x=0,y=0,z=0; // interpolated position relative to renderer origin
    int colorOverride=-1,textureFrame=-1;
    bool cameraOptionsPresent=true; // RenderManager.options != null
    std::uint32_t packedLight=0;
    const std::vector<std::uint8_t>* nbt=nullptr;
    std::int64_t renderTimeMillis=0;
};
struct XPOrbPose {
    int amount=0,xpColor=0; // EntityXPOrb value and client color tick counter
    float partialTicks=0,viewYaw=0,viewPitch=0; // RenderManager view angles, degrees
    double x=0,y=0,z=0; // interpolated position relative to renderer origin
    std::uint32_t packedLight=0; // ordinary entity light, before XP block-light boost
};
struct ItemEntityDraw {
    std::shared_ptr<const FirstPersonMesh> mesh;
    FirstPersonMatrix matrix{};
    ItemEntityTexture texture=ItemEntityTexture::ItemAtlas;
    std::uint32_t color=0xffffffffu; // ARGB; orb alpha is the original 128
    std::shared_ptr<const TileTexture> dynamicTexture;
    bool glint=false;
    FirstPersonMatrix textureMatrix{{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
};
struct ItemEntityFrame {
    std::vector<ItemEntityDraw> draws;
    std::uint32_t packedLight=0;
    int copies=0;
    float shadowSize=.15f,shadowOpacity=.75f;
    bool supported=false,approximate=false;
};

class ItemEntityVisuals {
public:
    static ItemEntityFrame dropped(const FirstPersonAssets&,const DroppedItemPose&,TileEntityVisuals* tiles=nullptr,SkinCache* skins=nullptr);
    static ItemEntityFrame orb(const XPOrbPose&);
    static ItemEntityFrame thrown(const FirstPersonAssets&,const ThrownItemPose&);
    static FirstPersonMatrix thrownTransform(const ThrownItemPose&);
    static int stackCopies(int count) noexcept;
    static int orbTexture(int amount) noexcept;
    static std::uint32_t orbColor(int colorTicks,float partialTicks);
    static std::uint32_t orbLight(std::uint32_t ordinaryPackedLight) noexcept;
    // Original transformations, before GROUND and RenderItem scale/centering.
    static std::vector<FirstPersonMatrix> droppedTransforms(const DroppedItemPose&,bool gui3d,
                                                           const std::array<float,3>& groundScale);
    static FirstPersonMatrix orbTransform(const XPOrbPose&);
    // Existing imported NVTX texture, relative to assets directory.
    static const char* orbTexturePath() noexcept {return "runtime/entities/minecraft/experience_orb.rgba";}
};
} // namespace mc
