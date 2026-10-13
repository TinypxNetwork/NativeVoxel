#pragma once

#include "mc/protocol.hpp"
#include "mc/world.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mc {

struct ItemStack {
    std::int16_t id{-1};
    std::int8_t count{};
    std::int16_t damage{};
    std::vector<std::uint8_t> nbt; // Complete uncompressed root tag, including TAG_End for absent NBT.
    bool empty() const { return id < 0 || count <= 0; }
};

struct MetadataValue {
    std::uint8_t type{};
    std::int64_t integer{};
    double number{};
    std::string text;
    std::vector<std::uint8_t> raw;
};

struct EntityAttributeModifier { std::string uuid; double amount{}; int operation{}; };
struct EntityAttribute {
    double base{}, value{};
    std::vector<EntityAttributeModifier> modifiers;
};

enum class EntityKind { Player, Mob, Object, Painting, ExperienceOrb, Global };
struct Effect { int amplifier{}, duration{}; bool hideParticles{}; };

struct ProjectileState {
    bool inGround{},dead{},impactPending{};
    int blockX{-1},blockY{-1},blockZ{-1},blockState{},airTicks{},groundTicks{},shake{};
    int ownerId{-1},caughtEntityId{-1},hitEntityId{-1},pickupMode{1};
    float previousYaw{},previousPitch{};
    double clientMotionX{},clientMotionY{},clientMotionZ{},damage{2};
    int knockback{};
    std::uint64_t randomState{0x5deece66dULL};
};

struct Entity {
    int id{}, type{};
    EntityKind kind{EntityKind::Object};
    std::string uuid, name;
    int serverX{}, serverY{}, serverZ{}; // Exact server fixed-point coordinates (1/32 block).
    double x{}, y{}, z{}, previousX{}, previousY{}, previousZ{};
    double motionX{}, motionY{}, motionZ{};
    float yaw{}, pitch{}, headYaw{};
    double targetX{}, targetY{}, targetZ{};
    float targetYaw{}, targetPitch{};
    int interpolationTicks{};
    bool onGround{};
    int vehicleId{-1}, leashHolderId{-1};
    std::unordered_map<std::uint8_t, MetadataValue> metadata;
    std::map<std::string, EntityAttribute> attributes;
    std::array<ItemStack, 5> equipment{};
    int xpValue{}; // S11 signed short; type retains its historical value for compatibility.
    std::unordered_map<int,Effect> effects;
    bool swingInProgress{};
    int swingProgressTicks{},swingDuration{6};
    float previousSwingProgress{},swingProgress{};
    std::uint64_t ticksExisted{};
    int ageTicks{};
    float hoverStart{};
    int objectData{}; // S0E: owner ID, potion metadata, item-frame facing, etc.
    std::optional<ProjectileState> projectile;
    int hurtTime{}, maxHurtTime{10}, deathTime{};
    float attackedAtYaw{};
    bool dying{};
};

struct PlayerInfo {
    std::string uuid, name, displayName;
    int gameMode{}, ping{};
    std::string texturesProperty,texturesSignature;
    std::string displayNameJson;
};

struct ChatMessage {
    std::string text, json;
    std::uint8_t position{};
    std::uint64_t tick{};
};

struct SoundEvent {
    std::string name;
    double x{}, y{}, z{};
    float volume{}, pitch{};
};

struct SignText {
    int x{}, y{}, z{};
    std::array<std::string,4> lines;
    std::array<std::string,4> jsonLines;
};

struct ParticleEvent {
    int type{},wireType{},count{};
    bool longDistance{};
    float x{},y{},z{},offsetX{},offsetY{},offsetZ{},speed{};
    std::vector<int> data;
    std::uint64_t tick{};
};
struct EntityAnimationEvent { int entityId{};std::uint8_t animation{};std::uint64_t tick{}; };
struct EntityDeathEvent { double x{},y{},z{};float width{.6f},height{1.8f}; };
// S0D removes the entity immediately. Keep its complete render state alive for
// EntityPickupFX, including stack metadata/NBT and the original hover phase.
struct EntityPickupEvent {
    Entity entity;
    int collectorId{};
    double collectorX{},collectorY{},collectorZ{};
    double previousCollectorX{},previousCollectorY{},previousCollectorZ{};
    std::uint64_t tick{};
};
struct WorldEffectEvent { int effect{},x{},y{},z{},data{};bool global{};std::uint64_t tick{}; };
struct BlockBreakProgress { int breakerId{},x{},y{},z{},stage{};std::uint64_t lastUpdateTick{}; };
struct TileEntityData {
    int x{},y{},z{};
    std::uint8_t action{};
    std::vector<std::uint8_t> nbt; // Complete S35 root tag, including TAG_End when absent.
};
struct BlockAction {
    int x{},y{},z{},blockId{};
    std::uint8_t action{},parameter{};
    std::uint64_t tick{};
    std::uint64_t serial{};
};
struct ScoreObjective { std::string displayName, renderType; };
struct ScoreTeam {
    std::string displayName, prefix, suffix, nameTagVisibility;
    std::uint8_t flags{};
    std::int8_t color{-1};
    std::vector<std::string> members;
};
struct TradeOffer {
    ItemStack first,second,result;
    bool disabled{};
    int uses{},maxUses{7};
};
struct ContainerWindow {
    int id{};
    int entityId{-1};
    std::string type, title;
    bool open{};
    std::vector<ItemStack> slots;
    std::map<int, int> properties;
    std::uint16_t transactionId{};
    std::vector<TradeOffer> trades;
    int selectedTrade{},activeTrade{-1};
    bool tradesLoaded{};
    std::string repairName;
    int repairMaterialCost{};
};

struct ServerWorldBorder {
    double centerX{}, centerZ{}, diameter{60000000.0}, targetDiameter{60000000.0};
    std::int64_t lerpMilliseconds{};
    int portalBoundary{29999984}, warningDistance{5}, warningTime{15};
};

class Game {
public:
    World world;
    Player player;
    net::Client client;
    bool online{}, joined{}, spawned{};
    int entityId{}, dimension{}, gameMode{}, difficulty{}, maxPlayers{};
    bool reducedDebugInfo{}, hardcore{}, invulnerable{}, creativeMode{};
    float health{20.0f}, maxHealth{20.0f}, saturation{5.0f}, experienceProgress{};
    float walkSpeed{0.1f};
    int hurtTime{}, maxHurtTime{10}, deathTime{};
    float attackedAtYaw{};
    int food{20}, experienceLevel{}, totalExperience{}, selectedSlot{};
    std::int64_t worldAge{}, worldTime{};
    int spawnX{}, spawnY{64}, spawnZ{};
    float rainStrength{}, thunderStrength{};
    bool raining{};
    std::uint64_t ticks{}, packetsReceived{}, packetsSent{}, positionCorrections{};
    std::uint64_t windowRevision{}, itemUseCompletions{};
    std::uint64_t armSwings{};
    std::string status{"Disconnected"}, error, levelType{"default"}, serverBrand;
    std::string resourcePackUrl, resourcePackHash;
    std::string title, subtitle, playerListHeader, playerListFooter;
    std::string titleJson,subtitleJson,playerListHeaderJson,playerListFooterJson,disconnectJson;
    std::string actionBarText,actionBarJson;
    int titleFadeIn{10}, titleStay{70}, titleFadeOut{20};
    int titleTicksRemaining{},actionBarTicksRemaining{};
    std::uint64_t chatRevision{};
    int cameraEntityId{-1}, vehicleId{-1}, viewDistance{8};
    std::string locale{"zh_CN"};
    int chatVisibility{};
    bool chatColors{true};
    std::uint8_t skinParts{127};
    std::vector<std::string> tabCompletions;
    std::uint64_t tabCompletionRevision{};
    ServerWorldBorder border;
    std::unordered_map<int, Entity> entities;
    std::unordered_map<std::string, PlayerInfo> playerList;
    std::unordered_map<int, Effect> effects;
    std::unordered_map<std::uint8_t, MetadataValue> playerMetadata;
    std::map<std::string, EntityAttribute> playerAttributes;
    std::array<ItemStack, 45> inventory{};
    std::array<std::uint64_t, 45> inventorySlotRevision{};
    ItemStack cursor;
    ContainerWindow window;
    std::deque<ChatMessage> chat;
    std::deque<SoundEvent> soundEvents;
    std::deque<ParticleEvent> particleEvents;
    std::deque<EntityAnimationEvent> animationEvents;
    std::deque<EntityDeathEvent> deathEvents;
    std::deque<EntityPickupEvent> pickupEvents;
    std::uint64_t pickupGeneration{}; // Changes when a world/session discards its effects.
    std::deque<WorldEffectEvent> worldEffectEvents;
    std::unordered_map<int,BlockBreakProgress> blockBreakProgress;
    std::unordered_map<std::uint64_t,SignText> signs;
    std::unordered_map<std::uint64_t,TileEntityData> tileEntities;
    std::deque<BlockAction> blockActions;
    std::map<std::string, ScoreObjective> objectives;
    std::map<std::string, std::map<std::string, int>> scores;
    std::map<std::string, ScoreTeam> teams;
    std::array<std::string, 19> displayedObjectives{};
    std::map<int, std::uint64_t> unhandledPackets;

    // Optional observer for replay/tests/diagnostics; play packets only, no login credentials.
    std::function<void(const net::Packet&)> outgoingObserver;
    std::function<void(const net::Packet&)> incomingObserver;
    std::function<bool(int,int,int)> offlineBlockInteraction;
    // Installed only for an active offline authority. False rejects the break;
    // Controller must not fall back to its client prediction in that case.
    std::function<bool(int,int,int,int)> offlineBlockBreak;
    // Accepted drops must be atomic; false leaves the proposed item intact.
    std::function<bool(const ItemStack&,bool)> offlineItemDrop;
    std::function<bool(const std::vector<ItemStack>&,bool)> offlineItemDrops;
    std::function<bool(bool)> offlineHeldItemDrop;
    std::function<bool(const ItemStack&)> offlineCreativeDrop;
    // Returns rod durability cost (zero for a successful throw), -1 on rejection.
    std::function<int(const ItemStack&,float)> offlineProjectileUse;
    std::function<void(int,double,double,double)> offlineProjectileExperience;
    std::function<void()> beforeOfflineContainerClose;

    void startPreview(int radiusChunks = 4);
    bool connect(const std::string& host, std::uint16_t port, const std::string& username,
                 std::optional<net::Auth> auth = std::nullopt);
    void disconnect();
    void poll();
    void tick(const Input& input);
    bool sendChat(const std::string& message);
    bool sendTabComplete(const std::string& prefix,const std::optional<std::array<int,3>>& targetBlock=std::nullopt);
    bool updateClientSettings(const std::string& language,int distance,int visibility,bool colors,std::uint8_t parts);
    bool selectSlot(int slot);
    bool respawn();
    bool digBlock(int action, int x, int y, int z, int face);
    bool dropHeldItem(bool entireStack=false);
    void invalidateTile(int x,int y,int z);
    void queueBlockAction(BlockAction event);
    bool queuePickup(int collectedId,int collectorId);
    bool placeBlock(int x, int y, int z, int face, float hitX, float hitY, float hitZ);
    bool swingArm();
    bool attackEntity(int id);
    bool useHeldItem();
    bool setCreativeSlot(int slot, const ItemStack& item);
    bool syncSelectedSlot();
    bool interactEntity(int id, int action = 0, float hitX = 0, float hitY = 0, float hitZ = 0);
    std::int16_t nextWindowTransaction();
    bool sendWindowClick(int windowId, int slot, int button, int mode, std::int16_t action, const ItemStack& returned);
    bool closeContainer();
    bool sendCustomPayload(const std::string& channel,const std::vector<std::uint8_t>& bytes);
    bool sendWindowButton(int button);

    // Decode a complete clientbound PLAY packet; also usable for deterministic
    // trace replay without a socket. Throws ProtocolError on malformed input.
    void consumePacket(const net::Packet& packet);
    static std::string plainChat(const std::string& json);
    static ItemStack readItem(net::Reader& reader);
    static void writeItem(net::Writer& writer, const ItemStack& item);

private:
    double lastReportedX_{}, lastReportedY_{}, lastReportedZ_{};
    float lastReportedYaw_{}, lastReportedPitch_{};
    bool serverSprintState_{}, serverSneakState_{};
    int positionUpdateTicks_{};
    int lastSentSlot_{};
    std::uint16_t inventoryTransaction_{};
    std::size_t tileEntityBytes_{};
    std::uint64_t nextBlockActionSerial_{1};
    std::uint64_t pickupSoundRandomState_{0x5deece66dULL};
    double borderLerpStartDiameter_{60000000.0};
    std::chrono::steady_clock::time_point borderLerpStarted_{};
    bool sendPacket(int id, const std::vector<std::uint8_t>& body);
    void resetPlayState();
    void resetMovementReport();
    void sendSettings();
    void updateEntityMetadata(Entity& entity, net::Reader& reader);
    void updatePlayerEffects();
    void tickLivingEffects();
    bool handleEntityStatus(int id,int status);
    void updateWorldBorder();
    void clearTileData();
    void clearChunkTileData(int x,int z);
};

} // namespace mc
