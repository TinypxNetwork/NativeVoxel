#include "mc/simulation.hpp"
#include "mc/item_nbt.hpp"

#include <cmath>
#include <limits>

namespace mc {
namespace {
using net::ProtocolError;
const nbt::Tag &required(const nbt::Tag &tag, const char *name, int type) {
    const auto *value = tag.get(name);
    if (!value || value->type != type)
        throw ProtocolError(std::string("Missing/invalid Anvil entity tag ") + name);
    return *value;
}
int integer(const nbt::Tag &tag, const char *name, int fallback, int minimum, int maximum) {
    const auto *value = tag.get(name);
    if (!value)
        return fallback;
    if (value->type < 1 || value->type > 4 || value->integer < minimum || value->integer > maximum)
        throw ProtocolError(std::string("Invalid Anvil entity integer ") + name);
    return int(value->integer);
}
double real(const nbt::Tag &tag, const char *name, double fallback = 0) {
    const auto *value = tag.get(name);
    if (!value)
        return fallback;
    if ((value->type != 5 && value->type != 6) || !std::isfinite(value->number))
        throw ProtocolError(std::string("Invalid Anvil entity number ") + name);
    return value->number;
}
const nbt::Tag &vector(const nbt::Tag &tag, const char *name, int type, std::size_t size) {
    const auto &value = required(tag, name, 9);
    if (value.subtype != type || value.list.size() != size)
        throw ProtocolError(std::string("Invalid Anvil entity vector ") + name);
    for (const auto &entry : value.list)
        if (entry.type != type || !std::isfinite(entry.number))
            throw ProtocolError(std::string("Non-finite Anvil entity vector ") + name);
    return value;
}
nbt::Tag floating(int type, double number) {
    nbt::Tag result;
    result.type = type;
    result.number = number;
    return result;
}
nbt::Tag vector(int type, std::initializer_list<double> numbers) {
    nbt::Tag result;
    result.type = 9;
    result.subtype = type;
    for (const auto number : numbers)
        result.list.push_back(floating(type, number));
    return result;
}
std::uint64_t chunkKey(int x, int z) {
    return (std::uint64_t(std::uint32_t(x)) << 32) | std::uint32_t(z);
}
} // namespace

bool Simulation::hasAnvilEntityChunk(int x, int z) const {
    return anvilEntityChunks_.count(chunkKey(x, z)) != 0;
}

void Simulation::importAnvilEntities(Game &game, const std::vector<nbt::Tag> &entities) {
    if (game.online || !running_)
        throw std::logic_error("Anvil entity import requires an active offline simulation");
    if (entities.size() > 65536 - std::min<std::size_t>(65536, droppedEntities_.size()))
        throw ProtocolError("Native dropped entity limit exceeded");
    std::map<int, drop_entities::State> imported;
    int next = nextEntityId_;
    auto seeds = worldgen::JavaRandom(static_cast<std::int64_t>(entitySeedState_ ^ 0x5deece66dULL));
    for (const auto &tag : entities) {
        if (tag.type != 10)
            throw ProtocolError("Anvil entity must be a compound");
        const auto &type = required(tag, "id", 8).string;
        if (type != "Item" && type != "XPOrb")
            throw ProtocolError("Anvil simulation supports only Item and XPOrb");
        while (next < 2000000000 && (game.entities.count(next) || droppedEntities_.count(next)))
            ++next;
        if (next < 1 || next >= 2000000000)
            throw ProtocolError("Native entity ID space exhausted");
        drop_entities::State entity;
        entity.id = next++;
        entity.orb = type == "XPOrb";
        // EntityXPOrb(World), used by Anvil loading, uses 0.25 rather than the
        // 0.5 dimensions of the constructor for freshly spawned experience.
        entity.width = entity.height = .25f;
        entity.random.setSeed(seeds.nextLong());
        entity.age = integer(tag, "Age", 0, -32768, 5999);
        entity.health = integer(tag, "Health", 5, -32768, 32767) & 255;
        if (!entity.health)
            throw ProtocolError("Invalid Anvil entity health");
        if (const auto *invulnerable = tag.get("Invulnerable"); invulnerable &&
            (invulnerable->type != 1 || invulnerable->integer < 0 || invulnerable->integer > 1))
            throw ProtocolError("Invalid Anvil entity invulnerability flag");
        entity.pickupDelay = entity.orb ? 0 : std::max(0, integer(tag, "PickupDelay", 0, -32768, 32767));
        entity.fire = integer(tag, "Fire", 0, -32768, 32767);
        entity.onGround = integer(tag, "OnGround", 0, 0, 1) != 0;
        entity.fallDistance = float(real(tag, "FallDistance"));
        if (!std::isfinite(entity.fallDistance))
            throw ProtocolError("Invalid Anvil entity fall distance");
        const auto &position = vector(tag, "Pos", 6, 3);
        const auto &motion = vector(tag, "Motion", 6, 3);
        const auto &rotation = vector(tag, "Rotation", 5, 2);
        entity.setPosition(position.list[0].number, position.list[1].number, position.list[2].number);
        entity.previousX = entity.x;
        entity.previousY = entity.y;
        entity.previousZ = entity.z;
        entity.motionX = std::abs(motion.list[0].number) > 10 ? 0 : motion.list[0].number;
        entity.motionY = std::abs(motion.list[1].number) > 10 ? 0 : motion.list[1].number;
        entity.motionZ = std::abs(motion.list[2].number) > 10 ? 0 : motion.list[2].number;
        entity.yaw = float(rotation.list[0].number);
        if (!std::isfinite(entity.yaw) || !std::isfinite(float(rotation.list[1].number)))
            throw ProtocolError("Invalid Anvil entity rotation");
        if (entity.orb)
            entity.value = integer(tag, "Value", 0, 1, 32767);
        else {
            const auto &item = required(tag, "Item", 10);
            const auto *id = item.get("id");
            if (!id || (id->type != 8 && id->type != 2))
                throw ProtocolError("Invalid Anvil item ID");
            integer(item, "Count", 0, 1, 127);
            integer(item, "Damage", 0, -32768, 32767);
            if (const auto *extra = item.get("tag"); extra && extra->type != 10)
                throw ProtocolError("Invalid Anvil item compound");
            entity.item = item_nbt::read(item);
            if (entity.item.empty())
                throw ProtocolError("Invalid/unknown Anvil item stack");
            for (const auto *name : {"Owner", "Thrower"})
                if (const auto *value = tag.get(name)) {
                    if (value->type != 8 || value->string.size() > 64)
                        throw ProtocolError("Invalid Anvil item owner/thrower");
                    (name[0] == 'O' ? entity.owner : entity.thrower) = value->string;
                }
        }
        if (entity.orb && !entity.value)
            throw ProtocolError("Missing Anvil XP value");
        entity.anvilData = tag;
        imported.emplace(entity.id, std::move(entity));
    }
    // No entity, allocator or random state is changed by a malformed batch.
    nextEntityId_ = next;
    entitySeedState_ = seeds.internalSeed();
    for (auto &entry : imported) {
        const auto id = entry.first;
        droppedEntities_.emplace(id, std::move(entry.second));
        publishDroppedEntity(game, droppedEntities_.at(id));
    }
}

std::vector<nbt::Tag> Simulation::exportAnvilEntities(int dimension) const {
    std::vector<nbt::Tag> result;
    result.reserve(droppedEntities_.size());
    for (const auto &entry : droppedEntities_) {
        const auto &entity = entry.second;
        if (entity.dead)
            continue;
        auto tag = entity.anvilData;
        tag.type = 10;
        tag.compound["id"] = nbt::text(entity.orb ? "XPOrb" : "Item");
        tag.compound["Pos"] = vector(6, {entity.x, entity.y, entity.z});
        tag.compound["Motion"] = vector(6, {entity.motionX, entity.motionY, entity.motionZ});
        double pitch = 0;
        if (const auto *rotation = tag.get("Rotation"); rotation && rotation->type == 9 &&
            rotation->subtype == 5 && rotation->list.size() == 2)
            pitch = rotation->list[1].number;
        tag.compound["Rotation"] = vector(5, {entity.yaw, pitch});
        tag.compound["Age"] = nbt::integer(2, entity.age);
        // EntityItem/EntityXPOrb write the low signed byte as a TAG_Short.
        tag.compound["Health"] = nbt::integer(2, static_cast<std::int8_t>(entity.health));
        tag.compound["Fire"] = nbt::integer(2, entity.fire);
        tag.compound["FallDistance"] = floating(5, entity.fallDistance);
        tag.compound["OnGround"] = nbt::integer(1, entity.onGround);
        tag.compound["Dimension"] = nbt::integer(3, dimension);
        if (!tag.get("Air"))
            tag.compound["Air"] = nbt::integer(2, 300);
        if (!tag.get("Invulnerable"))
            tag.compound["Invulnerable"] = nbt::integer(1, 0);
        if (!tag.get("PortalCooldown"))
            tag.compound["PortalCooldown"] = nbt::integer(3, 0);
        if (entity.orb)
            tag.compound["Value"] = nbt::integer(2, entity.value);
        else {
            tag.compound["PickupDelay"] = nbt::integer(2, entity.pickupDelay);
            tag.compound["Item"] = item_nbt::write(entity.item);
            if (entity.owner.empty())
                tag.compound.erase("Owner");
            else
                tag.compound["Owner"] = nbt::text(entity.owner);
            if (entity.thrower.empty())
                tag.compound.erase("Thrower");
            else
                tag.compound["Thrower"] = nbt::text(entity.thrower);
        }
        result.push_back(std::move(tag));
    }
    return result;
}
} // namespace mc
