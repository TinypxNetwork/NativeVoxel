#pragma once

#include "mc/first_person.hpp"
#include <memory>
#include <string>
#include <vector>

namespace mc {
class World;
class SkinCache;

enum class TileEntityKind { Unsupported, Chest, EnderChest, Skull, Banner };
enum class TileRenderStatus { Unsupported, Drawn, Hidden, MissingData, InvalidData };
struct ChestLidState {float previous=0,current=0;int users=0;};
struct TileEntityPose {
    int x=0,y=0,z=0; // actual world block position, for neighbors/banner phase
    double renderX=0,renderY=0,renderZ=0; // position relative to renderer origin
    float partialTicks=0;
    std::int64_t worldTime=0;
    const std::vector<std::uint8_t>* nbt=nullptr; // complete uncompressed root tag; borrowed for build()
    ChestLidState lid;
    bool christmas=false; // original December 24-26 chest variant, supplied by caller
    int destroyStage=-1; // -1 normal; 0..9 original block-damage texture
};
struct TileTexture {
    std::string key; // stable renderer texture identity; resource or generated banner/skin key
    unsigned width=0,height=0;
    std::vector<std::uint8_t> pixels; // top-down RGBA8
    std::uint64_t revision=1;
    bool repeat=false; // damage-stage UV matrices sample a repeating texture
};
struct TileEntityDraw {
    std::shared_ptr<const FirstPersonMesh> mesh;
    FirstPersonMatrix matrix{};
    std::shared_ptr<const TileTexture> texture;
    FirstPersonMatrix textureMatrix{{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
    std::uint32_t color=0xffffffffu; // ARGB
    bool cull=true;
};
struct TileEntityFrame {
    TileEntityKind kind=TileEntityKind::Unsupported;
    TileRenderStatus status=TileRenderStatus::Unsupported;
    std::vector<TileEntityDraw> draws;
    bool approximate=false;
    std::string diagnostic;
};
struct BannerMask {
    std::shared_ptr<const TileTexture> image;
    std::uint32_t rgb=0xffffffu; // EnumDyeColor MapColor (not sheep diffuse color)
    bool javaType6=true; // original loader ignores masks other than 8-bit RGBA
};

class TileEntityVisuals {
public:
    TileEntityVisuals();
    ~TileEntityVisuals();
    TileEntityVisuals(const TileEntityVisuals&)=delete;
    TileEntityVisuals& operator=(const TileEntityVisuals&)=delete;
    bool load(const std::string& assetsDirectory,std::string& error);
    TileEntityFrame build(const World&,const TileEntityPose&,SkinCache* skins=nullptr);
    // TileEntityItemStackRenderer skull path in item-local 0..1 space. Reads
    // item NBT SkullOwner (compound/string), reuses the same SkinCache and models.
    TileEntityFrame skullItem(int skullType,const std::vector<std::uint8_t>& itemNbt,
                             SkinCache* skins=nullptr,float yaw=0);
    TileEntityFrame builtinItem(int itemId,int metadata,const std::vector<std::uint8_t>& itemNbt,
                               SkinCache* skins=nullptr,bool christmas=false);
    void clear(); // clears NBT/generated banner/player-head caches; retains static resources
    static bool handlesBlock(unsigned blockId) noexcept;
    // Display-only chest state, driven once per game tick by S24 action 1.
    static void blockAction(ChestLidState&,int action,int parameter) noexcept;
    static void advanceLid(ChestLidState&) noexcept;
    static float lidRadians(const ChestLidState&,float partialTicks) noexcept;
    // Pure native equivalent of LayeredColorMaskTexture, first 17 layers only.
    static bool composeBanner(const TileTexture& base,const std::vector<BannerMask>&,
                              TileTexture& output,std::string& error);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
struct ChestVisualState {int x=0,y=0,z=0,blockId=0;ChestLidState lid;};
class ChestAnimationTracker {
public:
    ChestAnimationTracker();
    ~ChestAnimationTracker();
    ChestAnimationTracker(const ChestAnimationTracker&)=delete;
    ChestAnimationTracker& operator=(const ChestAnimationTracker&)=delete;
    // serial must be a monotonic event identity, retained when an event is replayed.
    bool action(const World&,int x,int y,int z,int blockId,int action,int parameter,std::uint64_t serial);
    void tick(const World&,std::uint64_t tickNumber);
    ChestLidState sample(const World&,int x,int y,int z) const;
    std::vector<ChestVisualState> states() const;
    void reset();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace mc
