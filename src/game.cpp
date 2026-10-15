#include "mc/game.hpp"
#include "mc/crafting.hpp"
#include "mc/projectiles.hpp"
#include "mc/entity_effects.hpp"
#include "mc/nbt.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace mc {
namespace {
using net::ProtocolError;
using net::Reader;
using net::Writer;
constexpr std::size_t MaxEntities = 65536;
constexpr std::size_t MaxNbtNodes = 65536;
constexpr std::size_t MaxTileBytes = 64u*1024u*1024u;
int swingDuration(const Entity& entity){
    const auto haste=entity.effects.find(3);if(haste!=entity.effects.end())return 6-(1+haste->second.amplifier);
    const auto fatigue=entity.effects.find(4);return fatigue==entity.effects.end()?6:6+(1+fatigue->second.amplifier)*2;
}
int titleDuration(const Game& game){const std::uint32_t value=static_cast<std::uint32_t>(game.titleFadeIn)+static_cast<std::uint32_t>(game.titleStay)+static_cast<std::uint32_t>(game.titleFadeOut);std::int32_t result;std::memcpy(&result,&value,4);return result;}

void finite(double value, const char* field) {
    if (!std::isfinite(value)) throw ProtocolError(std::string("Non-finite ") + field);
}
int boundedCount(int count, std::size_t limit, const char* description) {
    if (count < 0 || static_cast<std::size_t>(count) > limit)
        throw ProtocolError(std::string("Invalid ") + description + " count");
    return count;
}

struct BlockPosition { int x{}, y{}, z{}; };
int signExtend(std::uint64_t value, unsigned bits) {
    const auto sign = std::uint64_t{1} << (bits - 1);
    const auto mask = (std::uint64_t{1} << bits) - 1;
    value &= mask;
    return static_cast<int>((value & sign) ? static_cast<std::int64_t>(value) - static_cast<std::int64_t>(mask + 1) : value);
}
BlockPosition readPosition(Reader& reader) {
    const auto packed = reader.u64();
    return {signExtend(packed >> 38, 26), signExtend(packed >> 26, 12), signExtend(packed, 26)};
}
void writePosition(Writer& writer, int x, int y, int z) {
    const auto packed = ((static_cast<std::uint64_t>(x) & 0x3ffffffu) << 38) |
        ((static_cast<std::uint64_t>(y) & 0xfffu) << 26) | (static_cast<std::uint64_t>(z) & 0x3ffffffu);
    writer.u64(packed);
}
std::string signJson(const std::string& text) {
    std::string json="{\"text\":\"";
    static constexpr char hex[]="0123456789abcdef";
    for(const unsigned char c:text) {
        if(c=='"'||c=='\\'){json+='\\';json+=static_cast<char>(c);}
        else if(c<32){json+="\\u00";json+=hex[c>>4];json+=hex[c&15];}
        else json+=static_cast<char>(c);
    }
    return json+"\"}";
}
std::string readUuid(Reader& reader) {
    const auto bytes = reader.bytes(16);
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(32);
    for (const auto value : bytes) { result += hex[value >> 4]; result += hex[value & 15]; }
    return result;
}
std::string nbtString(Reader& reader) {
    const auto bytes = reader.bytes(reader.u16());
    return std::string(bytes.begin(), bytes.end());
}
void skipNbtPayload(Reader& reader, int type, unsigned depth, std::size_t& nodes) {
    if (depth > 512 || ++nodes > MaxNbtNodes) throw ProtocolError("NBT nesting/node limit exceeded");
    switch (type) {
    case 0: return;
    case 1: reader.skip(1); return;
    case 2: reader.skip(2); return;
    case 3: case 5: reader.skip(4); return;
    case 4: case 6: reader.skip(8); return;
    case 7: {
        const int count = boundedCount(reader.i32(), reader.remaining(), "NBT byte array");
        reader.skip(static_cast<std::size_t>(count)); return;
    }
    case 8: reader.skip(reader.u16()); return;
    case 9: {
        const auto subtype = reader.u8();
        const auto count = boundedCount(reader.i32(), MaxNbtNodes - std::min(nodes, MaxNbtNodes), "NBT list");
        if (subtype > 11 || (subtype == 0 && count != 0)) throw ProtocolError("Invalid NBT list subtype");
        for (int i = 0; i < count; ++i) skipNbtPayload(reader, subtype, depth + 1, nodes);
        return;
    }
    case 10:
        for (;;) {
            const auto subtype = reader.u8();
            if (subtype == 0) return;
            if (subtype > 11) throw ProtocolError("Invalid NBT compound tag");
            reader.skip(reader.u16());
            skipNbtPayload(reader, subtype, depth + 1, nodes);
        }
    case 11: {
        const int count = boundedCount(reader.i32(), reader.remaining() / 4, "NBT int array");
        reader.skip(static_cast<std::size_t>(count) * 4); return;
    }
    default: throw ProtocolError("Unknown 1.8.9 NBT type");
    }
}
std::vector<std::uint8_t> readNbt(Reader& reader) {
    const auto* start = reader.current();
    const auto startPosition = reader.position();
    const auto type = reader.u8();
    if (type) {
        if (type != 10) throw ProtocolError("Item/tile NBT root must be a compound");
        reader.skip(reader.u16());
        std::size_t nodes = 0;
        skipNbtPayload(reader, type, 0, nodes);
    }
    const auto length = reader.position() - startPosition;
    if (length > net::MaxPacketSize) throw ProtocolError("NBT exceeds protocol size limit");
    return std::vector<std::uint8_t>(start, start + length);
}

int enchantmentLevel(const ItemStack& item, int enchantmentId) {
    if (item.nbt.empty() || item.nbt[0] == 0) return 0;
    try {
        Reader reader(item.nbt);
        if (reader.u8() != 10) return 0;
        reader.skip(reader.u16());
        std::size_t nodes = 0;
        for (;;) {
            const auto type = reader.u8();
            if (!type) return 0;
            const auto name = nbtString(reader);
            if (type == 9 && name == "ench") {
                const auto subtype = reader.u8();
                const auto count = boundedCount(reader.i32(), MaxNbtNodes, "enchantment list");
                int maximum = 0;
                for (int i = 0; i < count; ++i) {
                    if (subtype != 10) { skipNbtPayload(reader, subtype, 1, nodes); continue; }
                    int id = -1, level = 0;
                    for (;;) {
                        const auto field = reader.u8();
                        if (!field) break;
                        const auto key = nbtString(reader);
                        if (field == 2 && key == "id") id = reader.i16();
                        else if (field == 2 && key == "lvl") level = reader.i16();
                        else skipNbtPayload(reader, field, 2, nodes);
                    }
                    if (id == enchantmentId) maximum = std::max(maximum, level);
                }
                return maximum;
            }
            skipNbtPayload(reader, type, 0, nodes);
        }
    } catch (const ProtocolError&) { return 0; }
}

struct Json {
    enum class Kind { Null, String, Number, Boolean, Array, Object } kind{Kind::Null};
    std::string value;
    std::vector<Json> array;
    std::map<std::string, Json> object;
    const Json* get(const std::string& name) const {
        const auto found = object.find(name);
        return found == object.end() ? nullptr : &found->second;
    }
};
void appendUtf8(std::string& result, unsigned cp) {
    if (cp < 0x80) result.push_back(static_cast<char>(cp));
    else if (cp < 0x800) { result.push_back(static_cast<char>(0xc0 | (cp >> 6))); result.push_back(static_cast<char>(0x80 | (cp & 63))); }
    else if (cp < 0x10000) {
        result.push_back(static_cast<char>(0xe0 | (cp >> 12))); result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
        result.push_back(static_cast<char>(0x80 | (cp & 63)));
    } else {
        result.push_back(static_cast<char>(0xf0 | (cp >> 18))); result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 63)));
        result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63))); result.push_back(static_cast<char>(0x80 | (cp & 63)));
    }
}
class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text) {}
    Json parse() {
        if (text_.size() > 131068) throw ProtocolError("Chat JSON length limit");
        auto result = value(0); space();
        if (pos_ != text_.size()) throw ProtocolError("Trailing chat JSON");
        return result;
    }
private:
    const std::string& text_;
    std::size_t pos_{}, nodes_{};
    void space() { while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\r' || text_[pos_] == '\n')) ++pos_; }
    char take() { if (pos_ == text_.size()) throw ProtocolError("Truncated chat JSON"); return text_[pos_++]; }
    unsigned hex4() {
        unsigned result = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = take();
            result <<= 4;
            if (c >= '0' && c <= '9') result += c - '0';
            else if (c >= 'a' && c <= 'f') result += c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') result += c - 'A' + 10;
            else throw ProtocolError("Invalid JSON Unicode escape");
        }
        return result;
    }
    std::string string() {
        if (take() != '"') throw ProtocolError("Expected JSON string");
        std::string out;
        for (;;) {
            const auto c = static_cast<unsigned char>(take());
            if (c == '"') return out;
            if (c < 32) throw ProtocolError("Control character in JSON string");
            if (c != '\\') { out.push_back(static_cast<char>(c)); continue; }
            switch (take()) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = hex4();
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (take() != '\\' || take() != 'u') throw ProtocolError("Unpaired JSON surrogate");
                    const auto low = hex4();
                    if (low < 0xdc00 || low > 0xdfff) throw ProtocolError("Invalid JSON low surrogate");
                    cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                } else if (cp >= 0xdc00 && cp <= 0xdfff) throw ProtocolError("Unpaired JSON low surrogate");
                appendUtf8(out, cp); break;
            }
            default: throw ProtocolError("Invalid JSON string escape");
            }
        }
    }
    Json value(unsigned depth) {
        if (depth > 64 || ++nodes_ > 32768) throw ProtocolError("Chat JSON nesting/node limit");
        space();
        if (pos_ == text_.size()) throw ProtocolError("Empty JSON value");
        Json out;
        const char first = text_[pos_];
        if (first == '"') { out.kind = Json::Kind::String; out.value = string(); return out; }
        if (first == '[') {
            out.kind = Json::Kind::Array; ++pos_; space();
            if (pos_ < text_.size() && text_[pos_] == ']') { ++pos_; return out; }
            for (;;) {
                out.array.push_back(value(depth + 1)); space();
                const char separator = take();
                if (separator == ']') return out;
                if (separator != ',') throw ProtocolError("Invalid JSON array separator");
            }
        }
        if (first == '{') {
            out.kind = Json::Kind::Object; ++pos_; space();
            if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; return out; }
            for (;;) {
                space(); const auto key = string(); space();
                if (take() != ':') throw ProtocolError("Invalid JSON member separator");
                out.object[key] = value(depth + 1); space();
                const char separator = take();
                if (separator == '}') return out;
                if (separator != ',') throw ProtocolError("Invalid JSON object separator");
            }
        }
        for (const std::string literal : {"null", "true", "false"}) {
            if (text_.compare(pos_, literal.size(), literal) == 0) {
                pos_ += literal.size(); out.kind = literal == "null" ? Json::Kind::Null : Json::Kind::Boolean;
                out.value = literal; return out;
            }
        }
        out.kind = Json::Kind::Number;
        const auto start = pos_;
        if (text_[pos_] == '-') ++pos_;
        if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') throw ProtocolError("Invalid JSON number");
        if (text_[pos_] == '0') ++pos_;
        else while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_; const auto digits = pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
            if (pos_ == digits) throw ProtocolError("Invalid JSON fraction");
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_; if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            const auto digits = pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
            if (pos_ == digits) throw ProtocolError("Invalid JSON exponent");
        }
        out.value = text_.substr(start, pos_ - start);
        return out;
    }
};

std::string chatText(const Json& value) {
    if (value.kind == Json::Kind::Null) return "";
    if (value.kind == Json::Kind::String || value.kind == Json::Kind::Number || value.kind == Json::Kind::Boolean) return value.value;
    std::string out;
    if (value.kind == Json::Kind::Array) { for (const auto& child : value.array) out += chatText(child); return out; }
    if (const auto* text = value.get("text")) out = chatText(*text);
    else if (const auto* translation = value.get("translate")) {
        const auto key = translation->value;
        std::vector<std::string> arguments;
        if (const auto* with = value.get("with")) for (const auto& argument : with->array) arguments.push_back(chatText(argument));
        if (key == "chat.type.text" && arguments.size() >= 2) out = "<" + arguments[0] + "> " + arguments[1];
        else if (key == "chat.type.announcement" && arguments.size() >= 2) out = "[" + arguments[0] + "] " + arguments[1];
        else if (key == "chat.type.admin" && arguments.size() >= 2) out = "[" + arguments[0] + ": " + arguments[1] + "]";
        else if (key == "multiplayer.player.joined" && !arguments.empty()) out = arguments[0] + " joined the game";
        else if (key == "multiplayer.player.left" && !arguments.empty()) out = arguments[0] + " left the game";
        else { out = key; for (const auto& argument : arguments) out += " " + argument; }
    } else if (const auto* score = value.get("score")) {
        if (const auto* scoreValue = score->get("value")) out = chatText(*scoreValue);
    } else if (const auto* selector = value.get("selector")) out = selector->value;
    if (const auto* extra = value.get("extra")) for (const auto& child : extra->array) out += chatText(child);
    return out;
}

std::string stripFormatting(const std::string& value) {
    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (static_cast<unsigned char>(value[i]) == 0xc2 && i + 2 < value.size() &&
            static_cast<unsigned char>(value[i + 1]) == 0xa7) {
            char code = value[i + 2]; if (code >= 'A' && code <= 'Z') code = static_cast<char>(code - 'A' + 'a');
            if ((code >= '0' && code <= '9') || (code >= 'a' && code <= 'f') ||
                (code >= 'k' && code <= 'o') || code == 'r') { i += 2; continue; }
        }
        out += value[i];
    }
    return out;
}

void fixedPosition(Entity& entity, Reader& reader) {
    entity.serverX = reader.i32(); entity.serverY = reader.i32(); entity.serverZ = reader.i32();
    entity.x = entity.previousX = entity.targetX = entity.serverX / 32.0;
    entity.y = entity.previousY = entity.targetY = entity.serverY / 32.0;
    entity.z = entity.previousZ = entity.targetZ = entity.serverZ / 32.0;
}
float rotation(Reader& reader) { return static_cast<float>(reader.i8() * 360) / 256.0f; }
void entityTarget(Entity& entity, bool teleport = false) {
    double x = entity.serverX / 32.0, y = entity.serverY / 32.0, z = entity.serverZ / 32.0;
    if (teleport && std::abs(entity.x - x) < .03125 && std::abs(entity.y - y) < .015625 && std::abs(entity.z - z) < .03125) {
        x = entity.x; y = entity.y; z = entity.z;
    }
    entity.targetX = x; entity.targetY = y; entity.targetZ = z; entity.interpolationTicks = 3;
    projectiles::correctPosition(entity);
}
void configureGameMode(Game& game, int mode) {
    game.gameMode = mode >= 0 && mode <= 3 ? mode : 0;
    game.player.spectator = game.gameMode == 3;
    game.player.noClip = game.player.spectator;
    game.creativeMode = game.gameMode == 1;
    game.invulnerable = game.creativeMode || game.player.spectator;
    game.player.allowFlying = game.invulnerable;
    if (game.player.spectator) game.player.flying = true;
    else if (!game.creativeMode) game.player.flying = false;
}
int implicitWindowSlots(const std::string& type) {
    if (type == "minecraft:crafting_table") return 10;
    if (type == "minecraft:furnace" || type == "minecraft:villager" || type == "minecraft:anvil") return 3;
    if (type == "minecraft:dispenser" || type == "minecraft:dropper") return 9;
    if (type == "minecraft:enchanting_table") return 2;
    if (type == "minecraft:brewing_stand") return 4;
    if (type == "minecraft:beacon") return 1;
    if (type == "minecraft:hopper") return 5;
    return 0;
}
} // namespace

std::string Game::plainChat(const std::string& json) {
    try { return stripFormatting(chatText(JsonParser(json).parse())); }
    catch (const ProtocolError&) { return stripFormatting(json); }
}
ItemStack Game::readItem(Reader& reader) {
    ItemStack item;
    item.id = reader.i16();
    if (item.id >= 0) { item.count = reader.i8(); item.damage = reader.i16(); item.nbt = readNbt(reader); }
    return item;
}
void Game::writeItem(Writer& writer, const ItemStack& item) {
    if (item.empty()) { writer.i16(-1); return; }
    writer.i16(item.id).i8(item.count).i16(item.damage);
    if (item.nbt.empty()) writer.u8(0);
    else {
        Reader check(item.nbt); readNbt(check); check.requireEnd();
        writer.bytes(item.nbt);
    }
}

void Game::resetMovementReport() {
    // EntityPlayerSP fields are zero-initialized. S08 acknowledgement does not
    // update these fields; only onUpdateWalkingPlayer does.
    lastReportedX_ = lastReportedY_ = lastReportedZ_ = 0;
    lastReportedYaw_ = lastReportedPitch_ = 0;
    serverSprintState_ = serverSneakState_ = false;
    positionUpdateTicks_ = 0;
}
#include "game_effects.inc"
void Game::resetPlayState() {
    world.clear(); player = Player();
    joined = spawned = false;
    entityId = dimension = gameMode = difficulty = maxPlayers = 0;
    reducedDebugInfo = hardcore = invulnerable = creativeMode = false;
    health = maxHealth = 20; saturation = 5; food = 20; walkSpeed = .1f;
    hurtTime=deathTime=0;maxHurtTime=10;attackedAtYaw=0;deathEvents.clear();
    experienceProgress = 0; experienceLevel = totalExperience = selectedSlot = 0;
    worldAge = worldTime = 0; spawnX = spawnZ = 0; spawnY = 64;
    rainStrength = thunderStrength = 0; raining = false;
    ticks = packetsReceived = packetsSent = positionCorrections = armSwings = 0;
    windowRevision = itemUseCompletions = 0; lastSentSlot_ = 0; inventoryTransaction_ = 0;
    error.clear(); serverBrand.clear(); levelType = "default";
    resourcePackUrl.clear(); resourcePackHash.clear(); title.clear(); subtitle.clear();
    titleJson.clear();subtitleJson.clear();disconnectJson.clear();actionBarText.clear();actionBarJson.clear();titleTicksRemaining=actionBarTicksRemaining=0;chatRevision=0;
    tabCompletions.clear();++tabCompletionRevision;
    titleFadeIn = 10; titleStay = 70; titleFadeOut = 20;
    playerListHeader.clear(); playerListFooter.clear(); cameraEntityId = vehicleId = -1;
    playerListHeaderJson.clear();playerListFooterJson.clear();particleEvents.clear();animationEvents.clear();worldEffectEvents.clear();blockBreakProgress.clear();
    pickupEvents.clear();++pickupGeneration;
    pickupSoundRandomState_=(static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())^0x5deece66dULL)&((1ULL<<48)-1);
    border = ServerWorldBorder(); borderLerpStartDiameter_ = border.diameter; borderLerpStarted_ = std::chrono::steady_clock::now();
    entities.clear(); playerList.clear(); effects.clear(); playerMetadata.clear(); playerAttributes.clear();
    inventory = {}; inventorySlotRevision = {}; cursor = ItemStack(); window = ContainerWindow();
    chat.clear(); soundEvents.clear(); signs.clear(); objectives.clear(); scores.clear(); teams.clear(); displayedObjectives = {};
    clearTileData();
    unhandledPackets.clear();
    editingSign.reset();++signEditorRevision;
    resetMovementReport();
}
void Game::startPreview(int radiusChunks) {
    client.disconnect(); online = false; resetPlayState();
    world.generatePreview(radiusChunks);
    double height = 90;
    for (int y = 255; y >= 0; --y) if (world.getBlock(0, y, 0)) { height = y + 1.0; break; }
    player.teleport(.5, height, .5);
    player.onGround = true;
    joined = spawned = true;
    status = "Offline preview (not vanilla world generation)";
}
bool Game::connect(const std::string& host, std::uint16_t port, const std::string& username, std::optional<net::Auth> auth) {
    client.disconnect(); online = false; resetPlayState();
    status = "Connecting to " + host + ":" + std::to_string(port);
    if (!client.connect(host, port, username, std::move(auth))) { error = client.error(); status = error; return false; }
    online = true; status = "Logging in"; return true;
}
void Game::disconnect() {
    client.disconnect(); online = joined = spawned = false; status = "Disconnected";
    editingSign.reset();++signEditorRevision;
    pickupEvents.clear();++pickupGeneration;
}
bool Game::queuePickup(int collectedId,int collectorId) {
    const auto found=entities.find(collectedId);
    if(found==entities.end())return false;
    EntityPickupEvent event;
    event.entity=found->second;event.tick=ticks;event.collectorId=collectorId;
    const auto collector=entities.find(collectorId);
    if(collectorId==entityId||collector==entities.end()) {
        // NetHandlerPlayClient falls back to the local player for an unknown ID.
        event.collectorId=entityId;
        event.collectorX=player.x;event.collectorY=player.y;event.collectorZ=player.z;
        event.previousCollectorX=player.previousX;event.previousCollectorY=player.previousY;event.previousCollectorZ=player.previousZ;
    } else {
        const auto& target=collector->second;
        event.collectorX=target.x;event.collectorY=target.y;event.collectorZ=target.z;
        event.previousCollectorX=target.previousX;event.previousCollectorY=target.previousY;event.previousCollectorZ=target.previousZ;
    }
    // EffectRenderer keeps at most 4000 effects in each particle layer.
    if(pickupEvents.size()>=4000)pickupEvents.pop_front();
    pickupEvents.push_back(std::move(event));
    return true;
}
bool Game::sendPacket(int id, const std::vector<std::uint8_t>& body) {
    if (outgoingObserver) outgoingObserver(net::Packet{id, body});
    if (client.state() == net::State::Play) {
        if (!client.send(id, body)) { error = client.error(); status = error; return false; }
        ++packetsSent; return true;
    }
    // Offline packet replay tests can capture exact outbound wire bodies.
    if (outgoingObserver) { ++packetsSent; return true; }
    return false;
}
void Game::sendSettings() {
    Writer settings;
    settings.string(locale, 7).i8(static_cast<std::int8_t>(std::clamp(viewDistance, 2, 32))).u8(static_cast<std::uint8_t>(chatVisibility)).boolean(chatColors).u8(skinParts);
    sendPacket(0x15, settings.data);
    Writer brand; brand.string("MC|Brand", 20).string("NativeVoxel");
    sendPacket(0x17, brand.data);
}
void Game::poll() {
    if (!online) return;
    auto packets = client.poll();
    int currentPacketId = -1;
    try {
        for (const auto& packet : packets) { currentPacketId = packet.id; consumePacket(packet); }
    } catch (const std::exception& e) {
        std::ostringstream description; description << "Invalid server play packet 0x" << std::hex << currentPacketId << ": " << e.what();
        error = description.str();
        status = error; client.disconnect(); online = spawned = false; editingSign.reset();++signEditorRevision;return;
    }
    if (client.state() == net::State::Failed || client.state() == net::State::Disconnected) {
        if (error.empty()) error = plainChat(client.error());
        status = error.empty() ? "Disconnected" : error;
        online = spawned = false;
        editingSign.reset();++signEditorRevision;
    }
}
void Game::updatePlayerEffects() {
    const auto jump = effects.find(8);
    player.jumpBoostAmplifier = jump == effects.end() ? -1 : jump->second.amplifier;
    player.blind = effects.find(15) != effects.end();
    if(!online){
        const auto attribute=playerAttributes.find("generic.movementSpeed");
        double speed=attribute==playerAttributes.end()?0.10000000149011612:attribute->second.value;
        const auto haste=effects.find(1),slow=effects.find(2);
        if(haste!=effects.end())speed*=1.+.20000000298023224*(haste->second.amplifier+1);
        if(slow!=effects.end())speed*=1.-.15000000596046448*(slow->second.amplifier+1);
        player.movementSpeed=std::clamp(speed,0.,1024.);
    }
    player.depthStriderLevel = 0;
    for (std::size_t i = 5; i < 9; ++i) player.depthStriderLevel = std::max(player.depthStriderLevel, enchantmentLevel(inventory[i], 8));
}
void Game::updateWorldBorder() {
    if (border.lerpMilliseconds > 0) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - borderLerpStarted_).count();
        const double fraction = std::clamp(static_cast<double>(elapsed) / static_cast<double>(border.lerpMilliseconds), 0.0, 1.0);
        border.diameter = borderLerpStartDiameter_ + (border.targetDiameter - borderLerpStartDiameter_) * fraction;
        if (fraction >= 1.0) border.lerpMilliseconds = 0;
    } else border.diameter = border.targetDiameter;
    world.setWorldBorder(border.centerX, border.centerZ, border.diameter, border.portalBoundary);
}
void Game::tick(const Input& input) {
    ++ticks;
    tickLivingEffects();
    if(titleTicksRemaining>0){--titleTicksRemaining;if(titleTicksRemaining<=0){title.clear();subtitle.clear();titleJson.clear();subtitleJson.clear();}}
    if(actionBarTicksRemaining>0)--actionBarTicksRemaining;
    if(ticks%20==0)for(auto i=blockBreakProgress.begin();i!=blockBreakProgress.end();){if(ticks-i->second.lastUpdateTick>400)i=blockBreakProgress.erase(i);else ++i;}
    updateWorldBorder();
    for (auto& entry : entities) {
        auto& entity = entry.second;
        ++entity.ticksExisted;entity.previousSwingProgress=entity.swingProgress;
        for(auto effect=entity.effects.begin();effect!=entity.effects.end();){if(effect->second.duration>0)--effect->second.duration;if(effect->second.duration==0)effect=entity.effects.erase(effect);else ++effect;}
        entity.swingDuration=swingDuration(entity);
        if(entity.swingInProgress){++entity.swingProgressTicks;if(entity.swingProgressTicks>=entity.swingDuration){entity.swingProgressTicks=0;entity.swingInProgress=false;}}
        else entity.swingProgressTicks=0;
        entity.swingProgress=static_cast<float>(entity.swingProgressTicks)/static_cast<float>(entity.swingDuration);
        ++entity.ageTicks;
        entity.previousX = entity.x; entity.previousY = entity.y; entity.previousZ = entity.z;
        if(entity.kind==EntityKind::Object&&projectiles::supported(entity.type))projectiles::initialize(entity);
        const bool interpolatingHook=entity.projectile&&entity.type==90&&entity.interpolationTicks>0;
        if(interpolatingHook){entity.projectile->previousYaw=entity.yaw;entity.projectile->previousPitch=entity.pitch;}
        if (entity.interpolationTicks > 0) {
            entity.x += (entity.targetX - entity.x) / entity.interpolationTicks;
            entity.y += (entity.targetY - entity.y) / entity.interpolationTicks;
            entity.z += (entity.targetZ - entity.z) / entity.interpolationTicks;
            float yawDifference = std::fmod(entity.targetYaw - entity.yaw, 360.0f);
            if (yawDifference >= 180) yawDifference -= 360;
            if (yawDifference < -180) yawDifference += 360;
            entity.yaw += yawDifference / entity.interpolationTicks;
            entity.pitch += (entity.targetPitch - entity.pitch) / entity.interpolationTicks;
            --entity.interpolationTicks;
        }
        if(entity.projectile&&!interpolatingHook)projectiles::tick(*this,entity);
    }
    projectiles::finishTick(*this);
    if (!joined || !spawned) return;
    if (!world.hasChunk(floorDiv16(floorBlock(player.x)), floorDiv16(floorBlock(player.z)))) {
        player.warnings |= PhysicsMissingChunks; return;
    }
    for (auto iterator = effects.begin(); iterator != effects.end();) {
        if (iterator->second.duration > 0) --iterator->second.duration;
        if (iterator->second.duration == 0) iterator = effects.erase(iterator);
        else ++iterator;
    }
    updatePlayerEffects();
    player.foodLevel = food; player.dead = health <= 0;
    if (vehicleId < 0) player.tick(world, input);
    else player.flightChangesLastTick.clear();
    if (!online) return;
    for (const bool flyingState : player.flightChangesLastTick) {
        Writer abilities;
        abilities.u8((invulnerable ? 1 : 0) | (flyingState ? 2 : 0) | (player.allowFlying ? 4 : 0) | (creativeMode ? 8 : 0))
            .f32(player.flySpeed).f32(walkSpeed);
        sendPacket(0x13, abilities.data);
    }
    if (vehicleId >= 0) {
        sendPacket(0x05, Writer().f32(player.yaw).f32(player.pitch).boolean(player.onGround).data);
        sendPacket(0x0c, Writer().f32(input.strafe).f32(input.forward).u8((input.jump ? 1 : 0) | (input.sneak ? 2 : 0)).data);
        player.warnings |= PhysicsEntityInteractionsUnavailable;
        return;
    }
    if (player.sprinting != serverSprintState_) {
        sendPacket(0x0b, Writer().varInt(entityId).varInt(player.sprinting ? 3 : 4).varInt(0).data);
        serverSprintState_ = player.sprinting;
    }
    if (player.sneaking != serverSneakState_) {
        sendPacket(0x0b, Writer().varInt(entityId).varInt(player.sneaking ? 0 : 1).varInt(0).data);
        serverSneakState_ = player.sneaking;
    }
    if (cameraEntityId >= 0 && cameraEntityId != entityId) return;
    const double dx = player.x - lastReportedX_;
    const double dy = player.bounds().minY - lastReportedY_;
    const double dz = player.z - lastReportedZ_;
    const double yawDifference = static_cast<double>(player.yaw - lastReportedYaw_);
    const double pitchDifference = static_cast<double>(player.pitch - lastReportedPitch_);
    const bool moved = dx * dx + dy * dy + dz * dz > 9.0e-4 || positionUpdateTicks_ >= 20;
    const bool rotated = yawDifference != 0 || pitchDifference != 0;
    Writer movement;
    if (moved) movement.f64(player.x).f64(player.bounds().minY).f64(player.z);
    if (rotated) movement.f32(player.yaw).f32(player.pitch);
    movement.boolean(player.onGround);
    sendPacket(moved ? (rotated ? 0x06 : 0x04) : (rotated ? 0x05 : 0x03), movement.data);
    ++positionUpdateTicks_;
    if (moved) {
        lastReportedX_ = player.x; lastReportedY_ = player.bounds().minY; lastReportedZ_ = player.z; positionUpdateTicks_ = 0;
    }
    if (rotated) { lastReportedYaw_ = player.yaw; lastReportedPitch_ = player.pitch; }
}

bool Game::sendChat(const std::string& message) {
    if (!online || !joined || message.empty()) return false;
    try { return sendPacket(0x01, Writer().string(message, 100).data); }
    catch (const ProtocolError&) { return false; }
}
bool Game::openSignEditor(int x,int y,int z) {
    if(!joined)return false;
    const auto id=blockId(world.getBlock(x,y,z));
    if(!online&&(id!=63&&id!=68))return false;
    if(editingSign&&!finishSignEditing())return false;
    Writer position;writePosition(position,x,y,z);
    Reader packed(position.data);const auto key=packed.u64();
    SignText draft;draft.x=x;draft.y=y;draft.z=z;
    const auto found=signs.find(key);
    if(found!=signs.end())draft=found->second;
    editingSign=std::move(draft);++signEditorRevision;return true;
}
bool Game::finishSignEditing() {
    if(!editingSign)return true;
    if(!joined){editingSign.reset();++signEditorRevision;return false;}
    auto sign=*editingSign;
    Writer body;writePosition(body,sign.x,sign.y,sign.z);
    try {
        for(std::size_t row=0;row<sign.lines.size();++row) {
            sign.jsonLines[row]=signJson(sign.lines[row]);
            body.string(sign.jsonLines[row],384);
        }
    } catch(const ProtocolError&) { return false; }
    Reader packed(body.data);const auto key=packed.u64();
    const auto id=blockId(world.getBlock(sign.x,sign.y,sign.z));
    const bool loaded=world.hasChunk(floorDiv16(sign.x),floorDiv16(sign.z))&&(id==63||id==68);
    std::optional<TileEntityData> savedTile;
    std::size_t retained=tileEntityBytes_;
    if(loaded&&!online) {
        if(signs.size()>=65536&&!signs.count(key))return false;
        const auto previous=tileEntities.find(key);
        if(previous==tileEntities.end()&&tileEntities.size()>=65536)return false;
        try {
            nbt::Tag tag;
            if(previous!=tileEntities.end())tag=nbt::parse(previous->second.nbt);
            tag.type=10;tag.compound["id"]=nbt::text("Sign");
            tag.compound["x"]=nbt::integer(3,sign.x);tag.compound["y"]=nbt::integer(3,sign.y);tag.compound["z"]=nbt::integer(3,sign.z);
            for(std::size_t row=0;row<sign.lines.size();++row)tag.compound["Text"+std::to_string(row+1)]=nbt::text(nbt::modifiedUtf8(sign.jsonLines[row]));
            auto bytes=nbt::encode(tag);
            const auto oldSize=previous==tileEntities.end()?0:previous->second.nbt.size();
            retained-=std::min(retained,oldSize);
            if(bytes.size()>MaxTileBytes-retained)return false;
            savedTile=TileEntityData{sign.x,sign.y,sign.z,0,std::move(bytes)};
        } catch(const ProtocolError&) { return false; }
    }
    if(online&&!sendPacket(0x12,body.data)){editingSign.reset();++signEditorRevision;return false;}
    if(loaded) {
        if(savedTile){tileEntityBytes_=retained+savedTile->nbt.size();tileEntities[key]=std::move(*savedTile);}
        if(signs.size()<65536||signs.count(key))signs[key]=std::move(sign);
    }
    editingSign.reset();++signEditorRevision;return true;
}
bool Game::sendTabComplete(const std::string& prefix,const std::optional<std::array<int,3>>& targetBlock) {
    if(!online||!joined||prefix.empty())return false;
    try {
        Writer body;body.string(prefix,32767).boolean(targetBlock.has_value());
        if(targetBlock)writePosition(body,(*targetBlock)[0],(*targetBlock)[1],(*targetBlock)[2]);
        return sendPacket(0x14,body.data);
    } catch(const ProtocolError&){return false;}
}
bool Game::updateClientSettings(const std::string& language,int distance,int visibility,bool colors,std::uint8_t parts) {
    distance=std::clamp(distance,2,32);visibility=std::clamp(visibility,0,2);parts&=127;
    const bool changed=locale!=language||viewDistance!=distance||chatVisibility!=visibility||chatColors!=colors||skinParts!=parts;
    locale=language;viewDistance=distance;chatVisibility=visibility;chatColors=colors;skinParts=parts;
    if(!changed||!online||!joined)return true;
    try {Writer body;body.string(locale,7).i8(static_cast<std::int8_t>(distance)).u8(static_cast<std::uint8_t>(visibility)).boolean(colors).u8(parts);return sendPacket(0x15,body.data);}
    catch(const ProtocolError&){return false;}
}
bool Game::selectSlot(int slot) {
    if (slot < 0 || slot >= 9) return false;
    if (slot == selectedSlot) return true;
    selectedSlot = slot;
    return syncSelectedSlot();
}
bool Game::syncSelectedSlot() {
    if (selectedSlot < 0 || selectedSlot >= 9) return false;
    if (!online || selectedSlot == lastSentSlot_) return true;
    lastSentSlot_ = selectedSlot;
    return sendPacket(0x09, Writer().i16(static_cast<std::int16_t>(selectedSlot)).data);
}
bool Game::respawn() { return online && joined && sendPacket(0x16, Writer().varInt(0).data); }
bool Game::digBlock(int action, int x, int y, int z, int face) {
    if (!online || !spawned || action < 0 || action > 5 || face < 0 || face > 5) return false;
    Writer body; body.varInt(action); writePosition(body, x, y, z); body.u8(static_cast<std::uint8_t>(face));
    return sendPacket(0x07, body.data);
}
bool Game::dropHeldItem(bool entireStack) {
    if(!joined||!spawned||gameMode==3)return false;
    if(online)return digBlock(entireStack?3:4,0,0,0,0);
    return offlineHeldItemDrop&&offlineHeldItemDrop(entireStack);
}
void Game::clearTileData(){tileEntities.clear();blockActions.clear();tileEntityBytes_=0;}
void Game::queueBlockAction(BlockAction event){event.tick=ticks;event.serial=nextBlockActionSerial_++;blockActions.push_back(event);if(blockActions.size()>1024)blockActions.pop_front();}
void Game::invalidateTile(int x,int y,int z){
    const auto key=((static_cast<std::uint64_t>(x)&0x3ffffffu)<<38)|((static_cast<std::uint64_t>(y)&0xfffu)<<26)|(static_cast<std::uint64_t>(z)&0x3ffffffu);
    const auto found=tileEntities.find(key);if(found!=tileEntities.end()){tileEntityBytes_-=std::min(tileEntityBytes_,found->second.nbt.size());tileEntities.erase(found);}signs.erase(key);
}
void Game::clearChunkTileData(int x,int z){
    for(auto i=tileEntities.begin();i!=tileEntities.end();){if(floorDiv16(i->second.x)==x&&floorDiv16(i->second.z)==z){tileEntityBytes_-=std::min(tileEntityBytes_,i->second.nbt.size());i=tileEntities.erase(i);}else ++i;}
    for(auto i=signs.begin();i!=signs.end();){if(floorDiv16(i->second.x)==x&&floorDiv16(i->second.z)==z)i=signs.erase(i);else ++i;}
    blockActions.erase(std::remove_if(blockActions.begin(),blockActions.end(),[&](const BlockAction& event){return floorDiv16(event.x)==x&&floorDiv16(event.z)==z;}),blockActions.end());
}
bool Game::placeBlock(int x, int y, int z, int face, float hitX, float hitY, float hitZ) {
    if (!online || !spawned || face < 0 || face > 5 || !std::isfinite(hitX) || !std::isfinite(hitY) || !std::isfinite(hitZ)) return false;
    Writer body; writePosition(body, x, y, z); body.u8(static_cast<std::uint8_t>(face));
    writeItem(body, inventory[36 + static_cast<std::size_t>(selectedSlot)]);
    body.u8(static_cast<std::uint8_t>(std::clamp(hitX, 0.0f, 1.0f) * 16.0f))
        .u8(static_cast<std::uint8_t>(std::clamp(hitY, 0.0f, 1.0f) * 16.0f))
        .u8(static_cast<std::uint8_t>(std::clamp(hitZ, 0.0f, 1.0f) * 16.0f));
    return sendPacket(0x08, body.data);
}
bool Game::swingArm() { if(spawned)++armSwings;return online && spawned && sendPacket(0x0a, {}); }
bool Game::attackEntity(int id) { return online && spawned && id != entityId && sendPacket(0x02, Writer().varInt(id).varInt(1).data); }
bool Game::useHeldItem() {
    if (!online || !spawned) return false;
    Writer body; writePosition(body, -1, -1, -1); body.u8(255);
    writeItem(body, inventory[36 + static_cast<std::size_t>(selectedSlot)]);
    body.u8(0).u8(0).u8(0); return sendPacket(0x08, body.data);
}
bool Game::setCreativeSlot(int slot, const ItemStack& item) {
    if (!spawned || !creativeMode || slot < -1 || slot >= 45) return false;
    if(!online)return slot==-1&&offlineCreativeDrop&&offlineCreativeDrop(item);
    Writer body; body.i16(static_cast<std::int16_t>(slot)); writeItem(body, item);
    return sendPacket(0x10, body.data);
}
bool Game::interactEntity(int id, int action, float hitX, float hitY, float hitZ) {
    if (!online || !spawned || id == entityId || (action != 0 && action != 2)) return false;
    Writer body; body.varInt(id).varInt(action);
    if (action == 2) {
        if (!std::isfinite(hitX) || !std::isfinite(hitY) || !std::isfinite(hitZ)) return false;
        body.f32(hitX).f32(hitY).f32(hitZ);
    }
    return sendPacket(0x02, body.data);
}
std::int16_t Game::nextWindowTransaction() {
    auto& transaction = window.open ? window.transactionId : inventoryTransaction_;
    ++transaction;
    std::int16_t value;
    std::memcpy(&value, &transaction, sizeof value);
    return value;
}
bool Game::sendWindowClick(int windowId, int slot, int button, int mode, std::int16_t action, const ItemStack& returned) {
    if (!online || !joined || windowId < 0 || windowId > 255 || slot < -999 || slot > 32767 || button < 0 || button > 255 || mode < 0 || mode > 6)
        return false;
    Writer body; body.u8(static_cast<std::uint8_t>(windowId)).i16(static_cast<std::int16_t>(slot)).u8(static_cast<std::uint8_t>(button))
        .i16(action).u8(static_cast<std::uint8_t>(mode)); writeItem(body, returned);
    return sendPacket(0x0e, body.data);
}
bool Game::closeContainer() {
    if(!online&&beforeOfflineContainerClose)beforeOfflineContainerClose();
    const int id = window.open ? window.id : 0;
    const bool sent = !online || sendPacket(0x0d, Writer().u8(static_cast<std::uint8_t>(id)).data);
    window = ContainerWindow(); cursor = ItemStack(); ++windowRevision; return sent;
}
bool Game::sendCustomPayload(const std::string& channel,const std::vector<std::uint8_t>& bytes) {
    if(!online||!joined||bytes.size()>32767)return false;
    Writer body;body.string(channel,20).bytes(bytes);return sendPacket(0x17,body.data);
}
bool Game::sendWindowButton(int button) {
    return online&&window.open&&button>=0&&button<256&&sendPacket(0x11,Writer().u8(static_cast<std::uint8_t>(window.id)).u8(static_cast<std::uint8_t>(button)).data);
}

void Game::updateEntityMetadata(Entity& entity, Reader& reader) {
    std::size_t count = 0;
    for (;;) {
        const auto header = reader.u8();
        if (header == 127) return;
        if (++count > 32) throw ProtocolError("Too many entity metadata fields");
        const auto index = static_cast<std::uint8_t>(header & 31);
        MetadataValue value; value.type = header >> 5;
        const auto* start = reader.current();
        const auto offset = reader.position();
        switch (value.type) {
        case 0: value.integer = reader.i8(); break;
        case 1: value.integer = reader.i16(); break;
        case 2: value.integer = reader.i32(); break;
        case 3: value.number = reader.f32(); finite(value.number, "metadata float"); break;
        case 4: value.text = reader.string(32767); break;
        case 5: readItem(reader); break;
        case 6: reader.i32(); reader.i32(); reader.i32(); break;
        case 7: finite(reader.f32(), "metadata rotation"); finite(reader.f32(), "metadata rotation"); finite(reader.f32(), "metadata rotation"); break;
        default: throw ProtocolError("Invalid entity metadata type");
        }
        value.raw.assign(start, start + reader.position() - offset);
        if (index == 2 && value.type == 4 && !value.text.empty()) entity.name = value.text;
        entity.metadata[index] = std::move(value);
    }
}

void Game::consumePacket(const net::Packet& packet) {
    if (incomingObserver) incomingObserver(packet);
    ++packetsReceived;
    Reader body(packet.data);
    switch (packet.id) {
    case 0x00: { const auto token = body.varInt(); body.requireEnd(); sendPacket(0, Writer().varInt(token).data); return; }
    case 0x01: {
        const int id = body.i32(); const auto mode = body.u8(); const int dim = body.i8();
        const auto hard = (mode & 8) != 0; const int diff = body.u8(); const int maximum = body.u8();
        const auto level = body.string(16); const bool reduced = body.boolean(); body.requireEnd();
        world.clear(); entities.clear(); signs.clear();clearTileData();particleEvents.clear();animationEvents.clear();worldEffectEvents.clear();blockBreakProgress.clear();pickupEvents.clear();++pickupGeneration; player = Player(); effects.clear(); playerMetadata.clear(); playerAttributes.clear(); resetMovementReport();
        entityId = id; dimension = dim; difficulty = diff; hardcore = hard; maxPlayers = maximum;
        hurtTime=deathTime=0;maxHurtTime=10;attackedAtYaw=0;deathEvents.clear();
        levelType = level; reducedDebugInfo = reduced; configureGameMode(*this, mode & ~8);
        cameraEntityId = entityId; joined = true; spawned = false; status = "Downloading terrain";
        editingSign.reset();++signEditorRevision;
        sendSettings(); return;
    }
    case 0x02: {
        const auto json = body.string(); const auto position = body.u8(); body.requireEnd();
        if(position==2){actionBarJson=json;actionBarText=plainChat(json);actionBarTicksRemaining=60;}
        else{chat.push_back(ChatMessage{plainChat(json), json, position, ticks}); if (chat.size() > 500) chat.pop_front();++chatRevision;}return;
    }
    case 0x03: worldAge = body.i64(); worldTime = body.i64(); break;
    case 0x04: {
        const int id = body.varInt(); const int slot = body.i16(); const auto item = readItem(body); body.requireEnd();
        if (slot >= 0 && slot < 5) {
            const auto found = entities.find(id);
            if (found != entities.end()) found->second.equipment[static_cast<std::size_t>(slot)] = item;
        }
        return;
    }
    case 0x05: { const auto pos = readPosition(body); body.requireEnd(); spawnX = pos.x; spawnY = pos.y; spawnZ = pos.z; return; }
    case 0x06: {
        const float newHealth = body.f32(); const int newFood = body.varInt(); const float newSaturation = body.f32();
        body.requireEnd(); finite(newHealth, "player health"); finite(newSaturation, "food saturation");
        if(spawned&&newHealth<health)hurtTime=maxHurtTime=10;
        health = newHealth; food = newFood; saturation = newSaturation; player.foodLevel = food; player.dead = health <= 0; return;
    }
    case 0x07: {
        const int dim = body.i32(); const int diff = body.u8(); const int mode = body.u8(); const auto level = body.string(16); body.requireEnd();
        editingSign.reset();++signEditorRevision;
        if (dim != dimension) {
            world.clear(); entities.clear(); signs.clear();clearTileData();particleEvents.clear();animationEvents.clear();worldEffectEvents.clear();blockBreakProgress.clear();pickupEvents.clear();++pickupGeneration; spawned = false; border = ServerWorldBorder();
            borderLerpStartDiameter_ = border.diameter; borderLerpStarted_ = std::chrono::steady_clock::now();
        }
        const auto oldId = entityId;
        player = Player(); health = maxHealth = 20; food = 20; saturation = 5; effects.clear(); playerMetadata.clear(); playerAttributes.clear();
        hurtTime=deathTime=0;maxHurtTime=10;attackedAtYaw=0;deathEvents.clear();
        inventory = {}; inventorySlotRevision = {}; cursor = ItemStack(); window = ContainerWindow(); ++windowRevision;
        lastSentSlot_ = 0; inventoryTransaction_ = 0;
        dimension = dim; difficulty = diff; levelType = level; entityId = oldId; cameraEntityId = entityId; vehicleId = -1;
        configureGameMode(*this, mode); resetMovementReport(); status = "Respawning"; return;
    }
    case 0x08: {
        double x = body.f64(), y = body.f64(), z = body.f64(); float yaw = body.f32(), pitch = body.f32(); const auto flags = body.u8();
        body.requireEnd(); finite(x, "teleport X"); finite(y, "teleport Y"); finite(z, "teleport Z"); finite(yaw, "teleport yaw"); finite(pitch, "teleport pitch");
        if (flags & 1) x += player.x; else player.motionX = 0;
        if (flags & 2) y += player.y; else player.motionY = 0;
        if (flags & 4) z += player.z; else player.motionZ = 0;
        if (flags & 8) yaw += player.yaw;
        if (flags & 16) pitch += player.pitch;
        if (spawned) ++positionCorrections;
        player.teleport(x, y, z); player.yaw = yaw; player.pitch = pitch;
        sendPacket(0x06, Writer().f64(player.x).f64(player.bounds().minY).f64(player.z).f32(yaw).f32(pitch).boolean(false).data);
        spawned = true; status = "Connected"; return;
    }
    case 0x09: { const auto slot = body.i8(); body.requireEnd(); if (slot >= 0 && slot < 9) selectedSlot = slot; return; }
    case 0x0a: body.varInt(); readPosition(body); ++unhandledPackets[packet.id]; break;
    case 0x0b:{const int id=body.varInt();const auto animation=body.u8();body.requireEnd();animationEvents.push_back({id,animation,ticks});if(animationEvents.size()>512)animationEvents.pop_front();
        const auto found=entities.find(id);if(animation==0&&found!=entities.end()){auto& entity=found->second;const int duration=swingDuration(entity);if(!entity.swingInProgress||entity.swingProgressTicks>=duration/2||entity.swingProgressTicks<0){entity.swingProgressTicks=-1;entity.swingInProgress=true;entity.swingDuration=duration;}}
        if(animation==1){if(id==entityId){hurtTime=maxHurtTime=10;attackedAtYaw=0;}else if(found!=entities.end()){found->second.hurtTime=found->second.maxHurtTime=10;found->second.attackedAtYaw=0;}}return;}
    case 0x0c: {
        Entity entity; entity.id = body.varInt(); entity.kind = EntityKind::Player; entity.uuid = readUuid(body);
        fixedPosition(entity, body); entity.yaw = entity.targetYaw = rotation(body); entity.pitch = entity.targetPitch = rotation(body);
        const auto item = body.i16();
        if (item > 0) { entity.equipment[0].id = item; entity.equipment[0].count = 1; }
        const auto profile = playerList.find(entity.uuid); if (profile != playerList.end()) entity.name = profile->second.name;
        updateEntityMetadata(entity, body); body.requireEnd();
        if (entities.size() >= MaxEntities && entities.find(entity.id) == entities.end()) throw ProtocolError("Entity limit exceeded");
        entities[entity.id] = std::move(entity); return;
    }
    case 0x0d: {
        const auto collected=body.varInt(),collector=body.varInt();body.requireEnd();
        if(queuePickup(collected,collector)) {
            const auto& entity=pickupEvents.back().entity;
            auto nextFloat=[&](){pickupSoundRandomState_=(pickupSoundRandomState_*0x5deece66dULL+11)&((1ULL<<48)-1);return static_cast<float>(pickupSoundRandomState_>>24)/16777216.f;};
            const float first=nextFloat(),second=nextFloat();
            soundEvents.push_back({entity.kind==EntityKind::ExperienceOrb?"random.orb":"random.pop",entity.x,entity.y,entity.z,.2f,((first-second)*.7f+1.f)*2.f});
            if(soundEvents.size()>256)soundEvents.pop_front();
        }
        entities.erase(collected);return;
    }
    case 0x0e: {
        Entity entity; entity.id = body.varInt(); entity.type = body.i8(); entity.kind = EntityKind::Object;
        fixedPosition(entity, body); entity.pitch = entity.targetPitch = rotation(body); entity.yaw = entity.targetYaw = rotation(body);
        const int data = body.i32();entity.objectData=data;
        if (data > 0) { entity.motionX = body.i16() / 8000.0; entity.motionY = body.i16() / 8000.0; entity.motionZ = body.i16() / 8000.0; }
        body.requireEnd();
        projectiles::initialize(entity);
        if(entity.projectile){
            if(entity.type==90&&data!=entityId){const auto owner=entities.find(data);if(owner==entities.end()||owner->second.kind!=EntityKind::Player)return;}
            if(entity.type==73||entity.type==75||entity.type==90)entity.motionX=entity.motionY=entity.motionZ=0;
            else if(data>0)projectiles::velocity(entity,entity.motionX,entity.motionY,entity.motionZ);
            if(entity.type==73)entity.equipment[0]={373,1,static_cast<std::int16_t>(data),{0}};
        }
        if (entities.size() >= MaxEntities && entities.find(entity.id) == entities.end()) throw ProtocolError("Entity limit exceeded");
        entities[entity.id] = std::move(entity); return;
    }
    case 0x0f: {
        Entity entity; entity.id = body.varInt(); entity.type = body.u8(); entity.kind = EntityKind::Mob;
        fixedPosition(entity, body); entity.yaw = entity.targetYaw = rotation(body); entity.pitch = entity.targetPitch = rotation(body); entity.headYaw = rotation(body);
        entity.motionX = body.i16() / 8000.0; entity.motionY = body.i16() / 8000.0; entity.motionZ = body.i16() / 8000.0;
        updateEntityMetadata(entity, body); body.requireEnd();
        if (entities.size() >= MaxEntities && entities.find(entity.id) == entities.end()) throw ProtocolError("Entity limit exceeded");
        entities[entity.id] = std::move(entity); return;
    }
    case 0x10: {
        Entity entity; entity.id = body.varInt(); entity.kind = EntityKind::Painting; entity.name = body.string(13);
        const auto position = readPosition(body); entity.type = body.u8(); body.requireEnd();
        entity.x = entity.previousX = entity.targetX = position.x + .5;
        entity.y = entity.previousY = entity.targetY = position.y + .5;
        entity.z = entity.previousZ = entity.targetZ = position.z + .5;
        if (entities.size() >= MaxEntities && entities.find(entity.id) == entities.end()) throw ProtocolError("Entity limit exceeded");
        entities[entity.id] = std::move(entity); return;
    }
    case 0x11: {
        Entity entity; entity.id = body.varInt(); entity.kind = EntityKind::ExperienceOrb;
        fixedPosition(entity, body); entity.type = entity.xpValue = body.i16(); body.requireEnd();
        if (entities.size() >= MaxEntities && entities.find(entity.id) == entities.end()) throw ProtocolError("Entity limit exceeded");
        entities[entity.id] = std::move(entity); return;
    }
    case 0x12: {
        const auto id = body.varInt(); const double x = body.i16() / 8000.0, y = body.i16() / 8000.0, z = body.i16() / 8000.0; body.requireEnd();
        if (id == entityId) { player.motionX = x; player.motionY = y; player.motionZ = z; }
        const auto found = entities.find(id); if (found != entities.end()) { projectiles::velocity(found->second,x,y,z); }
        return;
    }
    case 0x13: {
        const auto count = boundedCount(body.varInt(), std::min(body.remaining(), MaxEntities), "destroy entities");
        std::vector<int> ids; for (int i = 0; i < count; ++i) ids.push_back(body.varInt()); body.requireEnd();
        for (const auto id : ids) entities.erase(id);
        return;
    }
    case 0x14: case 0x15: case 0x16: case 0x17: {
        const auto id = body.varInt(); const bool move = packet.id == 0x15 || packet.id == 0x17;
        const bool look = packet.id == 0x16 || packet.id == 0x17;
        int x = 0, y = 0, z = 0; float yaw = 0, pitch = 0;
        if (move) { x = body.i8(); y = body.i8(); z = body.i8(); }
        if (look) { yaw = rotation(body); pitch = rotation(body); }
        const bool grounded = packet.id == 0x14 ? false : body.boolean(); body.requireEnd();
        const auto found = entities.find(id);
        if (found != entities.end()) {
            auto& entity = found->second;
            entity.serverX = static_cast<std::int32_t>(static_cast<std::uint32_t>(entity.serverX) + x);
            entity.serverY = static_cast<std::int32_t>(static_cast<std::uint32_t>(entity.serverY) + y);
            entity.serverZ = static_cast<std::int32_t>(static_cast<std::uint32_t>(entity.serverZ) + z);
            entity.targetYaw = look ? yaw : entity.yaw; entity.targetPitch = look ? pitch : entity.pitch;
            entity.onGround = grounded; entityTarget(entity);
        }
        return;
    }
    case 0x18: {
        const auto id = body.varInt(); const int x = body.i32(), y = body.i32(), z = body.i32();
        const float yaw = rotation(body), pitch = rotation(body); const bool grounded = body.boolean(); body.requireEnd();
        const auto found = entities.find(id); if (found != entities.end()) {
            auto& entity = found->second; entity.serverX = x; entity.serverY = y; entity.serverZ = z;
            entity.targetYaw = yaw; entity.targetPitch = pitch; entity.onGround = grounded; entityTarget(entity, true);
        }
        return;
    }
    case 0x19: { const auto id = body.varInt(); const float yaw = rotation(body); body.requireEnd(); const auto found = entities.find(id); if (found != entities.end()) found->second.headYaw = yaw; return; }
    case 0x1a: {
        const auto id = body.i32(); const auto statusCode = body.i8(); body.requireEnd();
        if (id == entityId && statusCode == 9) ++itemUseCompletions;
        else if (id == entityId && (statusCode == 22 || statusCode == 23)) reducedDebugInfo = statusCode == 22;
        else if(!handleEntityStatus(id,statusCode))++unhandledPackets[packet.id];
        return;
    }
    case 0x1b: {
        const int id = body.i32(), vehicle = body.i32(); const auto leash = body.u8(); body.requireEnd();
        if (id == entityId && leash == 0) vehicleId = vehicle;
        const auto found = entities.find(id); if (found != entities.end()) {
            if (leash == 0) found->second.vehicleId = vehicle; else found->second.leashHolderId = vehicle;
        }
        return;
    }
    case 0x1c: {
        const auto id = body.varInt(); const auto found = entities.find(id);
        Entity scratch; scratch.id = id;
        if (id == entityId) scratch.metadata = playerMetadata;
        auto& target = found == entities.end() ? scratch : found->second;
        updateEntityMetadata(target, body); body.requireEnd();
        if (id == entityId) {
            playerMetadata = target.metadata;
            const auto flags = playerMetadata.find(0);
            if (flags != playerMetadata.end() && flags->second.type == 0) player.sprinting = (flags->second.integer & 8) != 0;
            const auto hp = playerMetadata.find(6);
            if (hp != playerMetadata.end() && hp->second.type == 3) { health = static_cast<float>(hp->second.number); player.dead = health <= 0; }
        }
        return;
    }
    case 0x1d: {
        const auto id = body.varInt(); const auto effectId = body.u8(); Effect effect;
        effect.amplifier = body.i8(); effect.duration = body.varInt(); effect.hideParticles = body.u8() != 0; body.requireEnd();
        if (id == entityId) { effects[effectId] = effect; updatePlayerEffects(); }else if(const auto found=entities.find(id);found!=entities.end())found->second.effects[effectId]=effect;return;
    }
    case 0x1e: { const auto id = body.varInt(); const auto effect = body.u8(); body.requireEnd(); if (id == entityId) { effects.erase(effect); updatePlayerEffects(); }else if(const auto found=entities.find(id);found!=entities.end())found->second.effects.erase(effect); return; }
    case 0x1f: {
        const float progress = body.f32(); const auto level = body.varInt(), total = body.varInt(); body.requireEnd(); finite(progress, "experience progress");
        experienceProgress = progress; experienceLevel = level; totalExperience = total; return;
    }
    case 0x20: {
        const auto id = body.varInt(); const int count = boundedCount(body.i32(), 1024, "entity attribute");
        for (int i = 0; i < count; ++i) {
            const auto name = body.string(64); const double base = body.f64(); finite(base, "attribute base");
            const int modifiers = boundedCount(body.varInt(), std::min<std::size_t>(65536, body.remaining() / 25), "attribute modifier");
            EntityAttribute attribute; attribute.base = base;
            std::vector<EntityAttributeModifier> changes; std::set<std::string> uuids;
            for (int j = 0; j < modifiers; ++j) {
                const auto uuid = readUuid(body); const double amount = body.f64(); const int operation = body.u8(); finite(amount, "attribute modifier");
                if (operation > 2 || !uuids.insert(uuid).second) throw ProtocolError("Invalid/duplicate attribute modifier");
                attribute.modifiers.push_back({uuid, amount, operation});
                if (id != entityId || name != "generic.movementSpeed" || uuid != "662a6b8dda3e4c1c881396ea6097278d")
                    changes.push_back({uuid, amount, operation});
            }
            double afterAddition = base;
            for (const auto& modifier : changes) if (modifier.operation == 0) afterAddition += modifier.amount;
            double total = afterAddition;
            for (const auto& modifier : changes) if (modifier.operation == 1) total += afterAddition * modifier.amount;
            for (const auto& modifier : changes) if (modifier.operation == 2) total *= 1 + modifier.amount;
            finite(total, "effective attribute");
            if (id == entityId && name == "generic.movementSpeed") player.movementSpeed = std::clamp(total, 0.0, 1024.0);
            if (id == entityId && name == "generic.maxHealth") maxHealth = static_cast<float>(std::clamp(total, 0.0, 1024.0));
            attribute.value = total;
            if (id == entityId) {
                if (playerAttributes.size() >= 1024 && playerAttributes.find(name) == playerAttributes.end()) throw ProtocolError("Player attribute limit");
                playerAttributes[name] = std::move(attribute);
            } else {
                const auto entity = entities.find(id);
                if (entity != entities.end()) {
                    if (entity->second.attributes.size() >= 1024 && entity->second.attributes.find(name) == entity->second.attributes.end()) throw ProtocolError("Entity attribute limit");
                    entity->second.attributes[name] = std::move(attribute);
                }
            }
        }
        break;
    }
    case 0x21: {
        const int x = body.i32(), z = body.i32(); const bool groundUp = body.boolean(); const auto mask = body.u16(); const auto bytes = body.byteArray(); body.requireEnd();
        std::string failure;
        if (!world.applyChunk(x, z, groundUp, mask, dimension == 0, bytes.data(), bytes.size(), &failure)) {
            std::ostringstream detail;
            detail << failure << " (S21 dimension=" << dimension << " chunk=" << x << ',' << z << " groundUp=" << groundUp
                << " mask=" << mask << " bytes=" << bytes.size() << " expected=" << World::chunkDataSize(mask,dimension == 0,groundUp) << ')';
            throw ProtocolError(detail.str());
        }
        if(groundUp)clearChunkTileData(x,z);
        return;
    }
    case 0x22: {
        const int chunkX = body.i32(), chunkZ = body.i32();
        if (std::abs(static_cast<std::int64_t>(chunkX)) > 2000000 || std::abs(static_cast<std::int64_t>(chunkZ)) > 2000000) throw ProtocolError("Block-change chunk position out of range");
        const int count = boundedCount(body.varInt(), std::min<std::size_t>(65536, body.remaining() / 3), "block update");
        struct Change { std::uint16_t position; BlockState state; }; std::vector<Change> updates;
        for (int i = 0; i < count; ++i) {
            const auto position = body.u16(); const auto state = body.varInt();
            if (state < 0 || state > 65535) throw ProtocolError("Invalid block state");
            updates.push_back({position, static_cast<BlockState>(state)});
        }
        body.requireEnd();
        if(!world.hasChunk(chunkX,chunkZ))return; // WorldClient's EmptyChunk ignores stale block updates.
        for (const auto& change : updates){const int x=chunkX*16+(change.position>>12),y=change.position&255,z=chunkZ*16+((change.position>>8)&15);
            if(blockId(world.getBlock(x,y,z))!=blockId(change.state))invalidateTile(x,y,z);
            world.setBlock(x,y,z,change.state);}
        return;
    }
    case 0x23: {
        const auto pos = readPosition(body); const auto state = body.varInt(); body.requireEnd();
        if (state < 0 || state > 65535) throw ProtocolError("Invalid block state");
        if(!world.hasChunk(floorDiv16(pos.x),floorDiv16(pos.z)))return;
        if(blockId(world.getBlock(pos.x,pos.y,pos.z))!=blockId(static_cast<BlockState>(state)))invalidateTile(pos.x,pos.y,pos.z);
        world.setBlock(pos.x, pos.y, pos.z, static_cast<BlockState>(state)); return;
    }
    case 0x24:{const auto pos=readPosition(body);BlockAction event;event.x=pos.x;event.y=pos.y;event.z=pos.z;event.action=body.u8();event.parameter=body.u8();event.blockId=body.varInt()&4095;body.requireEnd();queueBlockAction(event);return;}
    case 0x25:{const int id=body.varInt();const auto pos=readPosition(body);const int stage=body.u8();body.requireEnd();if(stage<10){if(blockBreakProgress.size()>=65536&&!blockBreakProgress.count(id))throw ProtocolError("Block break progress limit exceeded");blockBreakProgress[id]={id,pos.x,pos.y,pos.z,stage,ticks};}else blockBreakProgress.erase(id);return;}
    case 0x26: {
        const bool skyLight = body.boolean(); const int count = boundedCount(body.varInt(), std::min<std::size_t>(65536, body.remaining() / 10), "bulk chunk");
        std::vector<BulkChunk> columns; columns.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) { const int x = body.i32(), z = body.i32(); const auto mask = body.u16(); columns.push_back({x, z, mask}); }
        std::string failure;
        if (!world.applyBulk(columns, skyLight, body.current(), body.remaining(), &failure)) throw ProtocolError(failure);
        for(const auto& column:columns)clearChunkTileData(column.x,column.z);
        body.skip(body.remaining()); return;
    }
    case 0x27: {
        const float x = body.f32(), y = body.f32(), z = body.f32(), strength = body.f32();
        finite(x, "explosion X"); finite(y, "explosion Y"); finite(z, "explosion Z"); finite(strength, "explosion strength");
        if (std::abs(x) > 32000000 || std::abs(y) > 32000000 || std::abs(z) > 32000000) throw ProtocolError("Explosion outside world bounds");
        const int count = boundedCount(body.i32(), std::min<std::size_t>(65536, body.remaining() / 3), "explosion block");
        std::vector<BlockPosition> positions;
        for (int i = 0; i < count; ++i) positions.push_back({static_cast<int>(x) + body.i8(), static_cast<int>(y) + body.i8(), static_cast<int>(z) + body.i8()});
        const float mx = body.f32(), my = body.f32(), mz = body.f32(); body.requireEnd(); finite(mx, "explosion motion"); finite(my, "explosion motion"); finite(mz, "explosion motion");
        for (const auto& pos : positions){invalidateTile(pos.x,pos.y,pos.z);world.setBlock(pos.x, pos.y, pos.z, 0);}
        player.motionX += static_cast<double>(mx); player.motionY += static_cast<double>(my); player.motionZ += static_cast<double>(mz); return;
    }
    case 0x28:{WorldEffectEvent event;event.effect=body.i32();const auto pos=readPosition(body);event.x=pos.x;event.y=pos.y;event.z=pos.z;event.data=body.i32();event.global=body.boolean();body.requireEnd();event.tick=ticks;worldEffectEvents.push_back(event);if(worldEffectEvents.size()>1024)worldEffectEvents.pop_front();return;}
    case 0x29: {
        SoundEvent sound; sound.name = body.string(256);
        sound.x = body.i32() / 8.0; sound.y = body.i32() / 8.0; sound.z = body.i32() / 8.0;
        sound.volume = body.f32(); sound.pitch = static_cast<float>(body.u8()) / 63.0f;
        body.requireEnd(); finite(sound.volume, "sound volume");
        soundEvents.push_back(std::move(sound)); if (soundEvents.size() > 256) soundEvents.pop_front();
        return;
    }
    case 0x2a: {
        ParticleEvent event;event.wireType=body.i32();event.type=event.wireType>=0&&event.wireType<42?event.wireType:35;event.longDistance=body.boolean();event.x=body.f32();event.y=body.f32();event.z=body.f32();event.offsetX=body.f32();event.offsetY=body.f32();event.offsetZ=body.f32();event.speed=body.f32();event.count=body.i32();
        for(float value:{event.x,event.y,event.z,event.offsetX,event.offsetY,event.offsetZ,event.speed})finite(value,"particle value");
        const int arguments=event.type==36?2:(event.type==37||event.type==38?1:0);for(int i=0;i<arguments;++i)event.data.push_back(body.varInt());body.requireEnd();event.tick=ticks;particleEvents.push_back(std::move(event));if(particleEvents.size()>2048)particleEvents.pop_front();return;
    }
    case 0x2b: {
        const auto reason = body.u8(); const float value = body.f32(); body.requireEnd(); finite(value, "game state");
        if (reason == 1) { raining = true; rainStrength = 0; }
        else if (reason == 2) { raining = false; rainStrength = 1; }
        else if (reason == 3) configureGameMode(*this, value >= -.5f && value < 3.5f ? static_cast<int>(std::floor(value + .5f)) : 0);
        else if (reason == 7) rainStrength = value;
        else if (reason == 8) thunderStrength = value;
        else ++unhandledPackets[packet.id];
        return;
    }
    case 0x2c: {
        Entity entity; entity.id = body.varInt(); entity.kind = EntityKind::Global; entity.type = body.i8(); fixedPosition(entity, body); body.requireEnd();
        if (entities.size() >= MaxEntities && entities.find(entity.id) == entities.end()) throw ProtocolError("Entity limit exceeded");
        entities[entity.id] = std::move(entity); return;
    }
    case 0x2d: {
        ContainerWindow opened; opened.id = body.u8(); opened.type = body.string(32); opened.title = plainChat(body.string());
        int count = body.u8(); if (opened.type == "EntityHorse") opened.entityId=body.i32(); body.requireEnd();
        if (!count) count = implicitWindowSlots(opened.type);
        opened.slots.resize(static_cast<std::size_t>(count) + 36); opened.open = true; window = std::move(opened); ++windowRevision; return;
    }
    case 0x2e: { const auto id = body.u8(); body.requireEnd(); if (id == window.id) { window = ContainerWindow(); ++windowRevision; } return; }
    case 0x2f: {
        const int id = body.i8(), slot = body.i16(); const auto item = readItem(body); body.requireEnd();
        if (id == -1) cursor = item;
        else if (id == 0 && slot >= 0 && slot < 45) { inventory[static_cast<std::size_t>(slot)] = item; ++inventorySlotRevision[static_cast<std::size_t>(slot)]; }
        else if (window.open && id == window.id && slot >= 0 && static_cast<std::size_t>(slot) < window.slots.size()) {
            window.slots[static_cast<std::size_t>(slot)] = item;
            if (window.slots.size() >= 36 && static_cast<std::size_t>(slot) >= window.slots.size() - 36) {
                const auto inventorySlot = 9 + static_cast<std::size_t>(slot) - (window.slots.size() - 36);
                inventory[inventorySlot] = item; ++inventorySlotRevision[inventorySlot];
            }
        }
        if ((id==0 && slot>=1 && slot<=4) || (window.open && id==window.id && window.type=="minecraft:crafting_table" && slot>=1 && slot<=9)) crafting::refresh(*this);
        if(window.open&&id==window.id&&window.type=="minecraft:villager"&&slot>=0&&slot<2)crafting::refreshMerchant(*this);
        if(window.open&&id==window.id&&window.type=="minecraft:anvil"&&slot>=0&&slot<2)crafting::refreshAnvil(*this);
        if(window.open&&id==window.id&&window.type=="minecraft:enchanting_table"&&slot>=0&&slot<2)crafting::refreshEnchantment(*this);
        updatePlayerEffects(); return;
    }
    case 0x30: {
        const int id = body.u8(); const int count = boundedCount(body.i16(), std::min<std::size_t>(32767, body.remaining() / 2), "inventory slot");
        std::vector<ItemStack> items; items.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) items.push_back(readItem(body));
        body.requireEnd();
        if (id == 0) {
            if (items.size()>inventory.size()) throw ProtocolError("Player inventory packet has too many slots");
            for (std::size_t i=0;i<items.size();++i) { inventory[i]=std::move(items[i]); ++inventorySlotRevision[i]; }
        } else if (window.open && id == window.id) {
            if (items.size()>window.slots.size()) throw ProtocolError("Container inventory packet has too many slots");
            for (std::size_t i=0;i<items.size();++i) {
                window.slots[i]=std::move(items[i]);
                if (window.slots.size()>=36 && i>=window.slots.size()-36) {
                    const auto playerSlot=9+i-(window.slots.size()-36);
                    inventory[playerSlot]=window.slots[i]; ++inventorySlotRevision[playerSlot];
                }
            }
        }
        if (id==0 || (window.open && id==window.id && window.type=="minecraft:crafting_table")) crafting::refresh(*this);
        if(window.open&&id==window.id&&window.type=="minecraft:villager")crafting::refreshMerchant(*this);
        if(window.open&&id==window.id&&window.type=="minecraft:anvil")crafting::refreshAnvil(*this);
        if(window.open&&id==window.id&&window.type=="minecraft:enchanting_table")crafting::refreshEnchantment(*this);
        updatePlayerEffects(); return;
    }
    case 0x31: { const int id = body.u8(), property = body.i16(), value = body.i16(); body.requireEnd(); if (window.open && id == window.id) window.properties[property] = value; return; }
    case 0x32: {
        const auto id = body.u8(); const auto action = body.i16(); const bool accepted = body.boolean(); body.requireEnd();
        if (!accepted && (id == 0 || (window.open && id == window.id))) sendPacket(0x0f, Writer().u8(id).i16(action).boolean(true).data);
        return;
    }
    case 0x33: {
        const auto packed = body.u64(); SignText sign;
        sign.x = signExtend(packed >> 38,26); sign.y = signExtend(packed >> 26,12); sign.z = signExtend(packed,26);
        for (std::size_t i = 0; i < 4; ++i) { sign.jsonLines[i] = body.string(); sign.lines[i] = plainChat(sign.jsonLines[i]); }
        body.requireEnd();
        if(!world.hasChunk(floorDiv16(sign.x),floorDiv16(sign.z)))return;
        if(editingSign&&editingSign->x==sign.x&&editingSign->y==sign.y&&editingSign->z==sign.z)return;
        if (signs.size() >= 65536 && signs.find(packed) == signs.end()) throw ProtocolError("Sign text limit exceeded");
        signs[packed] = std::move(sign); return;
    }
    case 0x34: {
        body.varInt(); body.i8(); const auto icons = boundedCount(body.varInt(), body.remaining() / 3, "map icon"); body.skip(static_cast<std::size_t>(icons) * 3);
        if (body.u8()) { body.u8(); body.u8(); body.u8(); body.byteArray(); }
        ++unhandledPackets[packet.id]; break;
    }
    case 0x35:{const auto packed=body.u64();TileEntityData tile;tile.x=signExtend(packed>>38,26);tile.y=signExtend(packed>>26,12);tile.z=signExtend(packed,26);tile.action=body.u8();tile.nbt=readNbt(body);body.requireEnd();
        if(!world.hasChunk(floorDiv16(tile.x),floorDiv16(tile.z)))return;
        const auto existing=tileEntities.find(packed);if(existing==tileEntities.end()&&tileEntities.size()>=65536)throw ProtocolError("Tile entity count limit exceeded");const auto oldSize=existing==tileEntities.end()?0:existing->second.nbt.size();const auto retained=tileEntityBytes_-std::min(tileEntityBytes_,oldSize);if(tile.nbt.size()>MaxTileBytes-retained)throw ProtocolError("Tile entity NBT byte limit exceeded");tileEntityBytes_=retained+tile.nbt.size();tileEntities[packed]=std::move(tile);return;}
    case 0x36: {const auto pos=readPosition(body);body.requireEnd();openSignEditor(pos.x,pos.y,pos.z);return;}
    case 0x37: {
        const auto count = boundedCount(body.varInt(), std::min<std::size_t>(65536, body.remaining() / 2), "statistics");
        for (int i = 0; i < count; ++i) { body.string(); body.varInt(); } ++unhandledPackets[packet.id]; break;
    }
    case 0x38: {
        const auto action = body.varInt(); const auto count = boundedCount(body.varInt(), std::min<std::size_t>(65536, body.remaining() / 16), "player list");
        if (action < 0 || action > 4) throw ProtocolError("Invalid player list action");
        for (int i = 0; i < count; ++i) {
            const auto uuid = readUuid(body); PlayerInfo info; info.uuid = uuid;
            const auto found = playerList.find(uuid); if (found != playerList.end()) info = found->second;
            if (action == 0) {
                info.name = body.string(16); const int properties = boundedCount(body.varInt(), std::min<std::size_t>(1024, body.remaining() / 3), "profile property");
                info.texturesProperty.clear();info.texturesSignature.clear();
                for (int j = 0; j < properties; ++j) {
                    const auto name=body.string(),value=body.string();const auto signature=body.boolean()?body.string():std::string{};
                    if(name=="textures"){info.texturesProperty=value;info.texturesSignature=signature;}
                }
                info.gameMode = body.varInt(); info.ping = body.varInt();info.displayNameJson.clear();info.displayName.clear();if (body.boolean()){info.displayNameJson=body.string();info.displayName=plainChat(info.displayNameJson);}
            } else if (action == 1) info.gameMode = body.varInt();
            else if (action == 2) info.ping = body.varInt();
            else if (action == 3){info.displayNameJson=body.boolean()?body.string():std::string{};info.displayName=info.displayNameJson.empty()?std::string{}:plainChat(info.displayNameJson);}
            if (action == 4) playerList.erase(uuid);
            else if (action == 0 || found != playerList.end()) {
                if (playerList.size() >= MaxEntities && found == playerList.end()) throw ProtocolError("Player list limit exceeded");
                playerList[uuid] = std::move(info);
            }
        }
        break;
    }
    case 0x39: {
        const auto flags = body.u8(); const float fly = body.f32(), walk = body.f32(); body.requireEnd(); finite(fly, "fly speed"); finite(walk, "walk speed");
        invulnerable = (flags & 1) != 0; player.flying = (flags & 2) != 0; player.allowFlying = (flags & 4) != 0; creativeMode = (flags & 8) != 0;
        player.flySpeed = fly; walkSpeed = walk; return;
    }
    case 0x3a: {
        const auto count=boundedCount(body.varInt(),std::min<std::size_t>(65536,body.remaining()),"tab completion");
        std::vector<std::string> values;values.reserve(count);std::size_t bytes=0;
        for(int i=0;i<count;++i){auto value=body.string();bytes+=value.size();if(bytes>1048576)throw ProtocolError("Tab completion text limit exceeded");values.push_back(std::move(value));}
        body.requireEnd();tabCompletions=std::move(values);++tabCompletionRevision;return;
    }
    case 0x3b: {
        const auto name = body.string(16); const auto action = body.u8();
        if (action == 0 || action == 2) {
            ScoreObjective objective; objective.displayName = body.string(32); objective.renderType = body.string(16); body.requireEnd();
            if (objectives.size() >= 4096 && objectives.find(name) == objectives.end()) throw ProtocolError("Scoreboard objective limit");
            objectives[name] = std::move(objective);
        } else if (action == 1) {
            body.requireEnd(); objectives.erase(name); scores.erase(name);
            for (auto& display : displayedObjectives) if (display == name) display.clear();
        } else throw ProtocolError("Invalid objective action");
        return;
    }
    case 0x3c: {
        const auto name = body.string(40); const auto action = body.varInt(); const auto objective = body.string(16);
        if (action == 0) {
            const auto score = body.varInt(); body.requireEnd();
            if (scores.size() >= 4096 && scores.find(objective) == scores.end()) throw ProtocolError("Scoreboard score limit");
            if (scores[objective].size() >= MaxEntities && scores[objective].find(name) == scores[objective].end()) throw ProtocolError("Scoreboard entry limit");
            scores[objective][name] = score;
        } else if (action == 1) {
            body.requireEnd();
            if (objective.empty()) for (auto& scoresForObjective : scores) scoresForObjective.second.erase(name);
            else { const auto found = scores.find(objective); if (found != scores.end()) found->second.erase(name); }
        } else throw ProtocolError("Invalid score action");
        return;
    }
    case 0x3d: { const auto slot = body.u8(); const auto name = body.string(16); body.requireEnd(); if (slot < displayedObjectives.size()) displayedObjectives[slot] = name; return; }
    case 0x3e: {
        const auto name = body.string(16); const auto action = body.u8();
        if (action > 4) throw ProtocolError("Invalid team action");
        ScoreTeam team; const auto found = teams.find(name); if (found != teams.end()) team = found->second;
        if (action == 0 || action == 2) {
            team.displayName = body.string(32); team.prefix = body.string(16); team.suffix = body.string(16);
            team.flags = body.u8(); team.nameTagVisibility = body.string(32); team.color = body.i8();
        }
        std::vector<std::string> members;
        if (action == 0 || action == 3 || action == 4) {
            const auto count = boundedCount(body.varInt(), std::min<std::size_t>(65536, body.remaining()), "team member");
            for (int i = 0; i < count; ++i) members.push_back(body.string(40));
        }
        body.requireEnd();
        if (action == 1) { teams.erase(name); return; }
        if (action == 0) team.members.clear();
        for (const auto& member : members) {
            if (action == 0 || action == 3) {
                for (auto& other : teams) {
                    auto& existing = other.second.members;
                    existing.erase(std::remove(existing.begin(), existing.end(), member), existing.end());
                }
                if (std::find(team.members.begin(), team.members.end(), member) == team.members.end()) team.members.push_back(member);
            } else team.members.erase(std::remove(team.members.begin(), team.members.end(), member), team.members.end());
        }
        if (teams.size() >= 4096 && found == teams.end()) throw ProtocolError("Scoreboard team limit");
        if (team.members.size() > MaxEntities) throw ProtocolError("Team membership limit");
        teams[name] = std::move(team); return;
    }
    case 0x3f: {
        const auto channel = body.string(20);
        if (channel == "MC|Brand") { serverBrand = body.string(); body.requireEnd(); }
        else if(channel=="MC|TrList") {
            const int id=body.i32();const auto count=body.u8();std::vector<TradeOffer> trades;
            for(unsigned i=0;i<count;++i){TradeOffer offer;offer.first=readItem(body);offer.result=readItem(body);if(body.boolean())offer.second=readItem(body);
                offer.disabled=body.boolean();offer.uses=body.i32();offer.maxUses=body.i32();if(offer.disabled)offer.uses=offer.maxUses;
                if(offer.first.empty()||offer.result.empty())throw ProtocolError("Invalid merchant recipe stack");
                trades.push_back(std::move(offer));}
            body.requireEnd();if(window.open&&window.id==id&&window.type=="minecraft:villager"){window.trades=std::move(trades);window.tradesLoaded=true;crafting::refreshMerchant(*this);}
        }
        else { ++unhandledPackets[packet.id]; body.skip(body.remaining()); }
        return;
    }
    case 0x40: { const auto json = body.string(); body.requireEnd();disconnectJson=json; error = plainChat(json); status = error; spawned = false;editingSign.reset();++signEditorRevision;return; }
    case 0x41: difficulty = body.u8(); break;
    case 0x42: {
        const auto event = body.varInt();
        if (event == 1) { body.varInt(); body.i32(); }
        else if (event == 2) {
            const auto deadPlayer = body.varInt(); body.i32(); const auto json = body.string();
            if (deadPlayer == entityId) { health = 0; player.dead = true; chat.push_back({plainChat(json), json, 0, ticks}); if (chat.size() > 500) chat.pop_front();++chatRevision; }
        } else if (event != 0) throw ProtocolError("Invalid combat event");
        break;
    }
    case 0x43: cameraEntityId = body.varInt(); break;
    case 0x44: {
        const auto action = body.varInt();
        if (action == 0) { border.diameter = border.targetDiameter = body.f64(); border.lerpMilliseconds = 0; }
        else if (action == 1) { border.diameter = body.f64(); border.targetDiameter = body.f64(); border.lerpMilliseconds = body.varLong(); }
        else if (action == 2) { border.centerX = body.f64(); border.centerZ = body.f64(); }
        else if (action == 3) {
            border.centerX = body.f64(); border.centerZ = body.f64(); border.diameter = body.f64(); border.targetDiameter = body.f64();
            border.lerpMilliseconds = body.varLong(); border.portalBoundary = body.varInt(); border.warningDistance = body.varInt(); border.warningTime = body.varInt();
        } else if (action == 4) border.warningTime = body.varInt();
        else if (action == 5) border.warningDistance = body.varInt();
        else throw ProtocolError("Invalid world border action");
        finite(border.centerX, "border center"); finite(border.centerZ, "border center"); finite(border.diameter, "border diameter"); finite(border.targetDiameter, "border target");
        if (border.lerpMilliseconds < 0) throw ProtocolError("Negative world-border transition time");
        if (action == 0 || action == 1 || action == 3) {
            borderLerpStartDiameter_ = border.diameter;
            borderLerpStarted_ = std::chrono::steady_clock::now();
        }
        body.requireEnd(); updateWorldBorder(); return;
    }
    case 0x45: {
        const auto action = body.varInt();
        if(action==0){const auto json=body.string();body.requireEnd();titleJson=json;title=plainChat(json);titleTicksRemaining=titleDuration(*this);}
        else if(action==1){const auto json=body.string();body.requireEnd();subtitleJson=json;subtitle=plainChat(json);}
        else if(action==2){const int in=body.i32(),stay=body.i32(),out=body.i32();body.requireEnd();if(in>=0)titleFadeIn=in;if(stay>=0)titleStay=stay;if(out>=0)titleFadeOut=out;if(titleTicksRemaining>0)titleTicksRemaining=titleDuration(*this);}
        else if(action==3){body.requireEnd();title.clear();subtitle.clear();titleJson.clear();subtitleJson.clear();titleTicksRemaining=0;}
        else if(action==4){body.requireEnd();title.clear();titleJson.clear();titleTicksRemaining=titleDuration(*this);titleFadeIn=10;titleStay=70;titleFadeOut=20;}
        else throw ProtocolError("Invalid title action");return;
    }
    case 0x46: body.varInt(); break; // Applied by Client before packet delivery; replay accepts the control body.
    case 0x47:{const auto header=body.string(),footer=body.string();body.requireEnd();playerListHeaderJson=header;playerListFooterJson=footer;playerListHeader=plainChat(header);playerListFooter=plainChat(footer);return;}
    case 0x48: {
        resourcePackUrl = body.string(); resourcePackHash = body.string(40); body.requireEnd();
        // Native download/activation is not implemented. DECLINED is an honest
        // protocol state; never report that an uninstalled pack loaded.
        sendPacket(0x19, Writer().string(resourcePackHash, 40).varInt(1).data); ++unhandledPackets[packet.id]; return;
    }
    case 0x49: body.varInt(); readNbt(body); ++unhandledPackets[packet.id]; break;
    default: ++unhandledPackets[packet.id]; body.skip(body.remaining()); break;
    }
    body.requireEnd();
}

} // namespace mc
