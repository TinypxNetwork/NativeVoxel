#include "mc/game.hpp"
#include "mc/nbt.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mc;
using mc::net::Packet;
using mc::net::Reader;
using mc::net::Writer;

namespace {
std::size_t checks = 0;
void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, const std::string& message, double tolerance = 1e-12) {
    require(std::abs(actual - expected) <= tolerance, message);
}
template<class Function> void rejects(Function function, const std::string& message) {
    bool threw = false;
    try { function(); } catch (const std::exception&) { threw = true; }
    require(threw, message);
}
std::uint64_t position(int x, int y, int z) {
    return ((static_cast<std::uint64_t>(x) & 0x3ffffffu) << 38) |
        ((static_cast<std::uint64_t>(y) & 0xfffu) << 26) | (static_cast<std::uint64_t>(z) & 0x3ffffffu);
}
Writer& uuid(Writer& out) { return out.u64(0x1234567812341234ull).u64(0x12341234567890abull); }
void packet(Game& game, int id, const Writer& body) { game.consumePacket(Packet{id, body.data}); }
std::vector<std::uint8_t> floorChunk(bool sky = true) {
    std::vector<std::uint8_t> bytes(World::chunkDataSize(1u << 3, sky, true), 0);
    for (int z = 0; z < 16; ++z) for (int x = 0; x < 16; ++x) {
        const std::size_t index = static_cast<std::size_t>((15 << 8) | (z << 4) | x) * 2;
        bytes[index] = static_cast<std::uint8_t>(blockState(1));
        bytes[index + 1] = static_cast<std::uint8_t>(blockState(1) >> 8);
    }
    return bytes;
}
void loadFloor(Game& game, int x = 0, int z = 0) {
    Writer body;
    body.i32(x).i32(z).boolean(true).u16(1u << 3).byteArray(floorChunk(game.dimension == 0));
    packet(game, 0x21, body);
}
void join(Game& game) {
    packet(game, 1, Writer().i32(42).u8(0).i8(0).u8(2).u8(20).string("default").boolean(false));
}
void teleport(Game& game, double x = .5, double y = 64, double z = .5, float yaw = 0, float pitch = 0, int flags = 0) {
    packet(game, 8, Writer().f64(x).f64(y).f64(z).f32(yaw).f32(pitch).u8(static_cast<std::uint8_t>(flags)));
}

void movementTests() {
    Game game; std::vector<Packet> emitted;
    game.outgoingObserver = [&](const Packet& value) { emitted.push_back(value); };
    game.online = true; join(game);
    require(game.joined && !game.spawned && game.entityId == 42 && game.dimension == 0, "Join Game state");
    require(emitted.size() == 2 && emitted[0].id == 0x15 && emitted[1].id == 0x17, "Join sends settings then brand");
    Reader settings(emitted[0].data);
    require(settings.string() == "zh_CN" && settings.i8() == 8 && settings.u8() == 0 && settings.boolean() && settings.u8() == 127, "client settings wire fields");
    settings.requireEnd(); Reader brand(emitted[1].data);
    require(brand.string() == "MC|Brand" && brand.string() == "NativeVoxel", "honest brand"); brand.requireEnd();
    emitted.clear(); game.tick(Input{}); require(emitted.empty(), "no movement before first teleport");

    game.player.motionX = 1; game.player.motionY = 2; game.player.motionZ = 3;
    teleport(game);
    require(game.spawned && emitted.size() == 1 && emitted[0].id == 6, "teleport acknowledgement");
    Reader acknowledgement(emitted[0].data);
    require(acknowledgement.f64() == .5 && acknowledgement.f64() == 64 && acknowledgement.f64() == .5 &&
        acknowledgement.f32() == 0 && acknowledgement.f32() == 0 && !acknowledgement.boolean(), "S08 acknowledgement contains feet Y and false onGround");
    acknowledgement.requireEnd();
    require(game.player.motionX == 0 && game.player.motionY == 0 && game.player.motionZ == 0, "absolute teleport resets velocity");
    emitted.clear(); game.tick(Input{}); require(emitted.empty(), "no movement in unloaded current chunk");
    loadFloor(game); game.tick(Input{});
    require(emitted.size() == 1 && emitted[0].id == 4, "first walking update preserves zero-initialized lastReported position");
    Reader first(emitted[0].data); first.f64(); first.f64(); first.f64(); require(!first.boolean(), "first zero-motion tick keeps previous false ground state");
    // Gravity is applied after movement; the following tick actually collides.
    emitted.clear(); game.tick(Input{});
    require(emitted.size() == 1 && emitted[0].id == 3 && emitted[0].data == std::vector<std::uint8_t>{1}, "standing C03 after collision");
    for (int i = 0; i < 19; ++i) {
        emitted.clear(); game.tick(Input{}); require(emitted.size() == 1 && emitted[0].id == 3, "stationary ground heartbeat");
    }
    emitted.clear(); game.tick(Input{});
    require(emitted.size() == 1 && emitted[0].id == 4, "position refresh after positionUpdateTicks reaches 20");
    game.player.yaw = 22.5f; emitted.clear(); game.tick(Input{});
    require(emitted.size() == 1 && emitted[0].id == 5, "rotation-only C05");
    Reader look(emitted[0].data); require(look.f32() == 22.5f && look.f32() == 0 && look.boolean(), "C05 payload");
    game.player.yaw = 0;
    Input sprint; sprint.forward = 1; sprint.sprint = true; emitted.clear(); game.tick(sprint);
    require(emitted.size() == 2 && emitted[0].id == 0x0b && emitted[1].id == 6, "sprint action precedes position/look packet");
    Reader sprintAction(emitted[0].data); require(sprintAction.varInt() == 42 && sprintAction.varInt() == 3 && sprintAction.varInt() == 0, "start sprint action");
    Input sneak; sneak.sneak = true; emitted.clear(); game.tick(sneak);
    require(emitted.size() >= 3 && emitted[0].id == 0x0b && emitted[1].id == 0x0b, "stop sprint before start sneak");
    Reader stop(emitted[0].data); stop.varInt(); require(stop.varInt() == 4, "stop sprint action ID");
    Reader startSneak(emitted[1].data); startSneak.varInt(); require(startSneak.varInt() == 0, "start sneak action ID");

    const auto oldX = game.player.x, oldY = game.player.y, oldZ = game.player.z;
    const auto oldYaw = game.player.yaw, oldPitch = game.player.pitch;
    game.player.motionX = .1; game.player.motionY = .2; game.player.motionZ = .3;
    emitted.clear(); teleport(game, 1, 2, 3, 4, 5, 31);
    near(game.player.x, oldX + 1, "relative X"); near(game.player.y, oldY + 2, "relative Y"); near(game.player.z, oldZ + 3, "relative Z");
    require(game.player.yaw == oldYaw + 4 && game.player.pitch == oldPitch + 5, "relative rotation flags");
    require(game.player.motionX == .1 && game.player.motionY == .2 && game.player.motionZ == .3, "relative teleport preserves velocity");
    require(game.positionCorrections == 1, "subsequent correction counter");
    game.player.motionX = game.player.motionY = game.player.motionZ = 0;
    emitted.clear(); teleport(game, .5, 64, .5, 0, 0, 0); game.tick(Input{});
    require(emitted.size() >= 2, "teleport ack and ordinary walking report remain distinct");
    packet(game, 0x21, Writer().i32(0).i32(0).boolean(true).u16(0).byteArray({}));
    emitted.clear(); game.tick(sprint); require(emitted.empty() && !game.world.hasChunk(0,0), "chunk unload suppresses simulation packets");
}

void chunkAndStateTests() {
    Game game; join(game); loadFloor(game);
    require(game.world.getBlock(15,63,15) == blockState(1), "chunk wire payload applied");
    loadFloor(game,-1,-1);loadFloor(game,-1,1);
    packet(game, 0x23, Writer().u64(position(-1,64,-2)).varInt(blockState(35,14)));
    require(game.world.getBlock(-1,64,-2) == blockState(35,14), "negative packed block position");
    packet(game, 0x22, Writer().i32(-1).i32(1).varInt(2).u16(0xf040).varInt(blockState(5,2)).u16(0x0f41).varInt(blockState(20)));
    require(game.world.getBlock(-1,64,16) == blockState(5,2) && game.world.getBlock(-16,65,31) == blockState(20), "multiblock local XZ encoding");
    Writer bulk; bulk.boolean(true).varInt(2).i32(1).i32(-1).u16(1u << 3).i32(2).i32(-1).u16(1u << 3).bytes(floorChunk()).bytes(floorChunk());
    packet(game, 0x26, bulk);
    require(game.world.getBlock(16,63,-16) == blockState(1) && game.world.getBlock(32,63,-16) == blockState(1), "bulk metadata then concatenated data");
    const auto revision = game.world.revision();
    Writer bad; bad.i32(5).i32(5).boolean(true).u16(8).byteArray({1});
    rejects([&] { packet(game, 0x21, bad); }, "invalid chunk shape");
    require(game.world.revision() == revision, "invalid chunk does not mutate world");
    packet(game, 0x06, Writer().f32(17.5f).varInt(9).f32(2.0f));
    require(game.health == 17.5f && game.food == 9 && game.player.foodLevel == 9, "health/food state");
    packet(game, 0x1c, Writer().varInt(42).u8(0).i8(8).u8((3 << 5) | 6).f32(12.5f).u8(127));
    require(game.player.sprinting && game.health == 12.5f && game.playerMetadata.count(6), "local metadata updates health and sprint flag");
    packet(game, 0x1f, Writer().f32(.25f).varInt(12).varInt(345));
    require(game.experienceProgress == .25f && game.experienceLevel == 12 && game.totalExperience == 345, "experience field ordering");
    packet(game, 0x12, Writer().varInt(42).i16(-1200).i16(3200).i16(16000));
    near(game.player.motionX, -.15, "velocity X /8000"); near(game.player.motionY, .4, "velocity Y /8000"); near(game.player.motionZ, 2, "velocity Z /8000");
    game.world.setBlock(-1,63,0,blockState(1));
    packet(game, 0x27, Writer().f32(-1.8f).f32(63.5f).f32(.2f).f32(4).i32(1).i8(0).i8(0).i8(0).f32(.125f).f32(.25f).f32(-.5f));
    require(game.world.getBlock(-1,63,0) == 0, "explosion coordinates use Java truncation toward zero");
    near(game.player.motionX, -.025, "explosion adds X motion"); near(game.player.motionY, .65, "explosion adds Y motion"); near(game.player.motionZ, 1.5, "explosion adds Z motion");
    Writer attribute; attribute.varInt(42).i32(1).string("generic.movementSpeed").f64(.1).varInt(4);
    attribute.u64(1).u64(1).f64(.1).u8(0);
    attribute.u64(2).u64(2).f64(.5).u8(1);
    attribute.u64(3).u64(3).f64(.2).u8(2);
    attribute.u64(0x662a6b8dda3e4c1cull).u64(0x881396ea6097278dull).f64(.30000001192092896).u8(2);
    packet(game, 0x20, attribute);
    near(game.player.movementSpeed, .36, "attribute operation ordering excludes local sprint UUID");
    require(game.playerAttributes.at("generic.movementSpeed").modifiers.size() == 4, "raw attribute modifiers retained");
    packet(game, 0x39, Writer().u8(15).f32(.07f).f32(.3f));
    require(game.player.flying && game.player.allowFlying && game.invulnerable && game.creativeMode, "ability bits");
    near(game.player.movementSpeed, .36, "abilities walk speed must not replace movement attribute");
    packet(game, 0x1d, Writer().varInt(42).u8(8).u8(2).varInt(120).u8(0));
    packet(game, 0x1d, Writer().varInt(42).u8(15).u8(0).varInt(30).u8(0));
    require(game.player.jumpBoostAmplifier == 2 && game.player.blind, "movement-relevant effects");
    packet(game, 0x1e, Writer().varInt(42).u8(8)); require(game.player.jumpBoostAmplifier == -1, "remove effect");
    packet(game, 0x3b, Writer().string("board").u8(0).string("Title").string("integer"));
    packet(game, 0x44, Writer().varInt(3).f64(10).f64(-20).f64(40).f64(40).varLong(0).varInt(29999984).varInt(5).varInt(15));
    require(game.world.border().minX == -10 && game.world.border().maxX == 30 && game.world.border().minZ == -40, "S44 initializes world collision border");
    packet(game, 0x44, Writer().varInt(1).f64(40).f64(20).varLong(0));
    require(game.border.diameter == 20 && game.world.border().minX == 0 && game.world.border().maxX == 20, "zero-duration world-border lerp applies target immediately");
    packet(game, 7, Writer().i32(-1).u8(3).u8(1).string("default"));
    require(game.dimension == -1 && game.world.chunks().empty() && !game.spawned && game.creativeMode && game.player.allowFlying, "dimension respawn rebuilds player/world");
    require(game.objectives.count("board") == 1, "scoreboard survives dimension respawn");
    loadFloor(game); game.tick(Input{});
    require(game.world.getBlock(0,63,0) == blockState(1), "Nether chunk has no sky-light array");
    require(game.world.border().maxX == 29999984, "dimension replacement restores default border until next S44");
}

void inventoryAndNbtTests() {
    Game game; game.online = true; std::vector<Packet> sent;
    game.outgoingObserver = [&](const Packet& packet) { sent.push_back(packet); }; join(game); sent.clear();
    Writer nbt;
    nbt.u8(10).u16(0).u8(9).u16(4).bytes(std::vector<std::uint8_t>{'e','n','c','h'}).u8(10).i32(1)
        .u8(2).u16(2).bytes(std::vector<std::uint8_t>{'i','d'}).i16(8)
        .u8(2).u16(3).bytes(std::vector<std::uint8_t>{'l','v','l'}).i16(3).u8(0).u8(0);
    ItemStack boots; boots.id = 313; boots.count = 1; boots.nbt = nbt.data;
    Writer slot; slot.i8(0).i16(8); Game::writeItem(slot, boots); packet(game, 0x2f, slot);
    require(game.inventory[8].id == 313 && game.inventory[8].nbt == nbt.data && game.player.depthStriderLevel == 3, "item NBT preserved and depth strider applied");
    Writer slotBytes; Game::writeItem(slotBytes, boots); Reader reader(slotBytes.data);
    const auto roundtrip = Game::readItem(reader); reader.requireEnd(); require(roundtrip.nbt == boots.nbt && roundtrip.id == boots.id, "complete slot roundtrip");
    Writer malformed; malformed.i16(1).i8(1).i16(0).u8(10).u16(0).u8(9).u16(0).u8(0).i32(1000000000);
    Reader corrupt(malformed.data); rejects([&] { Game::readItem(corrupt); }, "NBT list count/depth bounded before allocation");
    Writer negativeArray; negativeArray.i16(1).i8(1).i16(0).u8(10).u16(0).u8(7).u16(0).i32(-1);
    Reader negative(negativeArray.data); rejects([&] { Game::readItem(negative); }, "negative NBT array rejected");
    packet(game, 0x2d, Writer().u8(4).string("minecraft:container").string("{\"text\":\"Chest\"}").u8(27));
    require(game.window.open && game.window.id == 4 && game.window.slots.size() == 63 && game.window.title == "Chest", "open container");
    Writer all; all.u8(4).i16(63);
    for (int i = 0; i < 63; ++i) {
        ItemStack item; if (i == 54) { item.id = 1; item.count = 64; }
        Game::writeItem(all, item);
    }
    packet(game, 0x30, all);
    require(game.inventory[36].id == 1 && game.inventory[36].count == 64, "container hotbar maps to player inventory");
    Writer cursor; cursor.i8(-1).i16(-1); Game::writeItem(cursor, boots); packet(game, 0x2f, cursor);
    require(game.cursor.id == 313, "cursor slot");
    packet(game, 0x32, Writer().u8(4).i16(-32768).boolean(false));
    require(sent.size() == 1 && sent[0].id == 0x0f, "rejected transaction acknowledgement");
    Reader confirmation(sent[0].data); require(confirmation.u8() == 4 && confirmation.i16() == -32768 && confirmation.boolean(), "confirmation accepted=true same action number");
    sent.clear(); packet(game, 0x32, Writer().u8(5).i16(1).boolean(false)); packet(game, 0x32, Writer().u8(4).i16(2).boolean(true));
    require(sent.empty(), "no confirmation for unrelated window or already accepted transaction");
    packet(game, 0x2e, Writer().u8(4)); require(!game.window.open, "server closes window");
}

void entityTests() {
    Game xpGame;packet(xpGame,0x11,Writer().varInt(777).i32(16).i32(2048).i32(-16).i16(2477));
    require(xpGame.entities.at(777).kind==EntityKind::ExperienceOrb&&xpGame.entities.at(777).xpValue==2477&&xpGame.entities.at(777).type==2477,"S11 retains explicit XP amount and legacy type value");
    Game game; join(game);
    Writer list; list.varInt(0).varInt(1); uuid(list).string("PlayerOne").varInt(1).string("textures").string("base64-textures").boolean(true).string("signature").varInt(1).varInt(37).boolean(false);
    packet(game, 0x38, list);
    require(game.playerList.at("123456781234123412341234567890ab").texturesProperty=="base64-textures"&&game.playerList.at("123456781234123412341234567890ab").texturesSignature=="signature","profile texture property and signature retained");
    Writer latency;latency.varInt(2).varInt(1);uuid(latency).varInt(42);packet(game,0x38,latency);
    require(game.playerList.at("123456781234123412341234567890ab").ping==42&&game.playerList.at("123456781234123412341234567890ab").texturesProperty=="base64-textures","latency update preserves texture property");
    Writer spawn; spawn.varInt(100); uuid(spawn).i32(-32).i32(2048).i32(48).i8(64).i8(-32).i16(1)
        .u8((4 << 5) | 2).string("VisibleName").u8((3 << 5) | 6).f32(18.0f).u8(127);
    packet(game, 0x0c, spawn);
    const auto& entity = game.entities.at(100);
    require(entity.kind == EntityKind::Player && entity.x == -1 && entity.y == 64 && entity.z == 1.5 && entity.yaw == 90 && entity.pitch == -45, "spawn player fixed-point/rotation");
    require(entity.name == "VisibleName" && entity.equipment[0].id == 1 && entity.metadata.at(6).number == 18, "spawn metadata/equipment");
    packet(game, 0x17, Writer().varInt(100).i8(16).i8(-8).i8(32).i8(96).i8(16).boolean(true));
    require(game.entities.at(100).serverX == -16 && game.entities.at(100).serverY == 2040 && game.entities.at(100).targetZ == 2.5, "relative entity fixed-point accumulation");
    game.tick(Input{}); game.tick(Input{}); game.tick(Input{});
    near(game.entities.at(100).x, -.5, "three-step interpolation X"); near(game.entities.at(100).y, 63.75, "three-step interpolation Y");
    near(game.entities.at(100).yaw, 135, "entity rotation interpolation");
    packet(game, 0x19, Writer().varInt(100).i8(-64)); require(game.entities.at(100).headYaw == -90, "independent head yaw");
    packet(game, 0x12, Writer().varInt(100).i16(8000).i16(-8000).i16(0)); require(game.entities.at(100).motionX == 1 && game.entities.at(100).motionY == -1, "remote velocity");
    packet(game, 0x1b, Writer().i32(42).i32(100).u8(0)); require(game.vehicleId == 100, "local vehicle attachment");
    packet(game, 0x1b, Writer().i32(42).i32(-1).u8(0)); require(game.vehicleId == -1, "local vehicle detachment");
    packet(game, 0x13, Writer().varInt(1).varInt(100)); require(game.entities.empty(), "destroy entities");
    Writer remove; remove.varInt(4).varInt(1); uuid(remove); packet(game, 0x38, remove); require(game.playerList.empty(), "remove player-list entry");
}

void uiAndInteractionTests() {
    require(Game::plainChat("{\"translate\":\"chat.type.text\",\"with\":[{\"text\":\"Alex\"},\"hello\"],\"extra\":[{\"text\":\"!\"}]}") == "<Alex> hello!", "translated chat with children");
    require(Game::plainChat("[{\"text\":\"\\u4e2d\\u6587\"},{\"text\":\"\\ud83d\\ude00\"}]") == "\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80", "JSON Unicode and surrogate decoding");
    require(Game::plainChat("{\"text\":\"\xc2\xa7" "aGreen\"}") == "Green", "legacy formatting stripped for plain text");
    require(Game::plainChat("{\"text\":\"\xc2\xa7" "z\"}") == "\xc2\xa7" "z", "non-format section-sign sequence preserved");
    require(Game::plainChat("malformed plain message") == "malformed plain message", "malformed JSON fallback");
    Game game; game.online = true; std::vector<Packet> sent;
    game.outgoingObserver = [&](const Packet& packet) { sent.push_back(packet); }; join(game); teleport(game); sent.clear();
    packet(game, 0x29, Writer().string("random.click").i32(-12).i32(512).i32(8).f32(.5f).u8(63));
    require(game.soundEvents.size() == 1 && game.soundEvents.front().name == "random.click" && game.soundEvents.front().x == -1.5 &&
        game.soundEvents.front().y == 64 && game.soundEvents.front().volume == .5f && game.soundEvents.front().pitch == 1.0f, "named sound coordinates/volume/pitch decoded");
    packet(game, 0x3b, Writer().string("score").u8(0).string("Score").string("integer"));
    packet(game, 0x3c, Writer().string("Alex").varInt(0).string("score").varInt(9));
    packet(game, 0x3d, Writer().u8(1).string("score"));
    require(game.scores.at("score").at("Alex") == 9 && game.displayedObjectives[1] == "score", "scoreboard state");
    packet(game, 0x3e, Writer().string("red").u8(0).string("Red").string("[").string("]").u8(3).string("always").i8(12).varInt(1).string("Alex"));
    require(game.teams.at("red").members == std::vector<std::string>{"Alex"}, "team create");
    packet(game, 0x3e, Writer().string("red").u8(4).varInt(1).string("Alex")); require(game.teams.at("red").members.empty(), "team remove member");
    packet(game, 0x3c, Writer().string("Alex").varInt(1).string("")); require(game.scores.at("score").empty(), "remove all player scores");
    require(game.sendChat("hello") && sent.back().id == 1, "chat wire message");
    require(!game.sendChat(std::string(101,'a')), "outgoing chat length limit");
    require(game.selectSlot(3) && sent.back().id == 9 && game.selectedSlot == 3, "hotbar selection");
    ItemStack held; held.id = 5; held.count = 32; game.inventory[39] = held;
    require(game.placeBlock(-2,64,3,1,.5f,1,.25f), "block placement request");
    Reader placement(sent.back().data);
    require(sent.back().id == 8 && placement.u64() == position(-2,64,3) && placement.u8() == 1, "placement packed position and face");
    const auto item = Game::readItem(placement);
    require(item.id == 5 && item.count == 32 && placement.u8() == 8 && placement.u8() == 16 && placement.u8() == 4, "placement item/cursor scale 16");
    placement.requireEnd();
    require(game.useHeldItem(), "use item packet"); Reader use(sent.back().data);
    require(use.u64() == std::numeric_limits<std::uint64_t>::max() && use.u8() == 255, "use-item sentinel position/face");
    require(game.digBlock(0,1,2,3,4) && sent.back().id == 7, "digging packet");
    require(game.swingArm() && sent.back().id == 10, "swing animation packet");
    require(game.attackEntity(50) && sent.back().id == 2, "attack entity packet");
    require(!game.attackEntity(game.entityId), "no self-attack");
    packet(game, 0x48, Writer().string("https://example.invalid/test.zip").string("hash"));
    require(sent.back().id == 0x19, "resource pack produces explicit response");
    Reader resourceStatus(sent.back().data); require(resourceStatus.string() == "hash" && resourceStatus.varInt() == 1, "uninstalled resource pack is declined");
    require(game.respawn() && sent.back().id == 0x16 && sent.back().data == std::vector<std::uint8_t>{0}, "explicit respawn request");
    for (int id = 0; id <= 0x49; ++id) {
        if (id == 0x0a) continue;
        Game isolated;
        // Empty bodies for nonempty wire shapes must be rejected. C0A is
        // serverbound; all clientbound protocol-47 packets carry a body.
        rejects([&] { isolated.consumePacket(Packet{id,{}}); }, "truncated packet body rejected: " + std::to_string(id));
    }
}
void dropAndTileTests(){
    Game game;join(game);teleport(game);game.online=true;std::vector<Packet> sent;int offlineCalls=0;game.outgoingObserver=[&](const Packet& packet){sent.push_back(packet);};game.offlineHeldItemDrop=[&](bool){++offlineCalls;return false;};game.inventory[36].id=4;game.inventory[36].count=7;
    require(game.dropHeldItem()&&game.dropHeldItem(true)&&sent.size()==2,"Q and control Q send original dig packets");
    for(std::size_t i=0;i<sent.size();++i){Reader body(sent[i].data);require(sent[i].id==0x07&&body.varInt()==(i?3:4)&&body.u64()==0&&body.u8()==0,"drop packet action, origin and down face");body.requireEnd();}
    require(offlineCalls==0&&game.inventory[36].count==7,"online Q does not call authority or predict inventory decrement");game.online=false;
    require(!game.dropHeldItem()&&offlineCalls==1&&game.inventory[36].count==7,"rejected offline Q keeps held item");game.offlineHeldItemDrop={};require(!game.dropHeldItem()&&game.inventory[36].count==7,"Q without offline authority cannot delete items");
    nbt::Tag root;root.type=10;root.compound["SkullType"]=nbt::integer(1,3);root.compound["Owner"]=nbt::text("Native");const auto bytes=nbt::encode(root);const auto at=position(0,64,0);game.world.setBlock(0,64,0,blockState(144));
    packet(game,0x35,Writer().u64(at).u8(4).bytes(bytes));const auto& tile=game.tileEntities.at(at);require(tile.x==0&&tile.y==64&&tile.z==0&&tile.action==4&&tile.nbt==bytes,"S35 retains complete original tile NBT and action");
    rejects([&]{packet(game,0x35,Writer().u64(at).u8(6).bytes(bytes).u8(1));},"S35 rejects trailing bytes before replacing tile");require(game.tileEntities.at(at).action==4,"malformed tile update preserves previous data");
    game.ticks=77;packet(game,0x24,Writer().u64(at).u8(1).u8(255).varInt(54));const auto& action=game.blockActions.back();require(action.x==0&&action.y==64&&action.blockId==54&&action.action==1&&action.parameter==255&&action.tick==77,"S24 retains unsigned action fields and simulation tick");
    for(int i=0;i<1030;++i){game.ticks=static_cast<std::uint64_t>(i);packet(game,0x24,Writer().u64(at).u8(1).u8(0).varInt(54));}require(game.blockActions.size()==1024&&game.blockActions.front().tick==6,"block action event queue is bounded FIFO");
    packet(game,0x23,Writer().u64(at).varInt(blockState(1)));require(game.tileEntities.empty(),"block type replacement discards obsolete tile data");
    packet(game,0x35,Writer().u64(at).u8(4).bytes(bytes));packet(game,0x21,Writer().i32(0).i32(0).boolean(true).u16(0).byteArray({}));require(game.tileEntities.empty()&&game.blockActions.empty(),"chunk unload discards retained tiles and queued block actions");
}
void roamingMemoryTests(){
    Game game;join(game);std::size_t peakChunks=0;const auto chunk=floorChunk();nbt::Tag tag;tag.type=10;tag.compound["SkullType"]=nbt::integer(1,3);const auto bytes=nbt::encode(tag);
    for(int step=0;step<2000;++step){const int x=step-1000,z=step%17-8;const auto at=position(x*16+2,64,z*16+3);
        packet(game,0x21,Writer().i32(x).i32(z).boolean(true).u16(8).byteArray(chunk));peakChunks=std::max(peakChunks,game.world.chunks().size());
        packet(game,0x35,Writer().u64(at).u8(4).bytes(bytes));
        packet(game,0x21,Writer().i32(x).i32(z).boolean(true).u16(0).byteArray({}));
        packet(game,0x23,Writer().u64(at).varInt(blockState(1)));
        packet(game,0x22,Writer().i32(x).i32(z).varInt(1).u16(0x2340).varInt(blockState(5)));
        packet(game,0x35,Writer().u64(at).u8(4).bytes(bytes));
        packet(game,0x33,Writer().u64(at).string("\"one\"").string("\"two\"").string("\"three\"").string("\"four\""));
        require(game.world.chunks().empty()&&game.tileEntities.empty()&&game.signs.empty(),"roaming stale updates cannot resurrect unloaded chunks or tile caches");
    }
    std::cout<<"Roaming load/unload: 2000 cycles, peak chunks="<<peakChunks<<", retained chunks/tiles/signs="<<game.world.chunks().size()<<'/'<<game.tileEntities.size()<<'/'<<game.signs.size()<<'\n';
}
} // namespace

int main() {
    try {
        movementTests(); chunkAndStateTests(); inventoryAndNbtTests(); entityTests(); uiAndInteractionTests();dropAndTileTests();roamingMemoryTests();
        std::cout << "game packet/movement tests passed (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
