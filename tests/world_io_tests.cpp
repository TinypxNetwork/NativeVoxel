#include "mc/world_io.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <zlib.h>

namespace {
namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;
void check(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
void append(Bytes& to, const Bytes& from) { to.insert(to.end(), from.begin(), from.end()); }
Bytes u16(unsigned value) { return {static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value)}; }
Bytes u32(std::uint32_t value) { return {static_cast<std::uint8_t>(value >> 24), static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value)}; }
Bytes u64(std::uint64_t value) { Bytes b; for (int i = 7; i >= 0; --i) b.push_back(static_cast<std::uint8_t>(value >> (i * 8))); return b; }
Bytes f32(float value) { std::uint32_t bits; std::memcpy(&bits, &value, 4); return u32(bits); }
Bytes f64(double value) { std::uint64_t bits; std::memcpy(&bits, &value, 8); return u64(bits); }
Bytes text(const std::string& value) { Bytes b = u16(static_cast<unsigned>(value.size())); b.insert(b.end(), value.begin(), value.end()); return b; }
Bytes tag(unsigned type, const std::string& name, const Bytes& payload) { Bytes b{static_cast<std::uint8_t>(type)}; append(b, text(name)); append(b, payload); return b; }
Bytes compound(std::initializer_list<Bytes> children) { Bytes b; for (const auto& child : children) append(b, child); b.push_back(0); return b; }
Bytes sequence(unsigned type, const std::vector<Bytes>& payloads) { Bytes b{static_cast<std::uint8_t>(type)}; append(b, u32(static_cast<std::uint32_t>(payloads.size()))); for (const auto& child : payloads) append(b, child); return b; }
Bytes array(const Bytes& payload) { Bytes b = u32(static_cast<std::uint32_t>(payload.size())); append(b, payload); return b; }
Bytes zip(const Bytes& bytes, bool gzip) {
    z_stream s{};
    check(deflateInit2(&s, 6, Z_DEFLATED, gzip ? 31 : 15, 8, Z_DEFAULT_STRATEGY) == Z_OK, "fixture deflate init");
    Bytes out(compressBound(static_cast<uLong>(bytes.size())) + 64);
    s.next_in = const_cast<Bytef*>(bytes.data()); s.avail_in = static_cast<uInt>(bytes.size());
    s.next_out = out.data(); s.avail_out = static_cast<uInt>(out.size());
    const auto result = deflate(&s, Z_FINISH); const auto size = s.total_out; deflateEnd(&s);
    check(result == Z_STREAM_END, "fixture compression"); out.resize(size); return out;
}
Bytes unzip(const Bytes& bytes) {
    z_stream s{}; check(inflateInit2(&s, 47) == Z_OK, "fixture inflate init");
    Bytes out(4u * 1024 * 1024);
    s.next_in = const_cast<Bytef*>(bytes.data()); s.avail_in = static_cast<uInt>(bytes.size());
    s.next_out = out.data(); s.avail_out = static_cast<uInt>(out.size());
    const auto result = inflate(&s, Z_FINISH); const auto size = s.total_out; inflateEnd(&s);
    check(result == Z_STREAM_END, "fixture decompression"); out.resize(size); return out;
}
void write(const fs::path& path, const Bytes& b) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char*>(b.data()), b.size()); check(out.good(), "fixture write");
}
Bytes read(const fs::path& path) { std::ifstream in(path, std::ios::binary); return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()); }
unsigned get32(const std::uint8_t* p) { return (unsigned(p[0]) << 24) | (unsigned(p[1]) << 16) | (unsigned(p[2]) << 8) | p[3]; }

Bytes allTypes() {
    Bytes ints = u32(3); append(ints, u32(0)); append(ints, u32(0x80000000)); append(ints, u32(0x7fffffff));
    Bytes longs = u32(2); append(longs, u64(0x8000000000000000ULL)); append(longs, u64(0x7fffffffffffffffULL));
    return tag(10, "NativeFixture", compound({
        tag(1, "AByte", {0xf9}), tag(2, "BShort", u16(0xfb2e)), tag(3, "CInt", u32(0xc521974f)),
        tag(4, "DLong", u64(0x8877665544332211ULL)), tag(5, "EFloat", f32(.3f)), tag(6, "FDouble", f64(3.141592653589793)),
        tag(7, "GBytes", array({0, 255, 128, 42})), tag(8, "HString", text("plain")),
        tag(9, "IList", sequence(8, {text("one"), text("two")})), tag(10, "JCompound", compound({tag(3, "child", u32(42))})),
        tag(11, "KInts", ints), tag(12, "LLongs", longs)}));
}
const std::string encodedName = std::string("\xe4\xb8\x96\xe7\x95\x8c") + "\xc0\x80" + "\xed\xa0\xbd\xed\xb8\x80";
const std::string decodedName = std::string("\xe4\xb8\x96\xe7\x95\x8c") + std::string(1, '\0') + "\xf0\x9f\x98\x80";

Bytes level(int dimension = 0, bool player = true, bool malformed = false) {
    Bytes data;
    append(data, tag(8, "LevelName", text(encodedName)));
    append(data, tag(4, "RandomSeed", u64(0x8877665544332211ULL)));
    append(data, tag(3, "SpawnX", u32(0xffffffff))); append(data, tag(3, "SpawnY", u32(64))); append(data, tag(3, "SpawnZ", u32(0xffffffef)));
    append(data, tag(3, "version", u32(19133))); append(data, allTypes());
    if (player) append(data, tag(10, "Player", compound({
        tag(3, "Dimension", u32(static_cast<std::uint32_t>(dimension))),
        tag(9, "Pos", sequence(6, {f64(-.25), f64(70.5), f64(-16.25)})),
        tag(9, "Rotation", sequence(5, {f32(123.5f), f32(-31.25f)})),
        tag(9, "Inventory", sequence(10, {compound({tag(1, "Count", {1}), tag(2, "id", u16(264)), tag(1, "Slot", {0})})}))})));
    if (malformed) append(data, tag(7, "badArray", u32(0xffffffff)));
    data.push_back(0);
    return tag(10, "", compound({tag(10, "Data", data)}));
}
Bytes section(unsigned y, bool upper = false, bool truncatedBlocks = false) {
    Bytes blocks(truncatedBlocks ? 4095 : 4096), metadata(2048), add(2048);
    const unsigned index = upper ? ((15 << 8) | (10 << 4) | 5) : ((6 << 8) | (15 << 4) | 15);
    const unsigned id = upper ? 4095 : 197, meta = upper ? 13 : 9;
    blocks[index] = static_cast<std::uint8_t>(id);
    metadata[index >> 1] = static_cast<std::uint8_t>(meta << ((index & 1) * 4));
    add[index >> 1] = static_cast<std::uint8_t>((id >> 8) << ((index & 1) * 4));
    return compound({tag(1, "Y", {static_cast<std::uint8_t>(y)}), tag(7, "Blocks", array(blocks)), tag(7, "Data", array(metadata)),
                     tag(7, "Add", array(add)), tag(7, "BlockLight", array(Bytes(2048, 0xa5))), tag(7, "SkyLight", array(Bytes(2048, 0xc3)))});
}
Bytes chunk(bool empty = false, bool duplicate = false, bool truncatedBlocks = false) {
    Bytes biomes(256); for (unsigned i = 0; i < 256; ++i) biomes[i] = static_cast<std::uint8_t>(i);
    std::vector<Bytes> sections;
    if (!empty) { sections.push_back(section(4, false, truncatedBlocks)); sections.push_back(section(duplicate ? 4 : 15, true)); }
    return tag(10, "", compound({tag(10, "Level", compound({tag(3, "xPos", u32(0xffffffff)), tag(3, "zPos", u32(0xfffffffe)),
        tag(9, "Sections", sequence(10, sections)), tag(7, "Biomes", array(biomes)), allTypes(),
        tag(9, "Entities", sequence(10, {compound({tag(8, "id", text("Pig"))})})),
        tag(9, "TileEntities", sequence(10, {compound({tag(8, "id", text("Chest")), tag(3, "x", u32(0xffffffff)), tag(3, "y", u32(64)), tag(3, "z", u32(0xffffffef))})}))}))}));
}
Bytes region(const Bytes& nbt, unsigned compression = 2) {
    const Bytes encoded = compression == 3 ? nbt : zip(nbt, compression == 1);
    const auto sectors = static_cast<unsigned>((encoded.size() + 5 + 4095) / 4096);
    Bytes result((2 + sectors) * 4096);
    constexpr unsigned slot = 31 + 30 * 32; // chunk (-1,-2), region (-1,-1)
    const auto location = u32((2 << 8) | sectors); std::copy(location.begin(), location.end(), result.begin() + slot * 4);
    const auto length = u32(static_cast<std::uint32_t>(encoded.size() + 1)); std::copy(length.begin(), length.end(), result.begin() + 8192);
    result[8196] = static_cast<std::uint8_t>(compression); std::copy(encoded.begin(), encoded.end(), result.begin() + 8197);
    return result;
}
fs::path fixture(const fs::path& base, const std::string& name, unsigned compression = 2, int dimension = 0,
                 bool empty = false, bool malformed = false, bool duplicate = false, bool truncated = false, bool player = true) {
    const auto path = base / name;
    write(path / "level.dat", zip(level(dimension, player, malformed), true));
    const auto dimensionPath = dimension == 0 ? path : path / (dimension == -1 ? "DIM-1" : "DIM1");
    write(dimensionPath / "region/r.-1.-1.mca", region(chunk(empty, duplicate, truncated), compression));
    return path;
}
bool contains(const Bytes& b, const Bytes& needle) { return std::search(b.begin(), b.end(), needle.begin(), needle.end()) != b.end(); }
Bytes savedColumn(const fs::path& save,int cx,int cz){
    const int rx=cx/32-(cx%32<0?1:0),rz=cz/32-(cz%32<0?1:0);const auto file=read(save/"region"/("r."+std::to_string(rx)+"."+std::to_string(rz)+".mca"));
    const unsigned slot=unsigned(cx&31)+unsigned(cz&31)*32,start=(get32(file.data()+slot*4)>>8)*4096,size=get32(file.data()+start);
    return unzip(Bytes(file.begin()+start+5,file.begin()+start+4+size));
}
mc::nbt::Tag tileData(const std::string& name,int x,int y,int z){
    mc::nbt::Tag value;value.type=10;value.compound["id"]=mc::nbt::text(name);value.compound["x"]=mc::nbt::integer(3,x);value.compound["y"]=mc::nbt::integer(3,y);value.compound["z"]=mc::nbt::integer(3,z);return value;
}
mc::nbt::Tag chestData(int x,int y,int z){
    auto chest=tileData("Chest",x,y,z);chest.compound["Lock"]=mc::nbt::text("");chest.compound["CustomName"]=mc::nbt::text(encodedName);
    mc::nbt::Tag book;book.type=10;book.compound["id"]=mc::nbt::text("minecraft:enchanted_book");book.compound["Count"]=mc::nbt::integer(1,1);book.compound["Damage"]=mc::nbt::integer(2,0);book.compound["Slot"]=mc::nbt::integer(1,0);
    mc::nbt::Tag enchantment;enchantment.type=10;enchantment.compound["id"]=mc::nbt::integer(2,16);enchantment.compound["lvl"]=mc::nbt::integer(2,5);
    mc::nbt::Tag stored;stored.type=9;stored.subtype=10;stored.list.push_back(enchantment);mc::nbt::Tag itemTag;itemTag.type=10;itemTag.compound["StoredEnchantments"]=stored;book.compound["tag"]=itemTag;
    mc::nbt::Tag items;items.type=9;items.subtype=10;items.list.push_back(book);chest.compound["Items"]=items;return chest;
}
mc::nbt::Tag spawnerData(int x,int y,int z){
    auto spawner=tileData("MobSpawner",x,y,z);spawner.compound["EntityId"]=mc::nbt::text("Zombie");
    for(const auto& pair:std::vector<std::pair<std::string,int>>{{"Delay",20},{"MinSpawnDelay",200},{"MaxSpawnDelay",800},{"SpawnCount",4},{"MaxNearbyEntities",6},{"RequiredPlayerRange",16},{"SpawnRange",4}})spawner.compound[pair.first]=mc::nbt::integer(2,pair.second);
    return spawner;
}
mc::worldio::SaveInfo nativeInfo(){mc::worldio::SaveInfo info;info.seed=123456789;info.generation=mc::worldio::GenerationInfo{};info.generation->type=mc::worldgen::WorldType::Amplified;info.generation->mapFeatures=false;return info;}

void run(const fs::path& base) {
    std::string error;
    for (unsigned compression : {1u, 2u, 3u}) {
        const auto source = fixture(base, "compression" + std::to_string(compression), compression);
        const auto originalLevel = read(source / "level.dat"), originalRegion = read(source / "region/r.-1.-1.mca");
        mc::World world; mc::worldio::SaveInfo info;
        check(mc::worldio::loadWorld(source.u8string(), world, info, 1, &error), error);
        check(info.loadedChunks == 1 && info.hasPlayer && info.dimension == 0 && info.levelName == decodedName, "level metadata and modified UTF-8");
        check(info.x == -.25 && info.y == 70.5 && info.z == -16.25 && info.yaw == 123.5f && info.pitch == -31.25f, "saved player pose");
        check(world.getBlock(-1, 70, -17) == mc::blockState(197, 9), "legacy block metadata / negative region indexing");
        check(world.getBlock(-11, 255, -22) == mc::blockState(4095, 13), "Anvil Add high block bits");
        check(world.getBlockLight(-1, 70, -17) == 10 && world.getSkyLight(-1, 70, -17) == 12 && world.getBiome(-1, -17) == 255, "light nibble/biome preservation");
        check(originalLevel == read(source / "level.dat") && originalRegion == read(source / "region/r.-1.-1.mca"), "import must never modify input files");

        mc::Player player; player.teleport(-.75, 71.25, -16.5); player.yaw = 12.3456f; player.pitch = -6.5f;
        world.setBlock(-1, 70, -17, mc::blockState(44, 8));
        const auto exported = base / ("exported" + std::to_string(compression));
        check(mc::worldio::saveNewWorld(exported.u8string(), world, player, info, &error), error);
        mc::World restored; mc::worldio::SaveInfo restoredInfo;
        check(mc::worldio::loadWorld(exported.u8string(), restored, restoredInfo, 1, &error), error);
        check(restoredInfo.levelName == decodedName && restoredInfo.seed == info.seed && restoredInfo.x == player.x && restoredInfo.yaw == player.yaw, "level metadata export roundtrip");
        check(restored.getBlock(-1, 70, -17) == mc::blockState(44, 8) && restored.getBlock(-11, 255, -22) == mc::blockState(4095, 13), "edited chunk export roundtrip");
        // setBlock now performs the original incremental relight. The save must
        // preserve that current result, rather than the imported pre-edit bytes.
        for(int y=0;y<256;++y)for(int z=-32;z<-16;++z)for(int x=-16;x<0;++x)
            check(restored.getSkyLight(x,y,z)==world.getSkyLight(x,y,z)&&
                  restored.getBlockLight(x,y,z)==world.getBlockLight(x,y,z),
                  "export preserves all current light values after a block edit");
        check(contains(unzip(read(exported / "level.dat")), allTypes()), "all NBT types preserved in level.dat");
        const auto packedRegion = read(exported / "region/r.-1.-1.mca");
        constexpr unsigned slot = 31 + 30 * 32;
        const auto start = (get32(packedRegion.data() + slot * 4) >> 8) * 4096;
        const auto size = get32(packedRegion.data() + start);
        const Bytes packed(packedRegion.begin() + start + 5, packedRegion.begin() + start + 4 + size);
        check(contains(unzip(packed), allTypes()), "unknown chunk NBT tags preserved");
        const auto beforeRefusedSave = read(exported / "level.dat");
        check(!mc::worldio::saveNewWorld(exported.u8string(), world, player, info, &error) && beforeRefusedSave == read(exported / "level.dat"), "existing save overwrite refused");
        check(!mc::worldio::saveNewWorld((source / "new-child").u8string(), world, player, info, &error) && !fs::exists(source / "new-child"), "cannot export inside source save");
        check(originalLevel == read(source / "level.dat") && originalRegion == read(source / "region/r.-1.-1.mca"), "export must not modify source files");
    }
    for (int dimension : {-1, 1}) {
        const auto source = fixture(base, "dimension" + std::to_string(dimension), 2, dimension);
        mc::World world; mc::worldio::SaveInfo info;
        check(mc::worldio::loadWorld(source.u8string(), world, info, 0, &error), error);
        check(info.dimension == dimension && !world.chunk(-1, -2)->hasSkyLight && world.getSkyLight(-1, 70, -17) == 0, "Nether/End no-skylight dimensions");
    }
    {
        const auto source = fixture(base, "empty-column-spawn", 2, 0, true, false, false, false, false);
        mc::World world; mc::worldio::SaveInfo info;
        check(mc::worldio::loadWorld(source.u8string(), world, info, 0, &error), error);
        check(!info.hasPlayer && info.x == -.5 && info.y == 64 && info.z == -16.5 && world.hasChunk(-1, -2), "spawn fallback and loaded empty Anvil column");
    }
    std::vector<fs::path> invalid;
    invalid.push_back(fixture(base, "negative-array", 2, 0, false, true));
    invalid.push_back(fixture(base, "duplicate-sections", 2, 0, false, false, true));
    invalid.push_back(fixture(base, "bad-block-array", 2, 0, false, false, false, true));
    auto badAllocation = fixture(base, "bad-allocation");
    auto corrupt = read(badAllocation / "region/r.-1.-1.mca"); corrupt[(31 + 30 * 32) * 4 + 2] = 1; // points into header
    write(badAllocation / "region/r.-1.-1.mca", corrupt); invalid.push_back(badAllocation);
    auto badCompression = fixture(base, "bad-compression");
    corrupt = read(badCompression / "region/r.-1.-1.mca"); corrupt[8196] = 99;
    write(badCompression / "region/r.-1.-1.mca", corrupt); invalid.push_back(badCompression);
    auto truncated = fixture(base, "truncated-level");
    corrupt = read(truncated / "level.dat"); corrupt.resize(corrupt.size() - 4); write(truncated / "level.dat", corrupt); invalid.push_back(truncated);
    for (const auto& path : invalid) {
        mc::World unchanged; unchanged.setBlock(0, 10, 0, mc::blockState(57));
        mc::worldio::SaveInfo info; info.levelName = "unchanged";
        const auto revision = unchanged.revision();
        check(!mc::worldio::loadWorld(path.u8string(), unchanged, info, 0, &error) && !error.empty(), "malformed save must be rejected");
        check(info.levelName == "unchanged" && unchanged.revision() == revision && unchanged.getBlock(0, 10, 0) == mc::blockState(57), "failed import is transactional");
    }
    mc::World preview; preview.generatePreview(1);
    mc::Player player; player.teleport(.5, 64, .5);
    const auto newPreview = base / "new-preview";
    check(mc::worldio::saveNewWorld(newPreview.u8string(), preview, player, {}, &error), error);
    mc::World loadedPreview; mc::worldio::SaveInfo previewInfo;
    check(mc::worldio::loadWorld(newPreview.u8string(), loadedPreview, previewInfo, 1, &error), error);
    check(loadedPreview.chunks().size() == 9 && loadedPreview.getBlock(0, 63, 0) == preview.getBlock(0, 63, 0), "new preview creates a valid Anvil save");
    fs::create_directory(base / "empty-existing");
    check(!mc::worldio::saveNewWorld((base / "empty-existing").u8string(), preview, player, {}, &error), "empty existing directory also refused");
    {
        auto info=nativeInfo();const auto tile=chestData(0,64,0);auto removed=chestData(2,64,0);removed.compound["CustomName"]=mc::nbt::text("removed-native-tile");info.blockEntityUpdates={{0,64,0,tile},{1,64,0,spawnerData(1,64,0)},{2,64,0,removed},{2,64,0,{}}};
        preview.setBlock(0,64,0,mc::blockState(54,2));preview.setBlock(1,64,0,mc::blockState(52));preview.setBlock(2,64,0,0);
        const auto destination=base/"native-tiles";check(mc::worldio::saveNewWorld(destination.u8string(),preview,player,info,&error),error);
        const auto levelBytes=unzip(read(destination/"level.dat")),column=savedColumn(destination,0,0);
        check(contains(levelBytes,tag(8,"generatorName",text("amplified")))&&contains(levelBytes,tag(3,"GameType",u32(0))),"explicit native generator/game mode metadata");
        check(contains(column,tag(1,"TerrainPopulated",{0})),"incomplete stage is not marked populated");
        check(contains(column,tag(8,"CustomName",text(encodedName)))&&contains(column,tag(8,"EntityId",text("Zombie"))),"generated tile data and modified UTF-8 preserved");
        check(contains(column,tag(9,"StoredEnchantments",sequence(10,{compound({tag(2,"id",u16(16)),tag(2,"lvl",u16(5))})}))),"book enchantment typed NBT survives save bridge");
        check(!contains(column,tag(8,"CustomName",text("removed-native-tile"))),"removed tile is not exported");
        mc::World restored;mc::worldio::SaveInfo restoredInfo;check(mc::worldio::loadWorld(destination.u8string(),restored,restoredInfo,1,&error),error);
        restoredInfo.blockEntityUpdates={{0,64,0,{}},{1,64,0,chestData(1,64,0)}};restored.setBlock(0,64,0,0);restored.setBlock(1,64,0,mc::blockState(54,2));
        const auto changed=base/"native-tiles-updated";check(mc::worldio::saveNewWorld(changed.u8string(),restored,player,restoredInfo,&error),error);const auto changedColumn=savedColumn(changed,0,0);
        check(!contains(changedColumn,tag(8,"EntityId",text("Zombie")))&&contains(changedColumn,tag(8,"CustomName",text(encodedName))),"tile updates replace imported tile data");
        check(contains(changedColumn,tag(1,"TerrainPopulated",{0})),"import/export preserves existing population flag");
        for(int kind=0;kind<4;++kind){auto invalid=info;const auto failed=base/("invalid-tile-"+std::to_string(kind));
            if(kind==0)invalid.blockEntityUpdates[0].data.compound["x"]=mc::nbt::integer(3,99);
            else if(kind==1)invalid.blockEntityUpdates[0].data.compound["InvalidByte"]=mc::nbt::integer(1,300);
            else if(kind==2){mc::nbt::Tag badList;badList.type=9;badList.subtype=1;badList.list.push_back(mc::nbt::integer(2,1));invalid.blockEntityUpdates[0].data.compound["BadList"]=badList;}
            else invalid.generation->gameType=4;
            check(!mc::worldio::saveNewWorld(failed.u8string(),preview,player,invalid,&error)&&!fs::exists(failed),"invalid native tile/metadata must fail before creating a save");
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    fs::path base;
    try {
        if (argc == 3 && std::string(argv[1]) == "--export-preview") {
            mc::World world; world.generatePreview(1);
            mc::Player player; player.teleport(.5, 64, .5);
            std::string error;
            check(mc::worldio::saveNewWorld(argv[2], world, player, {}, &error), error);
            std::cout << "Exported native preview to a new directory\n";
            return 0;
        }
        if(argc==3&&std::string(argv[1])=="--export-tile-fixture"){
            mc::World world;world.generatePreview(1);world.setBlock(0,64,0,mc::blockState(54,2));world.setBlock(1,64,0,mc::blockState(52));world.setBlock(2,64,0,0);
            mc::Player player;player.teleport(.5,64,.5);auto info=nativeInfo();info.blockEntityUpdates={{0,64,0,chestData(0,64,0)},{1,64,0,spawnerData(1,64,0)},{2,64,0,chestData(2,64,0)},{2,64,0,{}}};std::string error;
            check(mc::worldio::saveNewWorld(argv[2],world,player,info,&error),error);std::cout<<"Exported native tile/generator-metadata fixture\n";return 0;
        }
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto parent = fs::weakly_canonical(fs::temp_directory_path());
        base = parent / ("native-worldio-test-" + std::to_string(stamp));
        check(fs::create_directory(base), "exclusive test workspace");
        run(base);
        check(fs::weakly_canonical(base).parent_path() == parent && base.filename().u8string().find("native-worldio-test-") == 0, "cleanup path guard");
        fs::remove_all(base);
        std::cout << "world IO tests passed (NBT, gzip/zlib/raw Anvil, metadata preservation, safe new-save export)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "world IO test failed: " << error.what() << "\nFixtures retained at " << base.u8string() << '\n';
        return 1;
    }
}
