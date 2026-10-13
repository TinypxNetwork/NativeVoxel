#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace mc {

struct EntityVertex {
    float x = 0, y = 0, z = 0;
    float u = 0, v = 0;
    float nx = 0, ny = 0, nz = 0;
};

struct EntityTexture {
    std::string name; // e.g. minecraft:entity/creeper/creeper
    unsigned width = 0, height = 0;
    std::vector<std::uint8_t> pixels; // top-down RGBA8
};

struct EntityPose {
    float headYaw = 0, headPitch = 0; // radians relative to body; Minecraft yaw/pitch convention
    float limbSwing = 0, limbSwingAmount = 0;
    float ageTicks = 0, swingProgress = 0;
    float scale = 1; // multiplier for renderer-specific entity size (slime size etc.)
    bool sneaking = false, riding = false, child = false, aimedBow = false;
    int heldItemRight = 0, heldItemLeft = 0;
    int variant = 0; // skeleton type, villager profession, slime size, etc., where documented
    std::uint32_t flags = 0; // EntityPoseFlags below, interpreted by each family.
    int entityId = 0, horseType = 0, horseArmor = 0, collarColor = 14;
    int invulnerabilityTicks = 0;
    float health = 20, partialTick = 0;
    float grazing = 0, rearing = 0, mouthOpen = 0, horseSize = .5f;
    float interestedAngle = 0, headShake = 0, maneShake = 0, bodyShake = 0, tailShake = 0;
    float jumpProgress = 0, spikeExtension = 1, tailPhase = -1, animationPhase = -1;
    float eyeOffsetX = 0, eyeOffsetY = 0;
    std::array<float,2> sideHeadYaw{}, sideHeadPitch{}; // radians relative to body
    // Armor-stand rotations are protocol degrees, matching Rotations metadata.
    std::array<float,3> standHead{}, standBody{};
    std::array<float,3> standLeftArm{{-10,0,-10}}, standRightArm{{-15,0,10}};
    std::array<float,3> standLeftLeg{{-1,0,-1}}, standRightLeg{{1,0,1}};
    float bodyYaw = 0; // radians, used to keep armor-stand base plate world-aligned
    // EntityDragon.getMovementOffsets(index, partialTick): yaw and height.
    std::array<std::array<double,2>,24> dragonOffsets{};
    std::uint8_t skinParts=0x7f; // EnumPlayerModelParts bit mask
    bool spectator=false,localPlayer=false;
};

enum EntityPoseFlags : std::uint32_t {
    PoseSitting=1u<<0, PoseAngry=1u<<1, PoseTamed=1u<<2, PoseSprinting=1u<<3,
    PoseHanging=1u<<4, PoseSaddled=1u<<5, PoseChested=1u<<6, PoseHasRider=1u<<7,
    PoseTailWag=1u<<8, PoseElder=1u<<9, PoseInvulnerable=1u<<10,
    PoseShowArms=1u<<11, PoseNoBasePlate=1u<<12, PoseMarker=1u<<13,
    PoseDrinking=1u<<14, PoseToast=1u<<15
};

struct EntityMesh {
    std::vector<EntityVertex> vertices; // outward-CCW independent triangles
    int texture = -1;                  // index into EntityModels::textures()
    bool supported = false;            // false -> no geometry; caller can choose an explicit fallback
    bool approximate = false;          // a supported family with documented missing special effects
    std::string modelName;
};
using EntityMatrix=std::array<float,16>;
struct EntityLayer {
    EntityMesh mesh;
    std::array<float,4> color{{1,1,1,1}};
    EntityMatrix textureMatrix{{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
    bool playerSkin=false,glint=false; // glint: depth EQUAL, no depth write, SRC_COLOR/ONE
};

class EntityModels {
public:
    bool load(const std::string& assetsDirectory, std::string& error);
    const std::vector<EntityTexture>& textures() const noexcept { return textures_; }
    // kind 0=player, 1=mob protocol type, 2=object protocol type.
    // Feet at (0,0,0), Y up, body faces +Z. Position/yaw are applied by the caller.
    EntityMesh build(int kind, int type, const EntityPose& pose = {}) const;
    // Same player-local feet/Y-up coordinates as build(0,...), including the
    // original RenderPlayer pre-scale. ItemArmor IDs select material/slot.
    std::vector<EntityLayer> playerArmor(int itemId,const EntityPose&,int rgb=-1,bool enchanted=false) const;
    EntityMatrix playerHand(const EntityPose&) const; // LayerHeldItem attachment before item model
    EntityMatrix playerHead(const EntityPose&) const; // LayerCustomHead attachment
    std::vector<EntityLayer> humanoidArmor(int kind,int type,int itemId,const EntityPose&,int rgb=-1,bool enchanted=false,int slotOverride=-1) const;
    EntityMatrix humanoidHand(int kind,int type,const EntityPose&) const;
    EntityMatrix humanoidHead(int kind,int type,const EntityPose&) const;
    int textureIndex(const std::string& resource) const noexcept;
private:
    std::vector<EntityTexture> textures_;
    std::unordered_map<std::string, int> lookup_;
};

} // namespace mc
