#pragma once
#include "mc/math.hpp"
#include "mc/entity_models.hpp"
#include "mc/skins.hpp"
#include "mc/first_person.hpp"
#include "mc/tile_entities.hpp"
#include "mc/entity_visuals.hpp"
#include "mc/particles.hpp"
#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace mc {
class Assets;
class World;
struct ItemStack;
struct Camera {
    double x = 0, y = 70, z = 0;
    float yaw = 0, pitch = 0, fov = 75;
};
struct RenderEntity {
    int id = 0, type = 0, kind = 0;
    double x = 0, y = 0, z = 0;
    float yaw = 0, pitch = 0, headYaw = 0, limbSwing = 0, limbSwingAmount = 0, ageTicks = 0, width = .6f, height = 1.8f;
    bool player = false, sneaking = false, child = false, invisible = false, showName = false;
    std::string name;
    EntityPose pose;
    std::shared_ptr<const PlayerSkin> skin;
    int itemId = -1, itemMetadata = 0, itemCount = 1, xpValue = 0;
    float hoverStart = 0;
    std::array<VisualItem,5> equipment{};
    std::vector<std::uint8_t> itemNbt;
    bool usingItem = false, translucentBody = false;
    int useTicksRemaining = 0;
    bool pickup = false;
    double pickupSourceX = 0, pickupSourceY = 0, pickupSourceZ = 0;
    int projectileShake = 0;
    int hurtTime = 0, deathTime = 0;
    bool burning = false;
    bool fishingLine = false, localAngler = false, anglerSneaking = false;
    double anglerX = 0, anglerY = 0, anglerZ = 0;
    float anglerYaw = 0, anglerPitch = 0, anglerBodyYaw = 0, anglerSwing = 0, anglerEyeHeight = 1.62f;
};
struct RenderStats {
    std::size_t visibleSections = 0, loadedSections = 0, triangles = 0, pendingMeshes = 0;
    double meshMs = 0, frameMs = 0;
    double meshUpdateMs = 0, dynamicPrepareMs = 0;
    std::size_t dynamicDraws = 0, dynamicVertices = 0;
    std::string gpu;
    std::string secretShaderError;
    std::size_t secretShaderPasses=0;
};
struct RenderSign {
    int x = 0, y = 0, z = 0;
    std::array<std::string, 4> lines;
};
struct RenderBreak { int x=0,y=0,z=0,stage=-1; };
struct RenderTile {
    int x = 0, y = 0, z = 0;
    const std::vector<std::uint8_t> *nbt = nullptr;
    ChestLidState lid;
};
struct RenderOptions {
    int distance = 12;
    std::uint8_t playerSkinParts = 127;
    bool shadows = false, fxaa = false, wireframe = false;
    float exposure = 1.f;
    float firstPersonFov = 70.f;
    int secretShader = -1, mipmapLevels = 1;
    bool ambientOcclusion = true, blockAlternatives = true;
    float gamma = 0.f, rain = 0.f, thunder = 0.f, nightVision = 0.f, partialTicks = 0.f;
    int dimension = 0;
    int respiration = 0, thirdPerson = 0;
    bool waterBreathing = false, firstPersonEffects = false, burning = false;
};
class Renderer {
  public:
    Renderer();
    ~Renderer();
    void swap(Renderer&) noexcept;
    bool initialize(const Assets &, std::string &error, const std::string &assetDirectory = "assets");
    void resetWorld();
    void menuBackground(int width, int height, double timeSeconds);
    void render(const World &, const Camera &, int width, int height, double timeSeconds, std::int64_t worldTime,
                const std::vector<RenderEntity> &entities = {}, const std::vector<RenderSign> &signs = {},
                const std::vector<RenderTile> &tiles = {}, SkinCache *skins = nullptr);
    void setFirstPerson(const FirstPersonPose &, std::shared_ptr<const PlayerSkin> skin = {}, const ItemStack* item = nullptr);
    void hideFirstPerson();
    void setBlockBreaks(std::vector<RenderBreak>);
    void setParticles(std::vector<ParticleSprite>);
    void setViewBobbing(const gfx::Mat4&);
    void selection(int x, int y, int z);
    void selection(double minX, double minY, double minZ, double maxX, double maxY, double maxZ);
    void clearSelection();
    void beginUI(int width, int height);
    void setUnicode(bool enabled);
    void rect(float x, float y, float width, float height, std::uint32_t rgba);
    void gradient(float x, float y, float width, float height, std::uint32_t top, std::uint32_t bottom);
    void text(const std::string &, float x, float y, float scale = 2, std::uint32_t rgba = 0xffffffff,
              bool shadow = true);
    float textWidth(const std::string &, float scale = 2) const;
    void rotatedText(const std::string &, float centerX, float centerY, float scale, float degrees,
                     std::uint32_t rgba = 0xffffffff);
    void worldText(const std::string &, double x, double y, double z, float worldScale = .0266667f);
    void blockIcon(std::uint16_t state, float x, float y, float size);
    void itemIcon(int itemId, int metadata, float x, float y, float size, int color = -1);
    void itemIcon(const ItemStack&, float x, float y, float size, int color = -1);
    void setPlayerPreview(std::shared_ptr<const PlayerSkin>, const std::array<ItemStack,5>&);
    void playerFace(std::shared_ptr<const PlayerSkin>, float x,float y,float size,bool hat=true);
    bool registerImage(const std::string& name,unsigned width,unsigned height,const std::vector<std::uint8_t>& rgba);
    void sprite(const std::string &name, float x, float y, float width, float height, float sourceX, float sourceY,
                float sourceWidth, float sourceHeight, std::uint32_t rgba = 0xffffffff);
    bool registerUITexture(const std::string &name, const std::string &nvtxPath);
    void previewEntity(float centerX, float bottomY, float height, float lookYaw = 0, float lookPitch = 0);
    void endUI();
    bool screenshot(const std::string &path, int width, int height, std::string &error);
    const RenderStats &stats() const;
    RenderOptions options;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mc
