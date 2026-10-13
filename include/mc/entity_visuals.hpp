#pragma once
#include "mc/entity_models.hpp"
#include "mc/first_person.hpp"
#include "mc/tile_entities.hpp"
#include <memory>

namespace mc {
struct VisualItem {
    int id=-1,metadata=0,count=1;
    const std::vector<std::uint8_t>* nbt=nullptr;
    bool present() const noexcept {return id>=0&&count>0;}
};
struct PlayerVisualInput {
    EntityPose pose;
    std::array<VisualItem,5> equipment; // protocol order: held, boots, leggings, chest, helmet
    bool usingItem=false,fishing=false;
    int useAction=0,useTicksRemaining=0,maxUseTicks=0;
    int kind=0,type=0; // player=0; mob=1; armor stand also supports object kind=2/type=78
    bool invisible=false,showInvisibleSkin=false;
    std::int64_t renderTimeMillis=0;
};
struct PlayerVisualFrame {
    std::vector<EntityLayer> layers; // body (playerSkin=true), then armor/glint
    std::vector<FirstPersonDraw> held;
    std::vector<TileEntityDraw> customHead;
    bool supported=false,approximate=false;
};
struct EntityAnimationTick {
    int id=0;
    std::uint64_t tick=0;
    double x=0,y=0,z=0;
    float yaw=0,pitch=0,headYaw=0; // degrees, after authoritative client entity update
    bool player=true,onGround=true,swingTriggered=false,usingItem=false;
    int swingDuration=6,useDuration=0,useAction=0;
    EntityPose pose; // metadata flags/variants; animation angles are filled by sample()
    bool armorStand=false;
    float swingProgress=-1,previousSwingProgress=-1; // optional already-updated Game animation state
};
struct EntityAnimationSample {
    bool valid=false;
    float bodyYaw=0,headYaw=0,pitch=0; // degrees, short-angle interpolation
    EntityPose pose;
    bool usingItem=false;
    int useTicksRemaining=0;
    float previousYaw=0,yaw=0,previousPitch=0; // retained rotations for RenderArrow etc.
};
class EntityAnimationTracker {
public:
    EntityAnimationTracker();
    ~EntityAnimationTracker();
    EntityAnimationTracker(const EntityAnimationTracker&)=delete;
    EntityAnimationTracker& operator=(const EntityAnimationTracker&)=delete;
    void tick(const EntityAnimationTick&); // once per game tick; duplicate ticks ignored
    EntityAnimationSample sample(int id,float partialTicks) const;
    void erase(int id);
    void prune(std::uint64_t currentTick,std::uint64_t maximumIdleTicks=3);
    std::size_t size() const noexcept;
    void reset();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
struct ArrowPose {
    double x=0,y=0,z=0; // renderer-relative position
    float previousYaw=0,yaw=0,previousPitch=0,pitch=0,partialTicks=0;
    int shakeTicks=0;
};
struct ArrowFrame {
    std::vector<FirstPersonDraw> draws; // bind arrowTexturePath, not ItemAtlas
    std::string textureKey="minecraft:entity/arrow";
};
struct FishingLinePose {
    double hookX=0,hookY=0,hookZ=0,anglerX=0,anglerY=0,anglerZ=0;
    float yaw=0,pitch=0,bodyYaw=0,swing=0,eyeHeight=1.62f;
    bool localAngler=false,thirdPerson=false,sneaking=false;
};
class EntityVisuals {
public:
    static PlayerVisualFrame player(const EntityModels&,const FirstPersonAssets&,const PlayerVisualInput&,
                                   TileEntityVisuals* tiles=nullptr,SkinCache* skins=nullptr);
    static ArrowFrame arrow(const ArrowPose&);
    static std::array<std::array<double,3>,17> fishingLine(const FishingLinePose&);
    static int primaryItemColor(const VisualItem&); // -1 means ordinary registry tint
    static bool enchanted(const VisualItem&);
    static bool hasEffect(const VisualItem&); // Item.hasEffect: includes potion/book overrides
    static ItemModelFrame item(const FirstPersonAssets&,const VisualItem&,ItemModelTransform,
                               TileEntityVisuals* tiles=nullptr,SkinCache* skins=nullptr,
                               int textureFrame=-1,std::int64_t renderTimeMillis=0);
    static FirstPersonFrame firstPerson(const FirstPersonAssets&,const FirstPersonPose&,const VisualItem&,
                                       TileEntityVisuals* tiles=nullptr,SkinCache* skins=nullptr);
    static const char* arrowTexturePath() noexcept {return "runtime/entities/minecraft/arrow.rgba";}
    // RenderItem.setupGuiTransform, before the model's GUI display transform.
    static FirstPersonMatrix inventoryTransform(float x,float y,float size,bool gui3d);
};
} // namespace mc
