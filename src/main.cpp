#include "mc/assets.hpp"
#include "mc/game.hpp"
#include "mc/platform.hpp"
#include "mc/renderer.hpp"
#include "mc/controller.hpp"
#include "mc/audio.hpp"
#include "mc/world_io.hpp"
#include "mc/registry.hpp"
#include "mc/i18n.hpp"
#include "mc/gui_inventory.hpp"
#include "mc/entity_dimensions.hpp"
#include "mc/picking.hpp"
#include "mc/pickup.hpp"
#include "mc/settings.hpp"
#include "mc/gui_options.hpp"
#include "mc/gui_text_input.hpp"
#include "mc/skins.hpp"
#include "mc/first_person.hpp"
#include "mc/tile_entities.hpp"
#include "mc/local_session.hpp"
#include "mc/render_fidelity.hpp"
#include "mc/fov.hpp"
#include "mc/entity_effects.hpp"
#include "mc/local_fire_visuals.hpp"
#include "mc/gui_hud.hpp"
#include "mc/resource_packs.hpp"
#include "mc/server_list.hpp"
#include <future>
#include <openssl/evp.h>
#include <cctype>
#include <shellapi.h>
#include <GL/gl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <ctime>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
namespace {
struct Options {
    std::string assets, server, username = "NativeCpp", screenshot, report, capture, replay, worldPath, saveWorld,
                                language = "zh_CN", initialScreen, seed = "189", worldType = "default",
                                authMode = "offline", uuid;
    int port = 25565, width = 1280, height = 720, distance = 12, frames = 0;
    double duration = 15;
    bool headless = false, status = false, demo = false, hidden = false, vsync = false, shadows = false,
         benchmark = false, networkSkins = false, vanilla = false;
    int dimension = 0, gameMode = 1;
    std::set<std::string> provided;
    std::vector<std::string> resourcePacks;
};
std::string profileUuid(std::string value) {
    value.erase(std::remove(value.begin(),value.end(),'-'),value.end());
    if(value.size()!=32||value.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)
        throw std::invalid_argument("UUID must contain 32 hexadecimal digits (hyphens are optional)");
    for(auto& c:value)c=char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
std::string offlineUuid(const std::string& name) {
    const std::string value="OfflinePlayer:"+name;
    std::array<unsigned char,EVP_MAX_MD_SIZE> bytes{};unsigned size=0;
    if(EVP_Digest(value.data(),value.size(),bytes.data(),&size,EVP_md5(),nullptr)!=1||size!=16)
        throw std::runtime_error("Cannot calculate offline profile UUID");
    bytes[6]=(bytes[6]&15)|0x30;bytes[8]=(bytes[8]&63)|0x80;
    std::ostringstream out;out<<std::hex<<std::setfill('0');for(unsigned i=0;i<16;++i)out<<std::setw(2)<<unsigned(bytes[i]);
    return out.str();
}
std::optional<mc::net::Auth> launchAuth(const Options& options) {
    if(options.authMode=="offline")return std::nullopt;
    const char* token=std::getenv("MC_ACCESS_TOKEN");
    if(!token||!*token)throw std::invalid_argument("Online login requires a legitimate Minecraft session in MC_ACCESS_TOKEN");
    return mc::net::Auth{token,options.uuid};
}
std::string quoteJson(const std::string &s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c == '\t')
            out += "\\t";
        else if (c >= 32)
            out += char(c);
    }
    return out + '"';
}
void splitAddress(std::string &host, int &port) {
    if (host.empty())
        throw std::invalid_argument("Server address is empty");
    if (host.front() == '[') {
        auto end = host.find(']');
        if (end == std::string::npos)
            throw std::invalid_argument("Invalid IPv6 address");
        if (end + 1 < host.size()) {
            if (host[end + 1] != ':')
                throw std::invalid_argument("Invalid address suffix");
            port = std::stoi(host.substr(end + 2));
        }
        host = host.substr(1, end - 1);
    } else if (host.find(':') == host.rfind(':') && host.find(':') != std::string::npos) {
        auto at = host.find(':');
        port = std::stoi(host.substr(at + 1));
        host.resize(at);
    }
    if (port < 1 || port > 65535)
        throw std::invalid_argument("Port must be between 1 and 65535");
}
Options parseOptions(const std::vector<std::string> &args) {
    Options o;
    const auto executable = fs::u8path(mc::executableDirectory());
    auto assets = executable / "assets";
    const auto parentAssets = executable.parent_path() / "assets";
    if (!fs::exists(assets / "assets.bin") && fs::exists(parentAssets / "assets.bin"))
        assets = parentAssets;
    o.assets = assets.lexically_normal().u8string();
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto &a = args[i];
        o.provided.insert(a);
        auto value = [&]() {
            if (++i >= args.size())
                throw std::invalid_argument("Missing value after " + a);
            return args[i];
        };
        if (a == "--assets")
            o.assets = value();
        else if (a == "--server" || a == "--connect")
            o.server = value();
        else if (a == "--username" || a == "--id")
            o.username = value();
        else if(a=="--auth"||a=="--auth-mode")
            o.authMode=value();
        else if(a=="--uuid")
            o.uuid=value();
        else if(a=="--resource-pack"||a=="--resourcepack")
            o.resourcePacks.push_back(value());
        else if (a == "--port")
            o.port = std::stoi(value());
        else if (a == "--status") {
            o.status = true;
            o.server = value();
        } else if (a == "--headless")
            o.headless = true;
        else if (a == "--demo")
            o.demo = true;
        else if (a == "--vanilla")
            o.vanilla = true;
        else if (a == "--seed")
            o.seed = value();
        else if (a == "--dimension")
            o.dimension = std::stoi(value());
        else if (a == "--game-mode")
            o.gameMode = std::stoi(value());
        else if (a == "--world-type")
            o.worldType = value();
        else if (a == "--hidden")
            o.hidden = true;
        else if (a == "--network-skins")
            o.networkSkins = true;
        else if (a == "--duration")
            o.duration = std::stod(value());
        else if (a == "--frames")
            o.frames = std::stoi(value());
        else if (a == "--screenshot")
            o.screenshot = value();
        else if (a == "--report")
            o.report = value();
        else if (a == "--capture")
            o.capture = value();
        else if (a == "--replay")
            o.replay = value();
        else if (a == "--world")
            o.worldPath = value();
        else if (a == "--save-world")
            o.saveWorld = value();
        else if (a == "--language")
            o.language = value();
        else if (a == "--screen")
            o.initialScreen = value();
        else if (a == "--width")
            o.width = std::stoi(value());
        else if (a == "--height")
            o.height = std::stoi(value());
        else if (a == "--view-distance")
            o.distance = std::stoi(value());
        else if (a == "--no-vsync")
            o.vsync = false;
        else if (a == "--no-shadows")
            o.shadows = false;
        else if (a == "--benchmark") {
            o.benchmark = true;
            o.demo = true;
            o.vsync = false;
        } else if (a == "--help" || a == "-h") {
            std::cout << "NativeVoxel: native C++ Minecraft 1.8.9 port in development\n"
                      << "  --demo                         Open the offline renderer/physics test world\n"
                      << "  --vanilla --seed value         Create native vanilla base terrain\n"
                      << "  --game-mode 0|1|2|3            Survival/Creative/Adventure/Spectator\n"
                      << "  --dimension -1|0|1             Nether/Overworld/End base terrain\n"
                      << "  --world-type default|largebiomes|amplified\n"
                      << "  --world directory             Load an Anvil save and native state if present\n"
                      << "  --save-world new-directory     Export a new local snapshot\n"
                      << "  --connect host[:port]          Connect to a protocol-47 server\n"
                      << "  --username name                Minecraft profile name (default NativeCpp)\n"
                      << "  --id name --auth offline|online --uuid profile-uuid\n"
                      << "                                  UUID is optional for offline profiles\n"
                      << "  --status host[:port]           Query server MOTD, protocol and latency\n"
                      << "  --headless --duration seconds  Run a bounded network integration session\n"
                      << "  --assets directory             Imported native resources\n"
                      << "  --resource-pack folder-or.zip  Add a pack (repeatable, last wins)\n"
                      << "  --benchmark --frames N         Render benchmark with GPU completion per frame\n"
                      << "  --hidden --screenshot file.ppm Save a verification frame without a visible window\n"
                      << "  --report file.json             Save run metrics\n"
                      << "  --no-vsync --no-shadows --view-distance N\n"
                      << "Online mode requires MC_ACCESS_TOKEN and --uuid (or MC_UUID) from a legitimate session.\n"
                      << "Current support/limitations are documented in README.md and docs/.\n";
            std::exit(0);
        } else
            throw std::invalid_argument("Unknown option: " + a);
    }
    if(o.authMode!="offline"&&o.authMode!="online")throw std::invalid_argument("--auth must be offline or online");
    if(o.username.empty()||o.username.size()>16||o.username.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)
        throw std::invalid_argument("Player ID must contain 1-16 letters, digits or underscores");
    if(o.uuid.empty()&&o.authMode=="online") {if(const char* uuid=std::getenv("MC_UUID"))o.uuid=uuid;}
    if(o.uuid.empty()) {
        if(o.authMode=="online")throw std::invalid_argument("Online mode requires --uuid or MC_UUID");
        o.uuid=offlineUuid(o.username);
    } else o.uuid=profileUuid(o.uuid);
    if (!o.server.empty())
        splitAddress(o.server, o.port);
    if (o.width < 320 || o.width > 7680 || o.height < 240 || o.height > 4320)
        throw std::invalid_argument("Unsupported window dimensions");
    if (o.duration < .1 || o.duration > 3600 || o.frames < 0)
        throw std::invalid_argument("Invalid duration or frame count");
    if ((o.dimension != -1 && o.dimension != 0 && o.dimension != 1) || o.gameMode < 0 || o.gameMode > 3)
        throw std::invalid_argument("Invalid dimension or game mode");
    for (auto &c : o.worldType)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    if (o.worldType != "default" && o.worldType != "largebiomes" && o.worldType != "amplified")
        throw std::invalid_argument("Unsupported world type");
    o.distance = std::clamp(o.distance, 2, 32);
    if (o.benchmark && !o.frames)
        o.frames = 600;
    return o;
}
void writeReport(const std::string &path, const std::string &json) {
    if (path.empty())
        return;
    auto target = fs::u8path(path);
    if (target.has_parent_path())
        fs::create_directories(target.parent_path());
    std::ofstream f(target);
    if (!f)
        throw std::runtime_error("Cannot write report: " + path);
    f << json << '\n';
}
class PacketCapture {
  public:
    explicit PacketCapture(const std::string &path) {
        if (path.empty())
            return;
        auto p = fs::u8path(path);
        if (p.has_parent_path())
            fs::create_directories(p.parent_path());
        stream_.open(p, std::ios::binary);
        if (!stream_)
            throw std::runtime_error("Cannot write play capture: " + path);
        stream_.write("NVPC\1\0\0\0", 8);
    }
    void packet(const mc::net::Packet &p) {
        if (!stream_.is_open())
            return;
        std::uint32_t id = p.id, size = std::uint32_t(p.data.size());
        stream_.write(reinterpret_cast<const char *>(&id), 4);
        stream_.write(reinterpret_cast<const char *>(&size), 4);
        stream_.write(reinterpret_cast<const char *>(p.data.data()), size);
        stream_.flush();
    }

  private:
    std::ofstream stream_;
};
void loadReplay(const std::string &path, mc::Game &game) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    char magic[8]{};
    f.read(magic, 8);
    if (!f || std::string(magic, 4) != "NVPC" || magic[4] != 1)
        throw std::runtime_error("Invalid play-packet capture");
    game.disconnect();
    game = mc::Game();
    while (f.peek() != std::char_traits<char>::eof()) {
        std::uint32_t id = 0, size = 0;
        f.read(reinterpret_cast<char *>(&id), 4);
        f.read(reinterpret_cast<char *>(&size), 4);
        if (!f || size > mc::net::MaxPacketSize || id > 0x49)
            throw std::runtime_error("Invalid captured play packet");
        mc::net::Packet packet;
        packet.id = id;
        packet.data.resize(size);
        f.read(reinterpret_cast<char *>(packet.data.data()), size);
        if (!f)
            throw std::runtime_error("Truncated play-packet capture");
        game.consumePacket(packet);
    }
    game.online = false;
    game.joined = game.spawned = true;
    game.error.clear();
    game.status = "Local replay of captured server packets";
}
std::string gameReport(const mc::Game &game, double seconds) {
    std::ostringstream out;
    out << "{\"protocol\":47,\"joined\":" << (game.joined ? "true" : "false")
        << ",\"spawned\":" << (game.spawned ? "true" : "false") << ",\"chunks\":" << game.world.chunks().size()
        << ",\"entities\":" << game.entities.size() << ",\"packets_received\":" << game.packetsReceived
        << ",\"packets_sent\":" << game.packetsSent << ",\"position_corrections\":" << game.positionCorrections
        << ",\"seconds\":" << seconds << ",\"position\":[" << game.player.x << ',' << game.player.y << ','
        << game.player.z << ']' << ",\"status\":" << quoteJson(game.status) << ",\"error\":" << quoteJson(game.error)
        << ",\"unhandled_packets\":{";
    bool first = true;
    for (const auto &pair : game.unhandledPackets) {
        if (!first)
            out << ',';
        first = false;
        out << quoteJson(std::to_string(pair.first)) << ':' << pair.second;
    }
    out << "}}";
    return out.str();
}
int headless(const Options &o) {
    if (o.server.empty())
        throw std::invalid_argument("--headless requires --connect host");
    mc::Game game;
    PacketCapture capture(o.capture);
    game.incomingObserver = [&](const mc::net::Packet &packet) { capture.packet(packet); };
    if (!game.connect(o.server, std::uint16_t(o.port), o.username, launchAuth(o))) {
        std::cerr << game.error << '\n';
        writeReport(o.report, gameReport(game, 0));
        return 2;
    }
    auto started = Clock::now(), nextTick = started;
    std::string previous;
    double seconds = 0;
    while ((seconds = std::chrono::duration<double>(Clock::now() - started).count()) < o.duration) {
        game.poll();
        if (game.status != previous) {
            previous = game.status;
            std::cout << "[network] " << previous << '\n';
        }
        if (!game.error.empty() || game.client.state() == mc::net::State::Failed ||
            game.client.state() == mc::net::State::Disconnected)
            break;
        auto now = Clock::now();
        if (now >= nextTick) {
            game.tick({});
            nextTick += std::chrono::milliseconds(50);
            if (nextTick + std::chrono::milliseconds(500) < now)
                nextTick = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::string report = gameReport(game, seconds);
    std::cout << report << '\n';
    writeReport(o.report, report);
    bool success = game.joined && game.spawned && !game.world.chunks().empty() && game.error.empty();
    game.disconnect();
    return success ? 0 : 2;
}
using Target = mc::BlockPick;
using EntityPick = mc::EntityPick;
Target raycast(const mc::World &world, const mc::Camera &camera, double reach) {
    return mc::pickBlock(world, {camera.x, camera.y, camera.z, camera.yaw, camera.pitch}, reach);
}
EntityPick pickEntity(const mc::Game &game, const mc::Camera &camera, const Target &block) {
    return mc::pickEntity(game, {camera.x, camera.y, camera.z, camera.yaw, camera.pitch}, block);
}
void appendInput(std::string &text, const std::u32string &input, std::size_t limit) {
    std::size_t units = 0;
    for (unsigned char c : text)
        if ((c & 0xc0) != 0x80)
            units += c >= 0xf0 ? 2 : 1;
    for (char32_t c : input) {
        if (c < 32 || c == 127 || c == 0xa7)
            continue;
        auto count = c > 0xffff ? 2u : 1u;
        if (units + count > limit)
            break;
        units += count;
        if (c < 128)
            text += char(c);
        else if (c < 0x800) {
            text += char(0xc0 | (c >> 6));
            text += char(0x80 | (c & 63));
        } else if (c < 0x10000) {
            text += char(0xe0 | (c >> 12));
            text += char(0x80 | ((c >> 6) & 63));
            text += char(0x80 | (c & 63));
        } else {
            text += char(0xf0 | (c >> 18));
            text += char(0x80 | ((c >> 12) & 63));
            text += char(0x80 | ((c >> 6) & 63));
            text += char(0x80 | (c & 63));
        }
    }
}
void backspace(std::string &value) {
    if (value.empty())
        return;
    std::size_t at = value.size() - 1;
    while (at > 0 && (std::uint8_t(value[at]) & 0xc0) == 0x80)
        --at;
    value.resize(at);
}
std::string saveFolderName(std::string value) {
    for (char &c : value)
        if (static_cast<unsigned char>(c) < 32 || std::string("<>:\"/\\|?*").find(c) != std::string::npos)
            c = '_';
    while (!value.empty() && (value.back() == '.' || value.back() == ' '))
        value.pop_back();
    if (value.empty() || value == "." || value == "..")
        value = "World";
    std::string base = value.substr(0, value.find('.'));
    std::transform(base.begin(), base.end(), base.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" ||
        (base.size() == 4 && (base.substr(0, 3) == "COM" || base.substr(0, 3) == "LPT") && base[3] >= '1' &&
         base[3] <= '9'))
        value = "World_" + value;
    return value;
}
std::int64_t worldSeed(const std::string &value) {
    if (value.empty())
        return std::chrono::system_clock::now().time_since_epoch().count();
    try {
        std::size_t n = 0;
        auto seed = std::stoll(value, &n);
        if (n == value.size())
            return seed;
    } catch (const std::exception &) {
    }
    std::uint32_t hash = 0;
    for (wchar_t c : mc::wide(value))
        hash = hash * 31 + std::uint16_t(c);
    return std::int32_t(hash);
}
enum class Screen {
    Menu,
    Worlds,
    NewWorld,
    ServerList,
    Server,
    ServerEdit,
    ServerDelete,
    Options,
    Language,
    Controls,
    Sounds,
    Skin,
    ChatSettings,
    Snooper,
    Stream,
    Play,
    Pause,
    Settings,
    Inventory,
    Chat,
    Connecting,
    ConfirmLink,
    ResourcePacks,
    Disconnected
};
bool optionsPage(Screen screen) {
    return screen==Screen::Options||screen==Screen::Settings||screen==Screen::Controls||screen==Screen::Sounds||
           screen==Screen::Skin||screen==Screen::ChatSettings||screen==Screen::Snooper||screen==Screen::Stream;
}
mc::OptionsPage optionsPageKind(Screen screen) {
    switch(screen){
    case Screen::Settings:return mc::OptionsPage::Video;case Screen::Controls:return mc::OptionsPage::Controls;
    case Screen::Sounds:return mc::OptionsPage::Sounds;case Screen::Skin:return mc::OptionsPage::Skin;
    case Screen::ChatSettings:return mc::OptionsPage::Chat;case Screen::Snooper:return mc::OptionsPage::Snooper;
    case Screen::Stream:return mc::OptionsPage::Stream;default:return mc::OptionsPage::Main;
    }
}
bool worldScreen(Screen screen,Screen returnScreen) {
    return screen==Screen::Play||screen==Screen::Chat||screen==Screen::Inventory||screen==Screen::Pause||screen==Screen::ConfirmLink||
           (returnScreen==Screen::Pause&&(optionsPage(screen)||screen==Screen::Language||screen==Screen::ResourcePacks));
}

int graphical(Options o) {
    DWORD consoleProcesses[2]{};
    if (!o.hidden && GetConsoleProcessList(consoleProcesses, 2) == 1)
        ShowWindow(GetConsoleWindow(), SW_HIDE);
    std::string error;
    mc::Settings preferences;
    mc::OptionsScreen optionsScreen;
    const auto preferencesPath =
        (fs::u8path(mc::executableDirectory()) / ".." / "options.txt").lexically_normal().u8string();
    if (!o.hidden)
        preferences.load(preferencesPath, &error);
    if (o.provided.count("--view-distance"))
        preferences.renderDistance = o.distance;
    else
        o.distance = preferences.renderDistance;
    if (o.provided.count("--language"))
        preferences.language = o.language;
    else
        o.language = preferences.language;
    if (o.provided.count("--no-vsync") || o.benchmark)
        preferences.vsync = false;
    else
        o.vsync = preferences.vsync;
    if (o.provided.count("--no-shadows"))
        preferences.shadows = false;
    else
        o.shadows = preferences.shadows;
    const std::string baseAssets=o.assets;
    const fs::path resourcePacksRoot=(fs::u8path(mc::executableDirectory())/".."/"resourcepacks").lexically_normal();
    fs::create_directories(resourcePacksRoot);
    std::vector<std::string> activePacks=o.resourcePacks;
    if(activePacks.empty()&&!o.hidden){std::ifstream selection(resourcePacksRoot/"selected.txt",std::ios::binary);std::string line;while(std::getline(selection,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(!line.empty())activePacks.push_back(line);}}
    for(auto& path:activePacks)path=fs::absolute(fs::u8path(path)).lexically_normal().u8string();
    if(!activePacks.empty()){
        mc::PreparedResourcePacks result;
        if(mc::prepareResourcePacks(baseAssets,activePacks,(resourcePacksRoot/".cache").u8string(),result,error))o.assets=result.assetDirectory;
        else{std::cerr<<"[resource packs] "<<error<<'\n';activePacks.clear();}
    }
    auto assets=std::make_shared<mc::Assets>();
    if (!assets->load(o.assets, error))
        throw std::runtime_error(error + "\nRun python tools/setup_assets.py to download and prepare the Minecraft resources.");
    mc::Game game;
    game.startPreview(std::min(o.distance, 6));
    mc::LocalSession local;
    mc::SkinCache skins;
    if (!skins.initialize(o.assets, error, !o.hidden || o.networkSkins))
        throw std::runtime_error(error);
    mc::Controller controller;
    mc::InventoryScreen inventoryScreen;
    mc::Audio audio;
    mc::I18n tr;
    tr.load(o.assets, o.language);
    mc::hud::Hud hud;
    if(!hud.load(o.assets,o.language,error))throw std::runtime_error(error);
    std::string hudLanguage=o.language;
    if (!inventoryScreen.load(o.assets, error))
        throw std::runtime_error(error);
    inventoryScreen.setBindings(&preferences);
    std::string audioError;
    if (!audio.initialize(o.assets, audioError, o.hidden || o.benchmark || o.frames > 0))
        std::cerr << "[audio] " << audioError << '\n';
    auto &saveInfo = local.saveInfo;
    PacketCapture capture(o.capture);
    game.incomingObserver = [&](const mc::net::Packet &packet) { capture.packet(packet); };
    mc::Window window;
    if (!window.create(o.width, o.height, L"NativeVoxel | Minecraft 1.8.9 | C++", o.hidden, error))
        throw std::runtime_error(error);
    mc::Renderer renderer;
    if (!renderer.initialize(*assets, error, o.assets))
        throw std::runtime_error(error);
    {mc::ResourceImage icon;std::string why;if(mc::readResourceImage(baseAssets,"pack.png",icon,why))renderer.registerImage("default_pack_icon",icon.width,icon.height,icon.pixels);}
    hud.registerTextures(renderer);
    mc::serverui::ServerList::registerTextures(renderer,o.assets);
    renderer.options.distance = o.distance;
    renderer.options.shadows = false;
    window.setVsync(preferences.vsync);
    if(!o.hidden)window.setFullscreen(preferences.fullscreen);
    bool appliedVsync = preferences.vsync;
    std::cout << "[renderer] " << renderer.stats().gpu << '\n';
    Screen screen = o.demo ? Screen::Play : Screen::Menu, returnScreen = Screen::Menu;
    bool debug = false, advancedTooltips = false, quit = false, retryAttempted = false;
    int activeField = 0, perspective = 0;
    bool offlineSession = o.demo && !o.benchmark;
    bool attackLatched = false, useLatched = false;
    int pendingSlot = -1;
    double footDistance = 0;
    float walkDistance=0,previousWalkDistance=0,cameraBobYaw=0,previousCameraBobYaw=0,cameraBobPitch=0,previousCameraBobPitch=0;
    int nextFootStep = 1;
    std::unordered_map<std::uint64_t, mc::ChestLidState> chestLids;
    mc::ChestAnimationTracker chestAnimations;
    mc::EntityAnimationTracker entityAnimations;
    mc::PickupAnimationTracker pickupAnimations;
    mc::visual::FovTracker fovTracker;
    mc::visual::LocalFireVisuals localFireVisuals;
    mc::ParticleSystem particles;
    std::unordered_map<std::string, std::string> requestedSkins;
    double nextSkinRetry = 0;
    mc::ItemStack renderedItem;
    float equipped = 0, previousEquipped = 0, swing = 0, previousSwing = 0;
    int renderedSlot = -1, swingTicks = 0;
    bool swinging = false;
    std::uint64_t seenSwings = 0;
    std::string serverText = o.server.empty() ? "mc.tinypx.net" : o.server, username = o.username;
    mc::gui::TextInput chatField(100);
    const auto& chatInput=chatField.text();
    mc::gui::ChatCompletion chatCompletion;
    std::uint64_t seenTabCompletion=0;
    double chatCursorChanged=0;
    std::string pendingUrl,chatDraft;
    mc::serverui::ServerList serverBrowser;
    const auto serversPath=(fs::u8path(mc::executableDirectory())/".."/"servers.dat").lexically_normal().u8string();
    serverBrowser.read(serversPath,error);
    if(serverBrowser.servers.empty()){mc::serverui::Server entry;entry.name="Minecraft Server";entry.address=serverText;serverBrowser.servers.push_back(std::move(entry));}
    serverBrowser.selected=0;
    mc::serverui::Server editedServer;
    int editingServer=-1;
    struct PingJob{std::string address;std::future<mc::net::StatusResult> future;};
    std::vector<PingJob> pingJobs;
    std::future<std::unique_ptr<mc::Game>> connectionJob;
    bool connectionCancelled=false;
    int chatScroll=0,chatHistory=-1;
    std::vector<mc::ResourcePackInfo> availablePacks;
    std::vector<std::string> selectedPacks=activePacks;
    int availablePackScroll=0,selectedPackScroll=0;
    struct PackLoadResult{bool ok=false;mc::PreparedResourcePacks prepared;std::string error;std::vector<std::string> paths;};
    std::future<PackLoadResult> packJob;
    std::string packNotice;
    auto refreshPacks=[&](){
        availablePacks.clear();
        for(const auto& entry:fs::directory_iterator(resourcePacksRoot)){
            if(entry.path().filename()==".cache")continue;
            mc::ResourcePackInfo info;std::string why;
            if(mc::inspectResourcePack(entry.path().u8string(),info,why))availablePacks.push_back(std::move(info));
        }
        for(const auto& path:activePacks)if(std::none_of(availablePacks.begin(),availablePacks.end(),[&](const auto& p){return p.path==path;})){
            mc::ResourcePackInfo info;std::string why;if(mc::inspectResourcePack(path,info,why))availablePacks.push_back(std::move(info));
        }
        std::sort(availablePacks.begin(),availablePacks.end(),[](const auto& a,const auto& b){return a.name<b.name;});
        selectedPacks=activePacks;
        availablePackScroll=selectedPackScroll=0;
        for(std::size_t i=0;i<availablePacks.size();++i){const auto& pack=availablePacks[i];if(pack.iconWidth)renderer.registerImage("pack_icon_"+std::to_string(i),pack.iconWidth,pack.iconHeight,pack.iconPixels);}
    };
    auto loadPacks=[&](){
        if(packJob.valid())return;
        const auto paths=selectedPacks;
        const auto cache=(resourcePacksRoot/".cache").u8string();
        packNotice="正在加载资源包…";
        packJob=std::async(std::launch::async,[baseAssets,paths,cache](){PackLoadResult result;result.paths=paths;result.ok=mc::prepareResourcePacks(baseAssets,paths,cache,result.prepared,result.error);return result;});
    };
    std::string newWorldName = "新的世界", newWorldSeed = o.seed;
    int selectedWorld = 0, worldField = 0, newWorldMode = o.gameMode;
    const fs::path savesRoot = (fs::u8path(mc::executableDirectory()) / ".." / "saves").lexically_normal();
    std::vector<fs::path> savePaths;
    auto refreshSaves = [&]() {
        savePaths.clear();
        if (fs::exists(savesRoot))
            for (const auto &entry : fs::directory_iterator(savesRoot))
                if (entry.is_directory() && fs::exists(entry.path() / "level.dat"))
                    savePaths.push_back(entry.path());
        std::sort(savePaths.begin(), savePaths.end());
        selectedWorld = std::clamp(selectedWorld, 0, std::max(0, int(savePaths.size()) - 1));
    };
    refreshSaves();
    std::string notice = "离线场景用于渲染和物理测试；完整玩法仍在移植。";
    std::array<std::uint16_t, 9> hotbar = {mc::blockState(1),  mc::blockState(2),  mc::blockState(3),
                                           mc::blockState(4),  mc::blockState(5),  mc::blockState(17),
                                           mc::blockState(20), mc::blockState(35), mc::blockState(98)};
    auto setPreviewItems = [&]() {
        local.attachPreview(game, 1, username);
        for (int i = 0; i < 9; ++i)
            game.inventory[36 + i] = {
                std::int16_t(mc::blockId(hotbar[i])), 64, std::int16_t(mc::blockMeta(hotbar[i])), {0}};
    };
    setPreviewItems();
    auto connect = [&](bool retry = false) {
        if(connectionJob.valid())return;
        fovTracker.reset();localFireVisuals.reset();
        std::string host = serverText;
        int port = o.port;
        try {
            local.detach(game);
            offlineSession = false;
            retryAttempted = retry;
            splitAddress(host, port);
            renderer.resetWorld();
            skins.clear();
            requestedSkins.clear();
            chestLids.clear();
            chestAnimations.reset();entityAnimations.reset();pickupAnimations.reset();particles.clear();
            walkDistance=previousWalkDistance=cameraBobYaw=previousCameraBobYaw=cameraBobPitch=previousCameraBobPitch=0;
            renderedItem = {};
            renderedSlot = -1;
            equipped = previousEquipped = 0;
            controller.reset();
            audio.stopAll();
            game.updateClientSettings(tr.language(),renderer.options.distance,preferences.chatVisibility,preferences.chatColors,preferences.skinParts);
            const auto auth=launchAuth(o);const auto profile=username;const auto locale=game.locale;const int distance=game.viewDistance;
            const int chatVisibility=preferences.chatVisibility;const bool chatColors=preferences.chatColors;const auto skinParts=preferences.skinParts;
            game.disconnect();game.error.clear();connectionCancelled=false;
            connectionJob=std::async(std::launch::async,[host,port,profile,auth,locale,distance,chatVisibility,chatColors,skinParts](){
                auto candidate=std::make_unique<mc::Game>();candidate->updateClientSettings(locale,distance,chatVisibility,chatColors,skinParts);
                candidate->connect(host,std::uint16_t(port),profile,auth);return candidate;
            });
            screen = Screen::Connecting;notice = "正在连接 " + host;window.setCapture(false);
        } catch (const std::exception &e) {
            notice = e.what();
            screen = Screen::Disconnected;
        }
    };
    auto loadSave = [&](const std::string &path) {
        fovTracker.reset();localFireVisuals.reset();
        chestAnimations.reset();entityAnimations.reset();pickupAnimations.reset();
        if (!local.load(game, path, o.distance, username, error)) {
            notice = error;
            screen = Screen::Disconnected;
            return;
        }
        renderer.resetWorld();
        controller.reset();
        chestLids.clear();
        offlineSession = true;
        screen = Screen::Play;
        window.setCapture(true);
    };
    auto generateWorld = [&](const std::string &name, const std::string &seedText, int mode) {
        fovTracker.reset();localFireVisuals.reset();
        pickupAnimations.reset();
        mc::worldgen::Settings settings;
        settings.mapFeatures = false;
        settings.dimension = mc::worldgen::Dimension(o.dimension);
        settings.worldType = o.worldType == "largebiomes" ? mc::worldgen::WorldType::LargeBiomes
                             : o.worldType == "amplified" ? mc::worldgen::WorldType::Amplified
                                                          : mc::worldgen::WorldType::Default;
        auto progress = [&](int done, int total) {
            if (!window.pump())
                return false;
            if (done % 8 && done != total)
                return true;
            renderer.menuBackground(window.width(), window.height(), 0);
            renderer.beginUI(window.width(), window.height());
            renderer.rect(0, 0, float(window.width()), float(window.height()), 0x00000099);
            renderer.text("Generating vanilla terrain: " + std::to_string(done) + " / " + std::to_string(total),
                          window.width() * .22f, window.height() * .45f, 2);
            renderer.endUI();
            window.swap();
            return true;
        };
        if (!local.create(game, worldSeed(seedText), settings, std::min(o.distance, 8), mode, username, error,
                          progress)) {
            notice = error;
            screen = Screen::Disconnected;
            return false;
        }
        saveInfo.levelName = name;
        renderer.resetWorld();
        controller.reset();
        chestLids.clear();
        offlineSession = true;
        screen = Screen::Play;
        window.setCapture(true);
        return true;
    };
    auto saveSnapshot = [&]() {
        if (!offlineSession || game.online)
            return true;
        auto name = saveFolderName(saveInfo.levelName) + " - " + std::to_string(std::time(nullptr));
        fs::path destination = savesRoot / fs::u8path(name);
        for (int i = 1; fs::exists(destination); ++i)
            destination = savesRoot / fs::u8path(name + "-" + std::to_string(i));
        if (!local.saveNew(game, destination.u8string(), error)) {
            notice = error;
            return false;
        }
        std::cout << "[save] " << destination.u8string() << '\n';
        return true;
    };
    if (!o.server.empty())
        connect();
    if (!o.replay.empty()) {
        local.detach(game);
        loadReplay(o.replay, game);
        renderer.resetWorld();
        screen = Screen::Play;
    }
    if (!o.worldPath.empty())
        loadSave(o.worldPath);
    if (o.vanilla)
        generateWorld("Native vanilla terrain", o.seed, o.gameMode);
    if (o.initialScreen == "inventory") {
        inventoryScreen.reset();
        screen = Screen::Inventory;
        window.setCapture(false);
    } else if (o.initialScreen == "server")
        screen = Screen::ServerList;
    else if (o.initialScreen == "settings") {
        screen = Screen::Settings;
        returnScreen = Screen::Menu;
    } else if (o.initialScreen == "controls") {
        screen = Screen::Controls;
        returnScreen = Screen::Menu;
    } else if (o.initialScreen == "sounds") {
        screen = Screen::Sounds;
        returnScreen = Screen::Menu;
    } else if (o.initialScreen == "options") {
        screen=Screen::Options;
    } else if (o.initialScreen == "resourcepacks") {
        refreshPacks();screen=Screen::ResourcePacks;
    } else if (o.initialScreen == "skin") {
        screen=Screen::Skin;
    } else if (o.initialScreen == "chat-settings") {
        screen=Screen::ChatSettings;
    } else if (o.initialScreen == "snooper") {
        screen=Screen::Snooper;
    } else if (o.initialScreen == "stream") {
        screen=Screen::Stream;
    } else if (o.initialScreen == "worlds")
        screen = Screen::Worlds;
    if (screen == Screen::Play)
        window.setCapture(true);
    auto started = Clock::now(), last = started, fpsClock = started;
    double accumulator = 0, totalFrameMs = 0, menuSeconds = 0;
    int frames = 0, fpsFrames = 0;
    float fps = 0, fov = 70;
    std::vector<double> benchmarkTimes;
    double measuredRendererMs = 0, measuredMeshesMs = 0, measuredDynamicMs = 0;
    Target target;
    while (!quit && window.pump()) {
        auto frameStart = Clock::now();
        double elapsed = std::chrono::duration<double>(frameStart - started).count();
        double dt = std::min(.5, std::chrono::duration<double>(frameStart - last).count());
        last = frameStart;
        if(screen==Screen::Menu)menuSeconds+=dt;
        if(connectionJob.valid()&&connectionJob.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){
            auto candidate=connectionJob.get();
            if(!connectionCancelled&&screen==Screen::Connecting){
                game=std::move(*candidate);
                game.incomingObserver=[&](const mc::net::Packet& packet){capture.packet(packet);};
                if(!game.error.empty()){notice=game.error;screen=Screen::Disconnected;}
            }
        }
        for(auto job=pingJobs.begin();job!=pingJobs.end();){
            if(job->future.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready){++job;continue;}
            auto result=job->future.get();
            for(std::size_t i=0;i<serverBrowser.servers.size();++i)if(serverBrowser.servers[i].address==job->address){
                if(result.ok){std::string why;
                    if(!serverBrowser.applyStatus(i,result.json,int(result.latencyMs),why)){serverBrowser.failPing(i,why);continue;}
                    mc::ResourceImage icon;if(serverBrowser.decodeIcon(i,icon,why)){
                        auto name="server_icon_"+std::to_string(i);renderer.registerImage(name,icon.width,icon.height,icon.pixels);serverBrowser.servers[i].iconTexture=name;
                    }
                }else serverBrowser.failPing(i,result.error);
            }
            job=pingJobs.erase(job);
        }
        if(screen==Screen::ServerList&&pingJobs.size()<5)for(auto index:serverBrowser.unqueried()){
            if(pingJobs.size()>=5)break;
            const auto address=serverBrowser.servers[index].address;serverBrowser.beginPing(index);
            pingJobs.push_back({address,std::async(std::launch::async,[address](){std::string host=address;int port=25565;
                try{splitAddress(host,port);return mc::net::queryStatus(host,std::uint16_t(port),5000);}catch(const std::exception& e){mc::net::StatusResult result;result.error=e.what();return result;}
            })});
        }
        if(packJob.valid()&&packJob.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){
            auto result=packJob.get();
            if(result.ok){
                auto nextAssets=std::make_shared<mc::Assets>();
                if(nextAssets->load(result.prepared.assetDirectory,error)){
                    auto oldAssets=assets;
                    mc::Renderer nextRenderer;nextRenderer.options=renderer.options;
                    if(nextRenderer.initialize(*nextAssets,error,result.prepared.assetDirectory)){
                        renderer.swap(nextRenderer);assets=std::move(nextAssets);o.assets=result.prepared.assetDirectory;
                        activePacks=result.paths;selectedPacks=activePacks;requestedSkins.clear();
                        skins.initialize(o.assets,error,!o.hidden||o.networkSkins);
                        inventoryScreen.load(o.assets,error);inventoryScreen.setBindings(&preferences);
                        tr.load(o.assets,preferences.language);hud.load(o.assets,preferences.language,error);hud.registerTextures(renderer);
                        {mc::ResourceImage icon;std::string why;if(mc::readResourceImage(baseAssets,"pack.png",icon,why))renderer.registerImage("default_pack_icon",icon.width,icon.height,icon.pixels);}
                        for(std::size_t i=0;i<availablePacks.size();++i){const auto& pack=availablePacks[i];if(pack.iconWidth)renderer.registerImage("pack_icon_"+std::to_string(i),pack.iconWidth,pack.iconHeight,pack.iconPixels);}
                        mc::serverui::ServerList::registerTextures(renderer,o.assets);
                        for(std::size_t i=0;i<serverBrowser.servers.size();++i){
                            mc::ResourceImage icon;std::string why;
                            if(serverBrowser.decodeIcon(i,icon,why)){
                                auto name="server_icon_"+std::to_string(i);
                                renderer.registerImage(name,icon.width,icon.height,icon.pixels);serverBrowser.servers[i].iconTexture=name;
                            }else serverBrowser.servers[i].iconTexture.clear();
                        }
                        audio.initialize(o.assets,error,o.hidden||o.benchmark||o.frames>0);
                        packNotice=result.prepared.warnings.empty()?"资源包已加载":result.prepared.warnings.front();
                        std::ofstream selected(resourcePacksRoot/"selected.txt",std::ios::binary|std::ios::trunc);for(const auto& path:activePacks)selected<<path<<'\n';
                        if(screen==Screen::ResourcePacks)screen=Screen::Options;
                    } else packNotice=error;
                } else packNotice=error;
            } else packNotice=result.error;
        }
        fov = preferences.fov;
        renderer.options.distance = preferences.renderDistance;
        renderer.options.shadows = false;
        renderer.options.gamma = preferences.gamma;
        renderer.options.dimension = game.dimension;
        renderer.options.rain = game.rainStrength;
        renderer.options.thunder = game.thunderStrength;
        renderer.options.fxaa = preferences.fxaa;
        renderer.options.playerSkinParts=preferences.skinParts;
        renderer.options.secretShader=preferences.secretShader;
        renderer.options.ambientOcclusion=preferences.ambientOcclusion!=0;
        renderer.options.blockAlternatives=preferences.blockAlternatives;
        renderer.options.mipmapLevels=preferences.mipmapLevels;
        game.updateClientSettings(tr.language(),preferences.renderDistance,preferences.chatVisibility,preferences.chatColors,preferences.skinParts);
        renderer.setUnicode(tr.unicode()||preferences.forceUnicodeFont);
        if(hudLanguage!=tr.language()){hud.load(o.assets,tr.language(),error);hud.registerTextures(renderer);hudLanguage=tr.language();}
        hud.setUnicode(tr.unicode()||preferences.forceUnicodeFont);
        if(!o.hidden)window.setFullscreen(preferences.fullscreen);
        if (appliedVsync != preferences.vsync) {
            window.setVsync(preferences.vsync);
            appliedVsync = preferences.vsync;
        }
        o.vsync = preferences.vsync;
        audio.setMasterVolume(preferences.soundLevels[0]);
        for (int i = 1; i < 9; ++i)
            audio.setCategoryVolume(mc::SoundCategory(i), preferences.soundLevels[i]);
        auto pressed = [&](const char *action) {
            int key = preferences.key(action);
            return key && window.pressed(key);
        };
        auto down = [&](const char *action) {
            int key = preferences.key(action);
            return key && window.down(key);
        };
        bool interactive = screen == Screen::Play;
        if (screen != Screen::Controls && pressed("fullscreen")) {
            window.toggleFullscreen();
            preferences.fullscreen=window.fullscreen();
        }
        if (window.pressed(VK_F3))
            debug = !debug;
        if(window.down(VK_F3)&&window.pressed('T')){selectedPacks=activePacks;loadPacks();}
        if(window.down(VK_F3)&&window.pressed('H'))advancedTooltips=!advancedTooltips;
        inventoryScreen.setAdvancedTooltips(advancedTooltips);
        if (screen == Screen::Play && pressed("togglePerspective"))
            perspective = (perspective + 1) % 3;
        bool textOpened = false;
        if (window.pressed(VK_ESCAPE) && screen != Screen::Inventory && !optionsPage(screen)) {
            if (screen == Screen::Play) {
                screen = Screen::Pause;
                window.setCapture(false);
            } else if (screen == Screen::Chat || screen == Screen::Inventory || screen == Screen::Pause) {
                if (screen == Screen::Inventory)
                    controller.closeWindow(game);
                screen = Screen::Play;
                window.setCapture(true);
            } else if(screen==Screen::Connecting){connectionCancelled=true;game.disconnect();screen=Screen::ServerList;}
            else if(screen==Screen::ConfirmLink)screen=Screen::Chat;
            else if(screen==Screen::Language)screen=Screen::Options;
            else if(screen==Screen::ResourcePacks&&!packJob.valid())screen=Screen::Options;
            else if (screen == Screen::Server || screen == Screen::Disconnected||screen==Screen::ServerEdit||screen==Screen::ServerDelete)
                screen = Screen::ServerList;
        } else if (interactive && pressed("inventory")) {
            inventoryScreen.reset();
            screen = Screen::Inventory;
            window.setCapture(false);
        } else if (interactive && preferences.chatVisibility!=2 && (pressed("chat") || pressed("command"))) {
            screen = Screen::Chat;
            chatField.setText(pressed("command") ? "/" : "");chatCompletion.cancel();chatCursorChanged=elapsed;
            chatHistory=-1;chatScroll=0;chatDraft.clear();
            window.setCapture(false);
            textOpened = true;
        }
        interactive = screen == Screen::Play;
        if (interactive && window.focused() && !window.captured() && window.pressed(VK_LBUTTON))
            window.setCapture(true);
        if (interactive && window.captured()) {
            float sensitivity = preferences.mouseSensitivity * .6f + .2f;
            sensitivity = sensitivity * sensitivity * sensitivity * 8;
            game.player.yaw = float(double(game.player.yaw) + double(float(window.deltaX) * sensitivity) * .15);
            game.player.pitch =
                std::clamp(float(double(game.player.pitch) +
                                 double(float(window.deltaY) * sensitivity) * (preferences.invertMouse ? -.15 : .15)),
                           -90.f, 90.f);
        }
        if (screen == Screen::Server) {
            auto &field = serverText;
            appendInput(field, window.textInput, 128);
            if (window.pressed(VK_BACK))
                backspace(field);
            if (window.pressed(VK_RETURN))
                connect();
        }
        if(screen==Screen::ServerEdit){
            if(window.pressed(VK_TAB))activeField=1-activeField;
            auto& field=activeField==0?editedServer.name:editedServer.address;
            appendInput(field,window.textInput,activeField==0?32:128);if(window.pressed(VK_BACK))backspace(field);
        }
        if (screen == Screen::NewWorld) {
            if (window.pressed(VK_TAB))
                worldField = 1 - worldField;
            auto &field = worldField ? newWorldSeed : newWorldName;
            appendInput(field, window.textInput, 64);
            if (window.pressed(VK_BACK))
                backspace(field);
        }
        if (screen == Screen::Chat && !textOpened) {
            if(!window.textInput.empty()){chatField.insert(window.textInput);chatCompletion.cancel();chatCursorChanged=elapsed;}
            const bool control=window.down(VK_CONTROL),shift=window.down(VK_SHIFT);
            const int pageRows=std::max(1,mc::hud::Hud::chatHeight(preferences.chatHeightFocused)/9-1);
            chatScroll=std::clamp(chatScroll+window.wheel*(shift?1:7)+(window.pressed(VK_PRIOR)?pageRows:0)-(window.pressed(VK_NEXT)?pageRows:0),0,100);
            const auto& history=hud.sentHistory();
            for(int key:window.repeatedKeyOrder){
                if(key==VK_TAB){
                    const auto request=chatCompletion.tab(chatField);
                    if(request){
                        if(game.online){std::optional<std::array<int,3>> block;if(target.hit)block=std::array<int,3>{{target.x,target.y,target.z}};game.sendTabComplete(*request,block);}
                        else {
                            const auto start=chatField.wordLeft(chatField.cursor(),false);std::string prefix=chatInput.substr(start,chatField.cursor()-start);
                            std::transform(prefix.begin(),prefix.end(),prefix.begin(),[](unsigned char c){return char(std::tolower(c));});
                            std::vector<std::string> candidates;
                            auto consider=[&](const std::string& name){auto lowered=name;std::transform(lowered.begin(),lowered.end(),lowered.begin(),[](unsigned char c){return char(std::tolower(c));});if(lowered.compare(0,prefix.size(),prefix)==0)candidates.push_back(name);};
                            consider(username);for(const auto& entry:game.playerList)consider(entry.second.name);
                            std::sort(candidates.begin(),candidates.end());candidates.erase(std::unique(candidates.begin(),candidates.end()),candidates.end());
                            chatCompletion.response(chatField,candidates);
                        }
                    }
                    chatCursorChanged=elapsed;continue;
                }
                bool edit=true;
                if(key==VK_BACK)chatField.erase(true,control);
                else if(key==VK_DELETE)chatField.erase(false,control);
                else if(key==VK_LEFT)chatField.move(-1,control,shift);
                else if(key==VK_RIGHT)chatField.move(1,control,shift);
                else if(key==VK_HOME)chatField.home(shift);
                else if(key==VK_END)chatField.end(shift);
                else if(key=='A'&&control)chatField.selectAll();
                else if((key=='C'||key=='X')&&control){
                    const auto selection=mc::wide(chatField.selection());
                    if(!selection.empty()&&OpenClipboard(window.handle())){
                        const auto bytes=(selection.size()+1)*sizeof(wchar_t);HGLOBAL storage=GlobalAlloc(GMEM_MOVEABLE,bytes);
                        if(storage){if(auto* destination=GlobalLock(storage)){std::copy(selection.begin(),selection.end(),static_cast<wchar_t*>(destination));static_cast<wchar_t*>(destination)[selection.size()]=0;GlobalUnlock(storage);EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,storage))GlobalFree(storage);}else GlobalFree(storage);}
                        CloseClipboard();
                    }
                    if(key=='X'&&chatField.selected())chatField.erase(true);
                } else if(key==VK_UP&&!history.empty()){
                    if(chatHistory<0){chatDraft=chatInput;chatHistory=int(history.size());}
                    chatHistory=std::max(0,chatHistory-1);chatField.setText(history[chatHistory]);
                } else if(key==VK_DOWN&&chatHistory>=0){
                    if(++chatHistory>=int(history.size())){chatHistory=-1;chatField.setText(chatDraft);}else chatField.setText(history[chatHistory]);
                } else edit=false;
                if(edit){chatCompletion.cancel();chatCursorChanged=elapsed;}
            }
            if (window.pressed(VK_RETURN)) {
                const auto first=chatInput.find_first_not_of(' '),last=chatInput.find_last_not_of(' ');
                const auto message=first==std::string::npos?std::string{}:chatInput.substr(first,last-first+1);
                if (!message.empty()) {
                    hud.rememberSent(message);
                    if (game.online)
                        game.sendChat(message);
                    else {
                        game.chat.push_back({message, "", 0, game.ticks});
                        if(game.chat.size()>100)game.chat.pop_front();
                        ++game.chatRevision;
                    }
                }
                chatField.clear();chatCompletion.cancel();
                screen = Screen::Play;
                window.setCapture(true);
            }
        }
        if (interactive) {
            for (int i = 0; i < 9; ++i)
                if (pressed(("hotbar." + std::to_string(i + 1)).c_str()))
                    pendingSlot = i;
            if (window.wheel)
                pendingSlot = ((pendingSlot >= 0 ? pendingSlot : game.selectedSlot) - window.wheel % 9 + 9) % 9;
            attackLatched = attackLatched || pressed("attack");
            useLatched = useLatched || pressed("use");
            if (pressed("drop"))
                game.dropHeldItem(window.down(VK_CONTROL));
        }
        game.poll();
        if(seenTabCompletion!=game.tabCompletionRevision){
            seenTabCompletion=game.tabCompletionRevision;
            if(screen==Screen::Chat&&chatCompletion.response(chatField,game.tabCompletions))chatCursorChanged=elapsed;
        }
        pickupAnimations.update(game);
        if(screen==Screen::Connecting&&game.spawned&&!game.world.chunks().empty()){screen=Screen::Play;window.setCapture(true);}
        particles.configure(preferences.particles,game.player.x,game.player.y,game.player.z);
        while(!game.animationEvents.empty()){
            const auto event=game.animationEvents.front();game.animationEvents.pop_front();
            if(event.animation==4||event.animation==5)particles.emitEntity(game,event.entityId,event.animation==4?9:10);
        }
        while(!game.deathEvents.empty()){particles.entityDeath(game.deathEvents.front());game.deathEvents.pop_front();}
        while(!game.particleEvents.empty()) {
            const auto& event=game.particleEvents.front();const double dx=event.x-game.player.x,dy=event.y-game.player.eyeY(),dz=event.z-game.player.z;
            if(dx*dx+dy*dy+dz*dz<(event.longDistance?65536:1024))particles.spawn(event);
            game.particleEvents.pop_front();
        }
        while(!game.worldEffectEvents.empty()) {
            const auto event=game.worldEffectEvents.front();game.worldEffectEvents.pop_front();
            particles.worldEffect(event,game.world);
            const auto sound=particles.worldEffectSound(event,game);
            if(!sound.name.empty())audio.play(sound.name,sound.x,sound.y,sound.z,sound.volume,sound.pitch);
        }
        for (const auto &entry : game.playerList) {
            const auto &info = entry.second;
            auto found = requestedSkins.find(entry.first);
            if (found == requestedSkins.end() || found->second != info.texturesProperty || elapsed >= nextSkinRetry) {
                skins.request(entry.first, info.texturesProperty, info.texturesSignature);
                requestedSkins[entry.first] = info.texturesProperty;
            }
        }
        for(auto it=requestedSkins.begin();it!=requestedSkins.end();) {
            if(!game.playerList.count(it->first))it=requestedSkins.erase(it);else ++it;
        }
        if (elapsed >= nextSkinRetry)
            nextSkinRetry = elapsed + 1;
        while (!game.blockActions.empty()) {
            auto event = game.blockActions.front();
            game.blockActions.pop_front();
            if (mc::blockId(game.world.getBlock(event.x, event.y, event.z)) != unsigned(event.blockId))
                continue;
            if (event.action != 1 || (event.blockId != 54 && event.blockId != 146 && event.blockId != 130))
                continue;
            auto key = (std::uint64_t(std::uint32_t(event.x) & 0x3ffffff) << 38) |
                       (std::uint64_t(std::uint32_t(event.y) & 4095) << 26) | (std::uint32_t(event.z) & 0x3ffffff);
            (void)key;
            chestAnimations.action(game.world,event.x,event.y,event.z,event.blockId,event.action,event.parameter,event.serial);
        }
        if (game.window.open && screen == Screen::Play) {
            inventoryScreen.reset();
            screen = Screen::Inventory;
            window.setCapture(false);
        }
        while (!game.soundEvents.empty()) {
            auto sound = std::move(game.soundEvents.front());
            game.soundEvents.pop_front();
            audio.play(sound.name, sound.x, sound.y, sound.z, sound.volume, sound.pitch);
        }
        if (!game.error.empty() && screen != Screen::Disconnected && screen != Screen::Menu &&
            screen != Screen::Server) {
            if (!retryAttempted && serverText.find("mc.tinypx.net") != std::string::npos &&
                game.error.find("session is not configured") != std::string::npos)
                connect(true);
            else {
                notice = mc::Game::plainChat(game.error);
                screen = Screen::Disconnected;
                window.setCapture(false);
            }
        }
        accumulator += dt;
        if (!game.online && screen != Screen::Play && screen != Screen::Inventory && screen != Screen::Chat)
            accumulator = 0;
        int catchUp = 0;
        while (accumulator >= .05 && catchUp++ < 10) {
            mc::Input input;
            if (interactive && window.focused()) {
                input.forward = float(down("forward")) - float(down("back"));
                input.strafe = float(down("left")) - float(down("right"));
                input.jump = down("jump");
                input.sneak = down("sneak");
                input.sprint = down("sprint");
            }
            mc::Camera aim{game.player.x, game.player.eyeY(), game.player.z, game.player.yaw, game.player.pitch, fov};
            auto hit = raycast(game.world, aim, game.gameMode == 1 ? 5. : 4.5);
            mc::ControllerInput actions;
            actions.target = {hit.hit, hit.x, hit.y, hit.z, hit.face, hit.hitX, hit.hitY, hit.hitZ};
            auto entityHit = pickEntity(game, aim, hit);
            if (entityHit.id >= 0) {
                actions.target.block = false;
                actions.target.entityId = entityHit.id;
                actions.target.entityHitX = float(entityHit.x);
                actions.target.entityHitY = float(entityHit.y);
                actions.target.entityHitZ = float(entityHit.z);
            }
            actions.focused = interactive && window.focused();
            actions.attackHeld = actions.focused && down("attack");
            actions.useHeld = actions.focused && down("use");
            actions.attackPressed = attackLatched;
            actions.usePressed = useLatched;
            actions.selectedSlot = pendingSlot;
            const auto beforeHit=hit.hit?game.world.getBlock(hit.x,hit.y,hit.z):0;
            controller.tick(game, actions);
            if(actions.attackHeld&&actions.target.block&&hit.hit){
                if(beforeHit&&game.world.getBlock(hit.x,hit.y,hit.z)==0)particles.blockDestroyed(beforeHit,hit.x,hit.y,hit.z);
                else particles.blockHit(game.world,hit.x,hit.y,hit.z,hit.face);
            }
            input.usingItem = controller.usingItem();
            attackLatched = useLatched = false;
            pendingSlot = -1;
            double oldX = game.player.x, oldZ = game.player.z;
            if (local.active())
                local.tick(game, input, preferences.renderDistance);
            else
                game.tick(input);
            if(local.active()) {
                int fireProtection=0;
                for(int slot=5;slot<=8;++slot)fireProtection=std::max(fireProtection,mc::Controller::enchantment(game.inventory[slot],1));
                localFireVisuals.tick(game,fireProtection);
            } else localFireVisuals.reset();
            pickupAnimations.update(game);
            previousWalkDistance=walkDistance;
            walkDistance+=float(std::hypot(game.player.x-oldX,game.player.z-oldZ)*.6);
            previousCameraBobYaw=cameraBobYaw;previousCameraBobPitch=cameraBobPitch;
            const float horizontal=game.player.onGround&&game.health>0?std::min(.1f,float(std::hypot(game.player.motionX,game.player.motionZ))):0;
            const float vertical=!game.player.onGround&&game.health>0?float(std::atan(-game.player.motionY*.20000000298023224)*15.):0;
            cameraBobYaw+=(horizontal-cameraBobYaw)*.4f;cameraBobPitch+=(vertical-cameraBobPitch)*.8f;
            for(const auto& entry:game.entities) {
                const auto& e=entry.second;mc::EntityAnimationTick animation;
                animation.id=e.id;animation.tick=game.ticks;animation.x=e.x;animation.y=e.y;animation.z=e.z;
                animation.yaw=e.yaw;animation.headYaw=e.headYaw;animation.pitch=e.pitch;
                animation.player=e.kind==mc::EntityKind::Player;animation.onGround=e.onGround;
                animation.armorStand=(e.kind==mc::EntityKind::Mob&&e.type==30)||(e.kind==mc::EntityKind::Object&&e.type==78);
                animation.swingProgress=e.swingProgress;animation.previousSwingProgress=e.previousSwingProgress;
                auto flags=e.metadata.find(0);animation.usingItem=flags!=e.metadata.end()&&(flags->second.integer&16);
                animation.useAction=mc::registry::item(e.equipment[0].id).useAction;
                animation.useDuration=mc::registry::item(e.equipment[0].id).useDuration;
                entityAnimations.tick(animation);
            }
            mc::EntityAnimationTick selfAnimation;selfAnimation.id=game.entityId;selfAnimation.tick=game.ticks;
            selfAnimation.x=game.player.x;selfAnimation.y=game.player.y;selfAnimation.z=game.player.z;
            selfAnimation.yaw=selfAnimation.headYaw=game.player.yaw;selfAnimation.pitch=game.player.pitch;
            selfAnimation.onGround=game.player.onGround;selfAnimation.usingItem=controller.usingItem();
            previousEquipped = equipped;
            const auto &held = game.inventory[36 + game.selectedSlot];
            const int bowUseTicks=controller.usingItem()&&held.id==261?
                mc::registry::item(261).useDuration-controller.useTicksRemaining():0;
            fovTracker.tick(mc::visual::playerFovMultiplier(game.player.movementSpeed,game.walkSpeed,
                game.player.sprinting,game.player.flying,controller.usingItem()&&held.id==261,bowUseTicks));
            bool same = renderedItem.id == held.id && renderedItem.damage == held.damage &&
                        renderedItem.nbt == held.nbt && renderedSlot == game.selectedSlot;
            equipped += std::clamp((same ? 1.f : 0.f) - equipped, -.4f, .4f);
            if (equipped < .1f) {
                renderedItem = held;
                renderedSlot = game.selectedSlot;
            }
            previousSwing = swing;
            int swingLength = 6;
            auto haste = game.effects.find(3), fatigue = game.effects.find(4);
            if (haste != game.effects.end())
                swingLength = 6 - (1 + haste->second.amplifier);
            else if (fatigue != game.effects.end())
                swingLength = 6 + (1 + fatigue->second.amplifier) * 2;
            swingLength = std::max(1, swingLength);
            if (seenSwings != game.armSwings) {
                if (!swinging || swingTicks >= swingLength / 2) {
                    swingTicks = -1;
                    swinging = true;
                }
                seenSwings = game.armSwings;
            }
            if (swinging && ++swingTicks >= swingLength) {
                swingTicks = 0;
                swinging = false;
            }
            swing = float(swingTicks) / swingLength;
            selfAnimation.swingProgress=swing;selfAnimation.previousSwingProgress=previousSwing;
            entityAnimations.tick(selfAnimation);
            entityAnimations.prune(game.ticks);
            chestAnimations.tick(game.world,game.ticks);
            particles.tick(game.world);
            particles.tickEmitters(game);
            if(game.spawned)particles.ambient(game.world,mc::floorBlock(game.player.x),mc::floorBlock(game.player.y),mc::floorBlock(game.player.z));
            if (game.player.onGround && !game.player.sneaking && !game.player.inWater) {
                footDistance += std::hypot(game.player.x - oldX, game.player.z - oldZ) * .6;
                if (footDistance > nextFootStep) {
                    nextFootStep = int(footDistance) + 1;
                    int bx = mc::floorBlock(game.player.x), by = mc::floorBlock(game.player.y - .2),
                        bz = mc::floorBlock(game.player.z);
                    auto id = mc::blockId(game.world.getBlock(bx, by, bz));
                    const auto &block = mc::registry::block(id);
                    if (block.valid && id != 0)
                        audio.play(block.stepSound, game.player.x, game.player.y, game.player.z,
                                   block.soundVolume * .15f, block.soundPitch);
                }
            }
            accumulator -= .05;
        }
        float alpha = float(std::clamp(accumulator / .05, 0., 1.));
        renderer.options.partialTicks=alpha;
        renderer.options.thirdPerson=perspective;
        renderer.setViewBobbing(mc::visual::hurtCamera(game.hurtTime,game.maxHurtTime,game.attackedAtYaw,game.deathTime,game.health<=0,alpha)*
            (preferences.viewBobbing&&game.spawned?mc::visual::viewBobbing(previousWalkDistance,walkDistance,previousCameraBobYaw,cameraBobYaw,previousCameraBobPitch,cameraBobPitch,alpha):mc::gfx::Mat4::identity()));
        renderer.setParticles(particles.frame(alpha));
        chestLids.clear();
        for(const auto& chest:chestAnimations.states()) {
            const auto key=(std::uint64_t(std::uint32_t(chest.x)&0x3ffffff)<<38)|(std::uint64_t(std::uint32_t(chest.y)&4095)<<26)|(std::uint32_t(chest.z)&0x3ffffff);
            chestLids[key]=chest.lid;
        }
        mc::Camera camera;
        camera.x = game.player.previousX + (game.player.x - game.player.previousX) * alpha;
        camera.y = game.player.previousY + (game.player.y - game.player.previousY) * alpha +
                   (game.player.eyeY() - game.player.y);
        camera.z = game.player.previousZ + (game.player.z - game.player.previousZ) * alpha;
        camera.yaw = game.player.yaw;
        camera.pitch = game.player.pitch;
        const bool eyeUnderwater=mc::visual::viewpointInWater(game.world,camera.x,camera.y,camera.z);
        camera.fov = fovTracker.angle(fov,alpha,true,eyeUnderwater,game.health<=0,game.deathTime);
        renderer.options.firstPersonFov=fovTracker.angle(fov,alpha,false,eyeUnderwater,game.health<=0,game.deathTime);
        if (screen == Screen::Menu || screen == Screen::Server || screen == Screen::Disconnected ||
            (screen == Screen::Settings && returnScreen == Screen::Menu) || (o.benchmark && o.replay.empty())) {
            camera = {56, 84, -63, float(38 + std::sin(elapsed * .04) * 10), 20, fov};
        }
        if (perspective && worldScreen(screen,returnScreen)) {
            auto direction = mc::gfx::viewDirection(camera.yaw, camera.pitch) * (perspective == 1 ? -1.f : 1.f);
            double distance = 4;
            for (int i = 0; i < 8; ++i) {
                double ox = ((i & 1) ? 1 : -1) * .1, oy = ((i & 2) ? 1 : -1) * .1, oz = ((i & 4) ? 1 : -1) * .1;
                auto hit =
                    game.world.raycast(camera.x + ox, camera.y + oy, camera.z + oz, camera.x + ox + direction.x * 4,
                                       camera.y + oy + direction.y * 4, camera.z + oz + direction.z * 4);
                if (hit.hit)
                    distance =
                        std::min(distance, std::max(0., std::sqrt((hit.hitX - camera.x) * (hit.hitX - camera.x) +
                                                                  (hit.hitY - camera.y) * (hit.hitY - camera.y) +
                                                                  (hit.hitZ - camera.z) * (hit.hitZ - camera.z)) -
                                                            .05));
            }
            camera.x += direction.x * distance;
            camera.y += direction.y * distance;
            camera.z += direction.z * distance;
            if (perspective == 2) {
                camera.yaw += 180;
                camera.pitch = -camera.pitch;
            }
        }
        std::vector<mc::RenderEntity> entities;
        entities.reserve(game.entities.size()+pickupAnimations.size());
        auto appendEntity = [&](const mc::Entity& e,const mc::PickupAnimationSample* pickup) {
            auto flags = e.metadata.find(0);
            auto meta = [&](int id, int fallback = 0) {
                auto found = e.metadata.find(std::uint8_t(id));
                return found == e.metadata.end() ? fallback : int(found->second.integer);
            };
            mc::RenderEntity r;
            r.id = e.id;
            r.type = e.type;
            r.kind = e.kind == mc::EntityKind::Player          ? 0
                     : e.kind == mc::EntityKind::Mob           ? 1
                     : e.kind == mc::EntityKind::ExperienceOrb ? 3
                                                               : 2;
            r.x = e.previousX + (e.x - e.previousX) * alpha;
            r.y = e.previousY + (e.y - e.previousY) * alpha;
            r.z = e.previousZ + (e.z - e.previousZ) * alpha;
            if(pickup){r.x=pickup->x;r.y=pickup->y;r.z=pickup->z;r.pickup=true;r.pickupSourceX=e.x;r.pickupSourceY=e.y;r.pickupSourceZ=e.z;}
            r.yaw = e.yaw;
            r.pitch = e.pitch;
            r.headYaw = e.headYaw;
            r.player = e.kind == mc::EntityKind::Player;
            r.name = e.name;
            r.ageTicks = float(e.ageTicks) + alpha;
            r.hurtTime=e.hurtTime;r.deathTime=e.deathTime;r.burning=(meta(0)&1)!=0;
            r.skin = r.player ? skins.get(e.uuid) : nullptr;
            for(int slot=0;slot<5;++slot){const auto& item=e.equipment[slot];r.equipment[slot]={item.id,item.damage,item.count,&item.nbt};}
            r.usingItem=(meta(0)&16)!=0;
            const auto dimensions=mc::entityDimensions(r.kind,r.type);r.width=dimensions.width;r.height=dimensions.height;
            r.xpValue = e.xpValue;
            r.hoverStart = e.hoverStart;
            if(e.kind==mc::EntityKind::Object&&e.type==73)r.itemMetadata=e.objectData;
            if(e.projectile)r.projectileShake=e.projectile->shake;
            if(e.kind==mc::EntityKind::Object&&e.type==90){
                const auto angler=game.entities.find(e.objectData);
                r.localAngler=e.objectData==game.entityId;r.fishingLine=r.localAngler||angler!=game.entities.end();
                if(r.localAngler){r.anglerX=game.player.previousX+(game.player.x-game.player.previousX)*alpha;r.anglerY=game.player.previousY+(game.player.y-game.player.previousY)*alpha;r.anglerZ=game.player.previousZ+(game.player.z-game.player.previousZ)*alpha;r.anglerYaw=r.anglerBodyYaw=game.player.yaw;r.anglerPitch=game.player.pitch;r.anglerSneaking=game.player.sneaking;r.anglerEyeHeight=float(game.player.eyeY()-game.player.y);}
                else if(angler!=game.entities.end()){const auto& owner=angler->second;r.anglerX=owner.previousX+(owner.x-owner.previousX)*alpha;r.anglerY=owner.previousY+(owner.y-owner.previousY)*alpha;r.anglerZ=owner.previousZ+(owner.z-owner.previousZ)*alpha;r.anglerYaw=r.anglerBodyYaw=owner.yaw;r.anglerPitch=owner.pitch;const auto flags=owner.metadata.find(0);r.anglerSneaking=flags!=owner.metadata.end()&&(flags->second.integer&2);r.anglerEyeHeight=r.anglerSneaking?1.54f:1.62f;}
                const auto pose=entityAnimations.sample(e.objectData,alpha);if(pose.valid){r.anglerYaw=pose.headYaw;r.anglerPitch=pose.pitch;r.anglerBodyYaw=pose.bodyYaw;r.anglerSwing=pose.pose.swingProgress;}
            }
            if (e.kind == mc::EntityKind::Object && e.type == 2) {
                auto stack = e.metadata.find(10);
                if (stack != e.metadata.end() && stack->second.type == 5) {
                    try {
                        mc::net::Reader bytes(stack->second.raw);
                        auto item = mc::Game::readItem(bytes);
                        r.itemId = item.id;
                        r.itemMetadata = item.damage;
                        r.itemCount = item.count;
                        r.itemNbt=std::move(item.nbt);
                    } catch (const mc::net::ProtocolError &) {
                    }
                }
            }
            r.limbSwingAmount = float(std::min(1., std::hypot(e.x - e.previousX, e.z - e.previousZ) * 4));
            r.limbSwing = r.ageTicks * .7f;
            const auto animation=pickup?mc::EntityAnimationSample{}:entityAnimations.sample(e.id,alpha);
            if(animation.valid){
                r.yaw=(e.kind==mc::EntityKind::Object&&e.type!=78)?animation.previousYaw+(animation.yaw-animation.previousYaw)*alpha:animation.bodyYaw;
                r.headYaw=animation.headYaw;r.pitch=animation.pitch;
                r.limbSwing=animation.pose.limbSwing;r.limbSwingAmount=animation.pose.limbSwingAmount;r.useTicksRemaining=animation.useTicksRemaining;}
            if(e.projectile){r.yaw=e.projectile->previousYaw+(e.yaw-e.projectile->previousYaw)*alpha;r.pitch=e.projectile->previousPitch+(e.pitch-e.projectile->previousPitch)*alpha;}
            r.sneaking = flags != e.metadata.end() && (flags->second.integer & 2);
            r.invisible = (meta(0) & 0x20) != 0;
            r.showName = r.player || meta(3) != 0;
            if(r.player){
                mc::hud::NameTagInput name;
                name.invisible=r.invisible;name.viewerSpectator=game.gameMode==3;name.sneaking=r.sneaking;
                const std::string viewer=game.client.username().empty()?username:game.client.username();
                const auto* team=mc::hud::playerTeam(game,r.name,&name.entityTeam);
                mc::hud::playerTeam(game,viewer,&name.viewerTeam);
                if(team){name.visibility=team->nameTagVisibility;name.seeFriendlyInvisibles=(team->flags&2)!=0;}
                const double dx=r.x-camera.x,dy=r.y-camera.y,dz=r.z-camera.z;name.distanceSquared=dx*dx+dy*dy+dz*dz;
                const auto policy=mc::hud::nameTagPolicy(name);r.showName=policy.visible;r.translucentBody=policy.bodyAlpha>0&&policy.bodyAlpha<1;
                r.name=mc::hud::teamName(game,r.name);
            }
            r.pose.entityId = e.id;
            r.pose.bodyYaw=r.yaw*.01745329252f;r.pose.partialTick=alpha;
            auto hp = e.metadata.find(6);
            if (hp != e.metadata.end())
                r.pose.health = float(hp->second.number);
            r.pose.riding = e.vehicleId >= 0;
            r.pose.heldItemRight = e.equipment[0].empty() ? 0 : 1;
            r.pose.skinParts=r.player?std::uint8_t(meta(10,127)):127;
            float swingDelta=e.swingProgress-e.previousSwingProgress;if(swingDelta<0)swingDelta+=1;
            r.pose.swingProgress=e.previousSwingProgress+swingDelta*alpha;
            if (e.kind == mc::EntityKind::Mob) {
                r.child = (e.type == 54 || e.type == 57) ? meta(12) != 0 : meta(12) < 0;
                if(r.child){r.width*=.5f;r.height*=.5f;}
                if (e.type == 51)
                    r.pose.variant = meta(13);
                else if (e.type == 55 || e.type == 62)
                    r.pose.scale = float(std::max(1, meta(16, 1)));
                else if (e.type == 56)
                    r.pose.variant = meta(16);
                else if (e.type == 120 || e.type == 101)
                    r.pose.variant = meta(e.type == 120 ? 16 : 18);
                else if (e.type == 95 || e.type == 98) {
                    int bits = meta(16);
                    if (bits & 1)
                        r.pose.flags |= mc::PoseSitting;
                    if (bits & 4)
                        r.pose.flags |= mc::PoseTamed;
                    if (bits & 2)
                        r.pose.flags |= mc::PoseAngry;
                    r.pose.collarColor = meta(20, 14);
                    if (e.type == 98)
                        r.pose.variant = meta(18);
                } else if (e.type == 65) {
                    if (meta(16) & 1)
                        r.pose.flags |= mc::PoseHanging;
                } else if (e.type == 68) {
                    if (meta(16) & 4)
                        r.pose.flags |= mc::PoseElder;
                } else if (e.type == 100) {
                    r.pose.horseType = meta(19);
                    r.pose.variant = meta(20);
                    r.pose.horseArmor = meta(22);
                    int bits = meta(16);
                    if (bits & 4)
                        r.pose.flags |= mc::PoseSaddled;
                    if (bits & 8)
                        r.pose.flags |= mc::PoseChested;
                    if (e.vehicleId >= 0)
                        r.pose.flags |= mc::PoseHasRider;
                }
            }
            if((e.kind==mc::EntityKind::Mob&&e.type==30)||(e.kind==mc::EntityKind::Object&&e.type==78)) {
                    int bits = meta(10);
                    r.child = (bits & 1) != 0;
                    r.width=(bits&16)?0.f:r.child?.25f:.5f;r.height=(bits&16)?0.f:r.child?.9875f:1.975f;
                    if (bits & 4)
                        r.pose.flags |= mc::PoseShowArms;
                    if (bits & 8)
                        r.pose.flags |= mc::PoseNoBasePlate;
                    if (bits & 16)
                        r.pose.flags |= mc::PoseMarker;
                    auto rotation = [&](int id, std::array<float, 3> &value) {
                        auto m = e.metadata.find(std::uint8_t(id));
                        if (m != e.metadata.end() && m->second.type == 7 && m->second.raw.size() == 12) {
                            mc::net::Reader data(m->second.raw);
                            value = {data.f32(), data.f32(), data.f32()};
                        }
                    };
                    rotation(11, r.pose.standHead);
                    rotation(12, r.pose.standBody);
                    rotation(13, r.pose.standLeftArm);
                    rotation(14, r.pose.standRightArm);
                    rotation(15, r.pose.standLeftLeg);
                    rotation(16, r.pose.standRightLeg);
            }
            entities.push_back(r);
        };
        for(const auto& pair:game.entities)appendEntity(pair.second,nullptr);
        const auto pickupFrame=pickupAnimations.frame(alpha);
        for(const auto& pickup:pickupFrame)appendEntity(*pickup.entity,&pickup);
        auto localSkin = skins.get(game.client.uuid().empty()?o.uuid:game.client.uuid());
        renderer.setPlayerPreview(localSkin,{{game.inventory[36+game.selectedSlot],game.inventory[8],game.inventory[7],game.inventory[6],game.inventory[5]}});
        if (perspective && game.spawned) {
            mc::RenderEntity self;
            self.id = game.entityId;
            self.player = true;
            self.skin = localSkin;
            self.x = game.player.previousX + (game.player.x - game.player.previousX) * alpha;
            self.y = game.player.previousY + (game.player.y - game.player.previousY) * alpha;
            self.z = game.player.previousZ + (game.player.z - game.player.previousZ) * alpha;
            self.yaw = self.headYaw = game.player.yaw;
            self.pitch = game.player.pitch;
            self.sneaking = game.player.sneaking;
            self.hurtTime=game.hurtTime;self.deathTime=game.deathTime;
            self.equipment[0]={game.inventory[36+game.selectedSlot].id,game.inventory[36+game.selectedSlot].damage,game.inventory[36+game.selectedSlot].count,&game.inventory[36+game.selectedSlot].nbt};
            for(int slot=1;slot<5;++slot){const auto& item=game.inventory[9-slot];self.equipment[slot]={item.id,item.damage,item.count,&item.nbt};}
            self.usingItem=controller.usingItem();self.useTicksRemaining=controller.useTicksRemaining();self.pose.localPlayer=true;
            self.pose.spectator=game.gameMode==3;
            const auto selfFlags=game.playerMetadata.find(0);
            self.invisible=selfFlags!=game.playerMetadata.end()&&(selfFlags->second.integer&0x20);
            self.burning=local.active()?localFireVisuals.burning():(selfFlags!=game.playerMetadata.end()&&(selfFlags->second.integer&1));
            self.pose.skinParts=preferences.skinParts;
            self.ageTicks = float(game.ticks) + alpha;
            self.limbSwing = self.ageTicks * .7f;
            self.limbSwingAmount = float(std::min(
                1., std::hypot(game.player.x - game.player.previousX, game.player.z - game.player.previousZ) * 4));
            const auto animation=entityAnimations.sample(game.entityId,alpha);
            if(animation.valid){self.yaw=animation.bodyYaw;self.headYaw=animation.headYaw;self.pitch=animation.pitch;self.limbSwing=animation.pose.limbSwing;self.limbSwingAmount=animation.pose.limbSwingAmount;}
            self.pose.bodyYaw=self.yaw*.01745329252f;self.pose.partialTick=alpha;
            float localSwingDelta=swing-previousSwing;if(localSwingDelta<0)localSwingDelta+=1;
            self.pose.swingProgress=previousSwing+localSwingDelta*alpha;
            entities.push_back(self);
        }
        renderer.options.respiration=mc::Controller::enchantment(game.inventory[5],5);
        renderer.options.waterBreathing=game.effects.count(13)!=0;
        renderer.options.firstPersonEffects=game.spawned&&!perspective&&game.gameMode!=3;
        const auto burningFlags=game.playerMetadata.find(0);
        renderer.options.burning=local.active()?localFireVisuals.burning():(burningFlags!=game.playerMetadata.end()&&(burningFlags->second.integer&1));
        if (!perspective && game.spawned && game.gameMode != 3 && worldScreen(screen,returnScreen)) {
            mc::FirstPersonPose pose;
            pose.leftSleeve=(preferences.skinParts&4)!=0;pose.rightSleeve=(preferences.skinParts&8)!=0;
            pose.itemId = renderedItem.empty() ? -1 : renderedItem.id;
            pose.metadata = renderedItem.damage;
            pose.fishingCast=renderedItem.id==346&&std::any_of(game.entities.begin(),game.entities.end(),[&](const auto& pair){
                return pair.second.kind==mc::EntityKind::Object&&pair.second.type==90&&pair.second.objectData==game.entityId;
            });
            float delta = swing - previousSwing;
            if (delta < 0)
                delta += 1;
            pose.swing = previousSwing + delta * alpha;
            pose.equip = 1 - (previousEquipped + (equipped - previousEquipped) * alpha);
            pose.usingItem = controller.usingItem();
            pose.useTicksRemaining = controller.useTicksRemaining();
            pose.maxUseTicks = mc::registry::item(pose.itemId).useDuration;
            int action = mc::registry::item(pose.itemId).useAction;
            pose.use = action == 1   ? mc::FirstPersonUse::Eat
                       : action == 2 ? mc::FirstPersonUse::Drink
                       : action == 3 ? mc::FirstPersonUse::Block
                       : action == 4 ? mc::FirstPersonUse::Bow
                                     : mc::FirstPersonUse::None;
            pose.partialTicks = alpha;
            pose.cameraPitch = pose.armPitch = game.player.pitch;
            pose.cameraYaw = pose.armYaw = game.player.yaw;
            pose.slim = localSkin && localSkin->slim;
            auto flags = game.playerMetadata.find(0);
            pose.invisible = flags != game.playerMetadata.end() && (flags->second.integer & 0x20);
            renderer.setFirstPerson(pose, localSkin, &renderedItem);
        } else
            renderer.hideFirstPerson();
        mc::Camera eyeCamera{game.player.x, game.player.eyeY(), game.player.z, game.player.yaw, game.player.pitch, fov};
        target = raycast(game.world, eyeCamera, game.gameMode == 1 ? 5. : 4.5);
        if (target.hit && interactive && pickEntity(game, eyeCamera, target).id < 0) {
            const auto bounds = game.world.selectionBounds(target.x, target.y, target.z);
            renderer.selection(bounds.minX, bounds.minY, bounds.minZ, bounds.maxX, bounds.maxY, bounds.maxZ);
        } else
            renderer.clearSelection();
        std::vector<mc::RenderBreak> breaks;
        for(const auto& entry:game.blockBreakProgress){const auto& b=entry.second;breaks.push_back({b.x,b.y,b.z,b.stage});}
        if(controller.mining){const auto& b=controller.currentTarget;breaks.push_back({b.x,b.y,b.z,int(controller.progress*10.f)-1});}
        renderer.setBlockBreaks(std::move(breaks));
        std::vector<mc::RenderSign> signs;
        signs.reserve(game.signs.size());
        for (const auto &entry : game.signs) {
            const auto& sign=entry.second;
            if(std::hypot(sign.x-camera.x,sign.z-camera.z)>80)continue;
            mc::RenderSign rendered{sign.x,sign.y,sign.z,sign.lines};
            for(int row=0;row<4;++row)if(!sign.jsonLines[row].empty())rendered.lines[row]=hud.text(sign.jsonLines[row]).formatted();
            signs.push_back(std::move(rendered));
        }
        std::vector<mc::RenderTile> tiles;
        tiles.reserve(game.tileEntities.size() + chestLids.size());
        std::set<std::uint64_t> tileKeys;
        for (const auto &entry : game.tileEntities) {
            const auto &t = entry.second;
            const auto lid = chestLids.find(entry.first);
            tiles.push_back({t.x, t.y, t.z, &t.nbt, lid == chestLids.end() ? mc::ChestLidState{} : lid->second});
            tileKeys.insert(entry.first);
        }
        for (const auto &entry : chestLids)
            if (!tileKeys.count(entry.first)) {
                auto key = entry.first;
                int x = int(key >> 38), y = int((key >> 26) & 4095), z = int(key & 0x3ffffff);
                if (x >= 33554432)
                    x -= 67108864;
                if (z >= 33554432)
                    z -= 67108864;
                tiles.push_back({x, y, z, nullptr, entry.second});
            }
        const bool worldVisible=worldScreen(screen,returnScreen);
        if(worldVisible)renderer.render(game.world, camera, window.width(), window.height(), elapsed, game.worldTime, entities, signs,
                        tiles, &skins);
        audio.updateListener(camera.x, camera.y, camera.z, camera.yaw, camera.pitch);
        audio.tick();
        if (interactive && target.hit && pressed("pickItem") && game.gameMode == 1) {
            auto state = game.world.getBlock(target.x, target.y, target.z);
            game.setCreativeSlot(36 + game.selectedSlot,
                                 {std::int16_t(mc::blockId(state)), 64, std::int16_t(mc::blockMeta(state)), {0}});
        }
        int w = window.width(), h = window.height();
        float scale = 1;
        while (w / (scale + 1) >= 320 && h / (scale + 1) >= 240 &&
               (preferences.guiScale == 0 || scale < preferences.guiScale))
            scale += 1;
        if ((tr.unicode()||preferences.forceUnicodeFont) && int(scale) % 2 && scale > 1)
            scale -= 1;
        if (screen == Screen::Menu)
            renderer.menuBackground(w, h, menuSeconds);
        renderer.beginUI(w, h);
        auto centerText = [&](const std::string &s, float y, float sc, std::uint32_t color = 0xffffffff) {
            renderer.text(s, (w - renderer.textWidth(s, sc)) * .5f, y, sc, color);
        };
        auto button = [&](const std::string &s, float x, float y, float bw, float bh, bool enabled = true) {
            bool hover =
                enabled && window.mouseX >= x && window.mouseX < x + bw && window.mouseY >= y && window.mouseY < y + bh;
            float row = 46 + (!enabled ? 0 : hover ? 40 : 20);
            renderer.sprite("widgets", x, y, bw / 2, bh, 0, row, bw / (2 * scale), 20);
            renderer.sprite("widgets", x + bw / 2, y, bw / 2, bh, 200 - bw / (2 * scale), row, bw / (2 * scale), 20);
            renderer.text(s, x + (bw - renderer.textWidth(s, scale)) * .5f, y + (bh - 8 * scale) * .5f, scale,
                          !enabled ? 0xa0a0a0ff
                          : hover  ? 0xffffa0ff
                                   : 0xe0e0e0ff);
            bool clicked = hover && window.pressed(VK_LBUTTON);
            if (clicked)
                audio.play("gui.button.press", 0, 0, 0, 1, 1, true);
            return clicked;
        };
        if (screen == Screen::Play || screen == Screen::Chat || screen == Screen::Inventory ||
            screen == Screen::Pause) {
            float barW = 182 * scale, barX = (w - barW) * .5f, barY = h - 22 * scale;
            renderer.sprite("widgets", barX, barY, barW, 22 * scale, 0, 0, 182, 22);
            renderer.sprite("widgets", barX - 1 * scale + game.selectedSlot * 20 * scale, barY - 1 * scale, 24 * scale,
                            22 * scale, 0, 22, 24, 22);
            for (int i = 0; i < 9; ++i) {
                float x = barX + (3 + i * 20) * scale, y = barY + 3 * scale;
                if (!game.inventory[36 + i].empty()) {
                    const auto &item = game.inventory[36 + i];
                    renderer.itemIcon(item, x, y, 16 * scale);
                    if (item.count > 1)
                        renderer.text(std::to_string(int(item.count)), x + 17 * scale-renderer.textWidth(std::to_string(int(item.count)),scale), y + 9 * scale, scale);
                }
            }
            if (game.gameMode != 1)
                for (int i = 0; i < 10; ++i) {
                    float x = barX + i * 8 * scale, y = barY - 17 * scale;
                    renderer.sprite("icons", x, y, 9 * scale, 9 * scale, 16, 0, 9, 9);
                    if (game.health > i * 2)
                        renderer.sprite("icons", x, y, 9 * scale, 9 * scale, game.health >= i * 2 + 2 ? 52 : 61, 0, 9,
                                        9);
                    float fx = barX + barW - (i + 1) * 8 * scale - 2 * scale;
                    renderer.sprite("icons", fx, y, 9 * scale, 9 * scale, 16, 27, 9, 9);
                    if (game.food > i * 2)
                        renderer.sprite("icons", fx, y, 9 * scale, 9 * scale, game.food >= i * 2 + 2 ? 52 : 61, 27, 9,
                                        9);
                }
            if (screen == Screen::Play) {
                renderer.rect(w / 2.f - 4 * scale, h / 2.f, 9 * scale, scale, 0xffffffd0);
                renderer.rect(w / 2.f, h / 2.f - 4 * scale, scale, 9 * scale, 0xffffffd0);
            }
            if (!game.spawned && game.online) {
                renderer.rect(w * .2f, h * .4f, w * .6f, 60 * scale, 0x10202aee);
                centerText(game.status, h * .4f + 20 * scale, scale);
            }
            const int guiW=int(std::ceil(w/scale)),guiH=int(std::ceil(h/scale));
            mc::hud::ChatOptions chatOptions;chatOptions.open=screen==Screen::Chat;chatOptions.scroll=chatScroll;
            chatOptions.hidden=preferences.chatVisibility==2;chatOptions.systemOnly=preferences.chatVisibility==1;
            chatOptions.colors=preferences.chatColors;chatOptions.opacity=preferences.chatOpacity;chatOptions.scale=preferences.chatScale;
            chatOptions.widthSetting=preferences.chatWidth;chatOptions.focusedHeight=preferences.chatHeightFocused;chatOptions.unfocusedHeight=preferences.chatHeightUnfocused;
            auto measureInput=[&](const std::string& text){return float(hud.metrics().width(text));};
            auto inputView=chatField.view(float(guiW-8),measureInput);
            if(screen==Screen::Chat&&window.pressed(VK_LBUTTON)&&window.mouseY/scale>=guiH-14){
                chatField.click(inputView,window.mouseX/scale-4,measureInput,window.down(VK_SHIFT));chatCompletion.cancel();chatCursorChanged=elapsed;
                inputView=chatField.view(float(guiW-8),measureInput);
            }
            chatOptions.input=inputView.text;chatOptions.cursorX=inputView.cursorX;chatOptions.cursorAtEnd=inputView.cursorAtEnd;
            chatOptions.selectionStartX=inputView.selectionStartX;chatOptions.selectionEndX=inputView.selectionEndX;
            chatOptions.cursorVisible=int((elapsed-chatCursorChanged)*20)/6%2==0;chatOptions.completionHint=chatCompletion.hint();
            const auto chatLayout=hud.chat(game,guiW,guiH,chatOptions);
            mc::hud::Hud::draw(renderer,hud.sidebar(game,guiW,guiH,game.client.username().empty()?username:game.client.username()),scale);
            mc::hud::Hud::draw(renderer,hud.experience(game,guiW,guiH),scale);
            mc::hud::Hud::draw(renderer,hud.titles(game,guiW,guiH,alpha),scale);
            mc::hud::Hud::draw(renderer,hud.actionBar(game,guiW,guiH,alpha),scale);
            mc::hud::Hud::draw(renderer,chatLayout,scale);
            const auto tab=hud.tab(game,guiW,{down("playerlist")&&screen==Screen::Play,!game.online||game.client.encrypted(),std::uint64_t(elapsed*1000)});
            mc::hud::Hud::draw(renderer,tab,scale,[&](const mc::hud::Draw& draw,float sc){renderer.playerFace(skins.get(draw.uuid),draw.bounds.x*sc,draw.bounds.y*sc,draw.bounds.width*sc,draw.hat);});
            if(screen==Screen::Chat)if(const auto* hit=chatLayout.hit(window.mouseX/scale,window.mouseY/scale)){
                mc::hud::Hud::draw(renderer,hud.tooltip(*hit,window.mouseX/scale,window.mouseY/scale,guiW,guiH,advancedTooltips),scale);
                if(window.pressed(VK_LBUTTON)){
                    const auto action=mc::chat::click(hit->style,window.down(VK_SHIFT));
                    using Kind=mc::chat::ActionRequest::Kind;
                    if(action.kind==Kind::RunCommand)game.sendChat(action.value);
                    else if(action.kind==Kind::SuggestCommand){chatField.setText(action.value);chatCompletion.cancel();chatCursorChanged=elapsed;}
                    else if(action.kind==Kind::InsertText){chatField.insert(action.value);chatCompletion.cancel();chatCursorChanged=elapsed;}
                    else if(action.kind==Kind::OpenUrl&&preferences.chatLinks){
                        pendingUrl=action.value;
                        if(preferences.chatLinksPrompt)screen=Screen::ConfirmLink;
                        else ShellExecuteW(window.handle(),L"open",mc::wide(pendingUrl).c_str(),nullptr,nullptr,SW_SHOWNORMAL);
                    }
                }
            }
            if (game.health <= 0 && screen == Screen::Play) {
                renderer.rect(0, 0, w, h, 0x80101099);
                centerText("You died!", h * .35f, scale * 2);
                if (button("Respawn", w * .35f, h * .5f, w * .3f, 28 * scale) || window.pressed(VK_RETURN)) {
                    if(game.respawn())localFireVisuals.reset();
                }
            }
        }
        if (debug && !preferences.reducedDebugInfo && !game.reducedDebugInfo && (screen == Screen::Play || screen == Screen::Pause)) {
            const auto &stats = renderer.stats();
            std::ostringstream s;
            s << std::fixed << std::setprecision(1) << "NativeVoxel C++ / protocol 47 / " << fps << " FPS\nXYZ "
              << game.player.x << " / " << game.player.y << " / " << game.player.z << "\nChunks "
              << game.world.chunks().size() << "  Visible sections " << stats.visibleSections << " / "
              << stats.loadedSections << "\nTriangles " << stats.triangles << "  Mesh queue " << stats.pendingMeshes
              << "\nCPU render " << stats.frameMs << " ms  Packets " << game.packetsReceived << "  Corrections "
              << game.positionCorrections;
            renderer.rect(4, 4, 660, 104, 0x00000090);
            renderer.text(s.str(), 9, 9, 1.6f);
        }
        const bool guiMenu = screen == Screen::Menu || screen == Screen::Worlds || screen == Screen::NewWorld ||
                             screen == Screen::ServerList || screen == Screen::Server || screen==Screen::ServerEdit||screen==Screen::ServerDelete||screen == Screen::Options ||
                             screen == Screen::Language || optionsPage(screen) || screen == Screen::Pause ||
                             screen == Screen::Disconnected || screen==Screen::Connecting || screen==Screen::ConfirmLink || screen==Screen::ResourcePacks;
        if (guiMenu) {
            if (optionsPage(screen)) {
                // OptionsScreen owns its background and emits one page per frame.
            } else if (screen == Screen::Menu) {
                renderer.gradient(0, 0, w, h, 0xffffff80, 0xffffff00);
                renderer.gradient(0, 0, w, h, 0x00000000, 0x00000080);
            } else if (worldVisible)
                renderer.gradient(0, 0, w, h, 0x101010b0, 0x202020d0);
            else
                for (int yy = 0; yy < h; yy += 32 * int(scale))
                    for (int xx = 0; xx < w; xx += 32 * int(scale))
                        renderer.sprite("options_background", xx, yy, 32 * scale, 32 * scale, 0, 0, 16, 16, 0x404040ff);
            float bw = 200 * scale, bh = 20 * scale, x = (w - bw) * .5f, y = (h / scale / 4 + 48) * scale;
            if (screen == Screen::Menu) {
                float logoX = w * .5f - 137 * scale;
                renderer.sprite("minecraft", logoX, 30 * scale, 155 * scale, 44 * scale, 0, 0, 155, 44);
                renderer.sprite("minecraft", logoX + 155 * scale, 30 * scale, 155 * scale, 44 * scale, 0, 45, 155, 44);
                const std::string splash = "Also try Terraria!";
                float pulse = (1.8f - std::abs(std::sin(float(elapsed) * 6.2831853f) * .1f)) * 100 /
                              (renderer.textWidth(splash, 1) + 32);
                renderer.rotatedText(splash, w * .5f + 90 * scale, 70 * scale, pulse * scale, -20, 0xffff00ff);
                if (button(tr("menu.singleplayer"), x, y, bw, bh)) {
                    refreshSaves();
                    screen = Screen::Worlds;
                }
                if (button(tr("menu.multiplayer"), x, y + 24 * scale, bw, bh))
                    screen = Screen::ServerList;
                button(tr("menu.online"), x, y + 48 * scale, bw, bh, false);
                if (button(tr("menu.options"), x, y + 84 * scale, 98 * scale, bh)) {
                    returnScreen = Screen::Menu;
                    screen = Screen::Options;
                }
                if (button(tr("menu.quit"), x + 102 * scale, y + 84 * scale, 98 * scale, bh))
                    quit = true;
                if (button("", x - 24 * scale, y + 84 * scale, 20 * scale, bh)) {
                    returnScreen = Screen::Menu;
                    screen = Screen::Language;
                }
                renderer.sprite("widgets", x - 24 * scale, y + 84 * scale, 20 * scale, 20 * scale, 0, 106, 20, 20);
                renderer.text("Minecraft 1.8.9", 2 * scale, h - 10 * scale, scale);
                const std::string copyright = "Copyright Mojang AB. Do not distribute!";
                renderer.text(copyright, w - renderer.textWidth(copyright, scale) - 2 * scale, h - 10 * scale, scale);
            } else if (screen == Screen::Worlds) {
                centerText(tr("selectWorld.title"), 20 * scale, scale);
                renderer.rect(0, 40 * scale, w, h - 106 * scale, 0x00000080);
                for (std::size_t i = 0; i < savePaths.size(); ++i) {
                    float rowY = (48 + int(i) * 36) * scale;
                    if (rowY > h - 80 * scale)
                        break;
                    if (int(i) == selectedWorld) {
                        renderer.rect(x - 8 * scale, rowY - 4 * scale, bw + 16 * scale, 34 * scale, 0x808080ff);
                        renderer.rect(x - 7 * scale, rowY - 3 * scale, bw + 14 * scale, 32 * scale, 0x000000ff);
                    }
                    renderer.text(savePaths[i].filename().u8string(), x, rowY, scale);
                    renderer.text("Anvil / 1.8.9", x, rowY + 12 * scale, scale, 0x808080ff);
                    if (window.pressed(VK_LBUTTON) && window.mouseY >= rowY - 4 * scale &&
                        window.mouseY < rowY + 30 * scale)
                        selectedWorld = int(i);
                }
                if (savePaths.empty())
                    centerText("没有本地世界", 70 * scale, scale, 0xa0a0a0ff);
                centerText("可读取 Anvil 存档；原版生成与完整单人模拟仍在移植。", h - 77 * scale, scale * .8f,
                           0xc0c0c0ff);
                if (button(tr("selectWorld.select"), w * .5f - 154 * scale, h - 52 * scale, 150 * scale, bh,
                           !savePaths.empty()))
                    loadSave(savePaths[selectedWorld].u8string());
                if (button(tr("selectWorld.create"), w * .5f + 4 * scale, h - 52 * scale, 150 * scale, bh)) {
                    screen = Screen::NewWorld;
                    newWorldName = "新的世界";
                }
                if (button(tr("gui.cancel"), x, h - 28 * scale, bw, bh))
                    screen = Screen::Menu;
            } else if (screen == Screen::NewWorld) {
                centerText(tr("selectWorld.create"), 20 * scale, scale);
                renderer.text(tr("selectWorld.enterName"), x, 58 * scale, scale, 0xa0a0a0ff);
                renderer.rect(x - 1, 73 * scale - 1, bw + 2, 22 * scale + 2, worldField == 0 ? 0xffffffff : 0xa0a0a0ff);
                renderer.rect(x, 73 * scale, bw, 22 * scale, 0x000000ff);
                renderer.text(newWorldName + ((!worldField && int(elapsed * 2) % 2) ? "_" : ""), x + 4 * scale,
                              80 * scale, scale);
                renderer.text("种子", x, 101 * scale, scale, 0xa0a0a0ff);
                renderer.rect(x - 1, 112 * scale - 1, bw + 2, 20 * scale + 2,
                              worldField == 1 ? 0xffffffff : 0xa0a0a0ff);
                renderer.rect(x, 112 * scale, bw, 20 * scale, 0x000000ff);
                renderer.text(newWorldSeed + ((worldField && int(elapsed * 2) % 2) ? "_" : ""), x + 4 * scale,
                              118 * scale, scale);
                if (window.pressed(VK_LBUTTON) && window.mouseX >= x && window.mouseX < x + bw) {
                    if (window.mouseY >= 73 * scale && window.mouseY < 95 * scale)
                        worldField = 0;
                    else if (window.mouseY >= 112 * scale && window.mouseY < 132 * scale)
                        worldField = 1;
                }
                if (button(newWorldMode == 1 ? "模式：创造" : "模式：生存核心", x, 141 * scale, bw, bh))
                    newWorldMode = newWorldMode == 1 ? 0 : 1;
                centerText("原版基础地形；完整装饰、结构和生物仍在接入。", 169 * scale, scale * .8f, 0xa0a0a0ff);
                if (button(tr("selectWorld.create"), w * .5f - 154 * scale, h - 28 * scale, 150 * scale, bh,
                           !newWorldName.empty())) {
                    const auto folder = saveFolderName(newWorldName);
                    fs::path targetPath = savesRoot / fs::u8path(folder);
                    int suffix = 1;
                    while (fs::exists(targetPath))
                        targetPath = savesRoot / fs::u8path(folder + " (" + std::to_string(suffix++) + ")");
                    if (generateWorld(newWorldName, newWorldSeed, newWorldMode) &&
                        !local.saveNew(game, targetPath.u8string(), error)) {
                        notice = error;
                        screen = Screen::Disconnected;
                    }
                }
                if (button(tr("gui.cancel"), w * .5f + 4 * scale, h - 28 * scale, 150 * scale, bh))
                    screen = Screen::Worlds;
            } else if (screen == Screen::ServerList) {
                mc::serverui::Input input;input.mouseX=window.mouseX/scale;input.mouseY=window.mouseY/scale;input.wheel=float(window.wheel);
                input.click=window.pressed(VK_LBUTTON);input.enter=window.pressed(VK_RETURN);input.escape=window.pressed(VK_ESCAPE);
                input.up=window.pressed(VK_UP);input.down=window.pressed(VK_DOWN);input.shift=window.down(VK_SHIFT);
                input.refresh=window.pressed(VK_F5);input.deleteKey=window.pressed(VK_DELETE);input.milliseconds=std::uint64_t(elapsed*1000);
                auto layout=serverBrowser.layout(hud,tr,int(std::ceil(w/scale)),int(std::ceil(h/scale)),input);
                const auto action=serverBrowser.interact(layout,input);mc::serverui::draw(renderer,layout,scale);
                using Action=mc::serverui::Action::Kind;
                if(action.kind==Action::Join&&action.index>=0){serverText=serverBrowser.servers[action.index].address;connect();}
                else if(action.kind==Action::DirectConnect){screen=Screen::Server;activeField=0;}
                else if(action.kind==Action::Add){editedServer={};editedServer.name="Minecraft Server";editingServer=-1;activeField=0;screen=Screen::ServerEdit;}
                else if(action.kind==Action::Edit&&action.index>=0){editedServer=serverBrowser.servers[action.index];editingServer=action.index;activeField=0;screen=Screen::ServerEdit;}
                else if(action.kind==Action::Delete&&action.index>=0){editingServer=action.index;screen=Screen::ServerDelete;}
                else if(action.kind==Action::Refresh){for(auto& server:serverBrowser.servers)server.state=mc::serverui::PingState::Unqueried;}
                else if(action.kind==Action::Cancel)screen=Screen::Menu;
                else if(action.kind==Action::Reordered)serverBrowser.write(serversPath,error);
            } else if (screen == Screen::Server) {
                centerText(tr("selectServer.direct"), 20 * scale, scale);
                renderer.text(tr("addServer.enterIp"), x, 100 * scale, scale, 0xa0a0a0ff);
                renderer.rect(x-1,116*scale-1,bw+2,20*scale+2,0xa0a0a0ff);
                renderer.rect(x,116*scale,bw,20*scale,0x000000ff);
                renderer.text(hud.metrics().trim(serverText+((int(elapsed*2)%2)?"_":""),192),x+4*scale,122*scale,scale);
                if (button(tr("selectServer.select"), x, h/4+108*scale, bw, bh,!serverText.empty()&&!connectionJob.valid()))
                    connect();
                if (button(tr("gui.cancel"), x, h/4+132*scale, bw, bh))
                    screen = Screen::ServerList;
            } else if(screen==Screen::ServerEdit){
                centerText(tr("addServer.title"),17*scale,scale);
                renderer.text(tr("addServer.enterName"),x,53*scale,scale,0xa0a0a0ff);
                renderer.text(tr("addServer.enterIp"),x,94*scale,scale,0xa0a0a0ff);
                for(int field=0;field<2;++field){const float fy=(field?106:66)*scale;
                    renderer.rect(x-1,fy-1,bw+2,20*scale+2,activeField==field?0xffffffff:0xa0a0a0ff);
                    renderer.rect(x,fy,bw,20*scale,0x000000ff);
                    renderer.text(hud.metrics().trim((field?editedServer.address:editedServer.name)+((field==activeField&&int(elapsed*2)%2)?"_":""),192),x+4*scale,fy+6*scale,scale);
                    if(window.pressed(VK_LBUTTON)&&window.mouseX>=x&&window.mouseX<x+bw&&window.mouseY>=fy&&window.mouseY<fy+20*scale)activeField=field;
                }
                const char* mode=editedServer.resourceMode==mc::serverui::ResourceMode::Prompt?"addServer.resourcePack.prompt":editedServer.resourceMode==mc::serverui::ResourceMode::Enabled?"addServer.resourcePack.enabled":"addServer.resourcePack.disabled";
                if(button(tr("addServer.resourcePack")+": "+tr(mode),x,h/4+72*scale,bw,bh))editedServer.resourceMode=mc::serverui::ResourceMode((int(editedServer.resourceMode)+1)%3);
                if((button(tr("addServer.add"),x,h/4+114*scale,bw,bh,!editedServer.address.empty()&&!editedServer.name.empty())||window.pressed(VK_RETURN))&&!editedServer.address.empty()&&!editedServer.name.empty()){
                    editedServer.state=mc::serverui::PingState::Unqueried;
                    if(editingServer>=0)serverBrowser.servers[editingServer]=editedServer;else{serverBrowser.servers.push_back(editedServer);serverBrowser.selected=int(serverBrowser.servers.size())-1;}
                    serverBrowser.write(serversPath,error);screen=Screen::ServerList;
                }
                if(button(tr("gui.cancel"),x,h/4+138*scale,bw,bh))screen=Screen::ServerList;
            } else if(screen==Screen::ServerDelete){
                centerText(tr("selectServer.deleteQuestion"),h*.35f,scale);
                if(editingServer>=0&&editingServer<int(serverBrowser.servers.size()))centerText(serverBrowser.servers[editingServer].name,h*.35f+20*scale,scale);
                if(button(tr("selectServer.deleteButton"),w*.5f-155*scale,h*.5f,150*scale,bh)){
                    if(editingServer>=0&&editingServer<int(serverBrowser.servers.size()))serverBrowser.servers.erase(serverBrowser.servers.begin()+editingServer);
                    serverBrowser.selected=std::min(editingServer,int(serverBrowser.servers.size())-1);serverBrowser.write(serversPath,error);screen=Screen::ServerList;
                }
                if(button(tr("gui.cancel"),w*.5f+5*scale,h*.5f,150*scale,bh))screen=Screen::ServerList;
            } else if (screen == Screen::Pause) {
                centerText(tr("menu.game"), 40 * scale, scale);
                if (button(tr("menu.returnToGame"), x, y - 16 * scale, bw, bh)) {
                    screen = Screen::Play;
                    window.setCapture(true);
                }
                button(tr("gui.achievements"), x, y + 8 * scale, 98 * scale, bh, false);
                button(tr("gui.stats"), x + 102 * scale, y + 8 * scale, 98 * scale, bh, false);
                if (button(tr("menu.options"), x, y + 32 * scale, 98 * scale, bh)) {
                    returnScreen = Screen::Pause;
                    screen = Screen::Options;
                }
                button(tr("menu.shareToLan"), x + 102 * scale, y + 32 * scale, 98 * scale, bh, false);
                if (button(game.online ? tr("menu.disconnect") : tr("menu.returnToMenu"), x, y + 56 * scale, bw, bh)) {
                    if (saveSnapshot()) {
                        offlineSession = false;
                        game.disconnect();
                        controller.reset();
                        audio.stopAll();
                        game.startPreview(5);
                        setPreviewItems();
                        renderer.resetWorld();
                        screen = Screen::Menu;
                    } else
                        screen = Screen::Disconnected;
                }
            } else if (optionsPage(screen)) {
                const auto previousScreen=screen;
                optionsScreen.setWorldContext(returnScreen==Screen::Pause&&game.spawned,game.online,game.difficulty,game.hardcore);
                const bool done=optionsScreen.draw(optionsPageKind(screen),preferences,renderer,window,tr,scale);
                const auto action=optionsScreen.takeAction();
                if(optionsScreen.takePressSound()){
                    if(action==mc::OptionsAction::SuperSecret){
                        std::vector<std::string> sounds;std::uint64_t random=mc::SoundRegistry::javaSeed(std::uint64_t(elapsed*1000000));
                        for(const auto& name:audio.registry().eventNames()){
                            mc::ResolvedSound resolved;std::string ignored;
                            if(audio.registry().resolve(name,random,resolved,ignored)&&(resolved.category==mc::SoundCategory::Neutral||resolved.category==mc::SoundCategory::Block||resolved.category==mc::SoundCategory::Hostile||resolved.category==mc::SoundCategory::Player||resolved.category==mc::SoundCategory::Weather))sounds.push_back(name);
                        }
                        if(!sounds.empty())audio.play(sounds[(random>>17)%sounds.size()],0,0,0,.5f,1,true);
                    } else audio.play("gui.button.press",0,0,0,1,1,true);
                }
                if(done)screen=previousScreen==Screen::Options?returnScreen:Screen::Options;
                else switch(action){
                case mc::OptionsAction::OpenSkin:screen=Screen::Skin;break;
                case mc::OptionsAction::OpenSounds:screen=Screen::Sounds;break;
                case mc::OptionsAction::OpenStream:screen=Screen::Stream;break;
                case mc::OptionsAction::OpenVideo:screen=Screen::Settings;break;
                case mc::OptionsAction::OpenControls:screen=Screen::Controls;break;
                case mc::OptionsAction::OpenLanguage:screen=Screen::Language;break;
                case mc::OptionsAction::OpenChat:screen=Screen::ChatSettings;break;
                case mc::OptionsAction::OpenResourcePacks:refreshPacks();screen=Screen::ResourcePacks;break;
                case mc::OptionsAction::OpenSnooper:screen=Screen::Snooper;break;
                case mc::OptionsAction::Difficulty:if(!game.online&&!game.hardcore)game.difficulty=(game.difficulty+1)%4;break;
                default:break;
                }
                if(screen!=previousScreen){optionsScreen.reset();if(!o.hidden&&!o.benchmark&&!o.frames)preferences.save(preferencesPath,&error);}
            } else if (screen == Screen::Language) {
                centerText(tr("options.language"), 16 * scale, scale);
                if (button("简体中文（中国）", x, 60 * scale, bw, bh)) {
                    preferences.language = "zh_CN";
                    tr.load(o.assets, preferences.language);
                }
                if (button("English (US)", x, 84 * scale, bw, bh)) {
                    preferences.language = "en_US";
                    tr.load(o.assets, preferences.language);
                }
                if (button("繁體中文（台灣）", x, 108 * scale, bw, bh)) {
                    preferences.language = "zh_TW";
                    tr.load(o.assets, preferences.language);
                }
                if(button(tr("options.forceUnicodeFont")+": "+tr(preferences.forceUnicodeFont?"options.on":"options.off"),w*.5f-155*scale,h-28*scale,150*scale,bh))preferences.forceUnicodeFont=!preferences.forceUnicodeFont;
                if (button(tr("gui.done"),w*.5f+5*scale,h-28*scale,150*scale,bh)) {
                    if(!o.hidden&&!o.benchmark&&!o.frames)preferences.save(preferencesPath,&error);
                    screen = Screen::Options;
                }
            } else if(screen==Screen::ResourcePacks){
                centerText(tr("resourcePack.title"),16*scale,scale);
                const float left=w*.5f-204*scale,right=w*.5f+4*scale,listTop=32*scale,listBottom=h-55*scale;
                const int visibleRows=std::max(1,int((listBottom-58*scale)/(36*scale)));
                const int availableCount=int(availablePacks.size())-int(selectedPacks.size());
                if(window.wheel&&window.mouseY>=listTop&&window.mouseY<listBottom){
                    if(window.mouseX>=left&&window.mouseX<left+200*scale)availablePackScroll=std::clamp(availablePackScroll-window.wheel,0,std::max(0,availableCount-visibleRows));
                    if(window.mouseX>=right&&window.mouseX<right+200*scale)selectedPackScroll=std::clamp(selectedPackScroll-window.wheel,0,std::max(0,int(selectedPacks.size())+1-visibleRows));
                }
                renderer.rect(left,listTop,200*scale,listBottom-listTop,0x00000090);renderer.rect(right,listTop,200*scale,listBottom-listTop,0x00000090);
                renderer.text(tr("resourcePack.available.title"),left+5*scale,35*scale,scale);
                renderer.text(tr("resourcePack.selected.title"),right+5*scale,35*scale,scale);
                auto drawPack=[&](const std::string& path,float px,int row,bool active){
                    const int visibleRow=row-(active?selectedPackScroll:availablePackScroll);
                    if(visibleRow<0||(58+visibleRow*36+34)*scale>listBottom)return;
                    const auto found=std::find_if(availablePacks.begin(),availablePacks.end(),[&](const auto& pack){return pack.path==path;});if(found==availablePacks.end())return;
                    const auto index=std::size_t(found-availablePacks.begin());const auto& pack=*found;const float py=(58+visibleRow*36)*scale;
                    renderer.sprite(pack.iconWidth?"pack_icon_"+std::to_string(index):"unknown_pack",px,py,32*scale,32*scale,0,0,pack.iconWidth?pack.iconWidth:32,pack.iconHeight?pack.iconHeight:32);
                    renderer.text(hud.metrics().trim(pack.name,150),px+34*scale,py+1*scale,scale);
                    renderer.text(hud.metrics().trim(mc::chat::stripFormatting(pack.description),150),px+34*scale,py+12*scale,scale*.8f,0x808080ff);
                    if(!packJob.valid()&&window.mouseX>=px&&window.mouseX<px+32*scale&&window.mouseY>=py&&window.mouseY<py+32*scale){
                        renderer.rect(px,py,32*scale,32*scale,0x000000aa);
                        if(active){
                            const auto pos=std::find(selectedPacks.begin(),selectedPacks.end(),path);const int at=int(pos-selectedPacks.begin());
                            if(window.mouseX<px+16*scale){renderer.sprite("resource_packs",px,py,32*scale,32*scale,32,0,32,32);if(window.pressed(VK_LBUTTON))selectedPacks.erase(pos);}
                            else if(window.mouseY<py+16*scale){if(at+1<int(selectedPacks.size())){renderer.sprite("resource_packs",px,py,32*scale,32*scale,96,0,32,32);if(window.pressed(VK_LBUTTON))std::swap(selectedPacks[at],selectedPacks[at+1]);}}
                            else if(at>0){renderer.sprite("resource_packs",px,py,32*scale,32*scale,64,0,32,32);if(window.pressed(VK_LBUTTON))std::swap(selectedPacks[at],selectedPacks[at-1]);}
                        }else{renderer.sprite("resource_packs",px,py,32*scale,32*scale,0,0,32,32);if(window.pressed(VK_LBUTTON))selectedPacks.push_back(path);}
                    }
                };
                int availableRow=0;for(const auto& pack:availablePacks)if(std::find(selectedPacks.begin(),selectedPacks.end(),pack.path)==selectedPacks.end())drawPack(pack.path,left+4*scale,availableRow++,false);
                int selectedRow=0;const auto displayedPacks=selectedPacks;for(auto it=displayedPacks.rbegin();it!=displayedPacks.rend();++it)drawPack(*it,right+4*scale,selectedRow++,true);
                const float defaultY=(58+(selectedRow-selectedPackScroll)*36)*scale;
                if(defaultY>=listTop&&defaultY+32*scale<=listBottom){renderer.sprite("default_pack_icon",right+4*scale,defaultY,32*scale,32*scale,0,0,128,128);renderer.text("Default",right+38*scale,defaultY+2*scale,scale,0xffffffff);renderer.text("The default look of Minecraft",right+38*scale,defaultY+13*scale,scale*.8f,0x808080ff);}
                if(!packNotice.empty())centerText(hud.metrics().trim(packNotice,390),h-68*scale,scale*.85f);
                if(button(tr("resourcePack.openFolder"),w*.5f-154*scale,h-48*scale,150*scale,bh))ShellExecuteW(window.handle(),L"open",resourcePacksRoot.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
                if(button(packJob.valid()?"正在加载…":tr("gui.done"),w*.5f+4*scale,h-48*scale,150*scale,bh,!packJob.valid())){
                    if(selectedPacks!=activePacks)loadPacks();else screen=Screen::Options;
                }
                centerText(tr("resourcePack.folderInfo"),h-26*scale,scale*.8f,0x808080ff);
            } else if(screen==Screen::Connecting){
                mc::serverui::ConnectionView view;view.address=serverText;view.phase=connectionJob.valid()?mc::serverui::ConnectionPhase::Connecting:game.joined?mc::serverui::ConnectionPhase::DownloadingTerrain:mc::serverui::ConnectionPhase::Authorizing;
                mc::hud::Hud::draw(renderer,mc::serverui::connection(hud,tr,int(std::ceil(w/scale)),int(std::ceil(h/scale)),view),scale);
                mc::serverui::Input input;input.mouseX=window.mouseX/scale;input.mouseY=window.mouseY/scale;input.click=window.pressed(VK_LBUTTON);input.escape=window.pressed(VK_ESCAPE);
                if(mc::serverui::connectionClicked(hud,int(std::ceil(w/scale)),int(std::ceil(h/scale)),view,input)){connectionCancelled=true;game.disconnect();screen=Screen::ServerList;}
            } else if(screen==Screen::ConfirmLink){
                centerText(tr("chat.link.confirm"),h*.5f-50*scale,scale);
                centerText(hud.metrics().trim(pendingUrl,280),h*.5f-30*scale,scale,0xffff55ff);
                if(button(tr("gui.yes"),w*.5f-155*scale,h*.5f+20*scale,150*scale,bh)){
                    ShellExecuteW(window.handle(),L"open",mc::wide(pendingUrl).c_str(),nullptr,nullptr,SW_SHOWNORMAL);screen=Screen::Chat;
                }
                if(button(tr("gui.no"),w*.5f+5*scale,h*.5f+20*scale,150*scale,bh))screen=Screen::Chat;
            } else if (screen == Screen::Disconnected) {
                mc::serverui::ConnectionView view;view.phase=mc::serverui::ConnectionPhase::Failed;
                view.message=notice.empty()?game.error:notice;
                if(notice.empty()||notice==game.error)view.messageJson=game.disconnectJson;
                mc::hud::Hud::draw(renderer,mc::serverui::connection(hud,tr,int(std::ceil(w/scale)),int(std::ceil(h/scale)),view),scale);
                mc::serverui::Input input;input.mouseX=window.mouseX/scale;input.mouseY=window.mouseY/scale;
                input.click=window.pressed(VK_LBUTTON);input.enter=window.pressed(VK_RETURN);input.escape=window.pressed(VK_ESCAPE);
                if(mc::serverui::connectionClicked(hud,int(std::ceil(w/scale)),int(std::ceil(h/scale)),view,input))screen=Screen::ServerList;
            }
        }
        if (screen == Screen::Inventory && inventoryScreen.draw(renderer, game, controller, window, tr, scale)) {
            controller.closeWindow(game);
            screen = Screen::Play;
            window.setCapture(true);
        }
        renderer.endUI();
        ++frames;
        ++fpsFrames;
        if (o.benchmark)
            glFinish();
        double frameMs = std::chrono::duration<double, std::milli>(Clock::now() - frameStart).count();
        totalFrameMs += frameMs;
        if (o.benchmark && renderer.stats().pendingMeshes == 0 && frames > 60) {
            benchmarkTimes.push_back(frameMs);
            measuredRendererMs += renderer.stats().frameMs;
            measuredMeshesMs += renderer.stats().meshUpdateMs;
            measuredDynamicMs += renderer.stats().dynamicPrepareMs;
        }
        double fpsSeconds = std::chrono::duration<double>(Clock::now() - fpsClock).count();
        if (fpsSeconds >= .5) {
            fps = float(fpsFrames / fpsSeconds);
            fpsFrames = 0;
            fpsClock = Clock::now();
        }
        bool lastFrame = o.frames > 0 && frames >= o.frames && renderer.stats().pendingMeshes == 0 &&
                         (!o.networkSkins || skins.pending() == 0 || elapsed > 20);
        if (lastFrame || pressed("screenshot")) {
            std::string path = o.screenshot;
            if (path.empty() && !lastFrame) {
                fs::create_directories("screenshots");
                path = "screenshots/frame-" + std::to_string(game.ticks) + ".ppm";
            }
            if (!path.empty()) {
                auto f = fs::u8path(path);
                if (f.has_parent_path())
                    fs::create_directories(f.parent_path());
                if (!renderer.screenshot(path, w, h, error))
                    std::cerr << error << '\n';
                else
                    std::cout << "[screenshot] " << path << '\n';
            }
        }
        window.swap();
        window.clearTransient();
        if (lastFrame)
            break;
        if (!window.focused() && !o.hidden)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        else if (!o.benchmark && !o.hidden && preferences.maxFps < 260) {
            auto remaining = std::chrono::duration<double>(1.0 / preferences.maxFps) - (Clock::now() - frameStart);
            if (remaining.count() > 0)
                std::this_thread::sleep_for(remaining);
        }
    }
    double seconds = std::chrono::duration<double>(Clock::now() - started).count();
    std::ostringstream report;
    report << "{\"gpu\":" << quoteJson(renderer.stats().gpu) << ",\"width\":" << window.width()
           << ",\"height\":" << window.height() << ",\"view_distance\":" << renderer.options.distance
           << ",\"shadows\":" << (renderer.options.shadows ? "true" : "false")
           << ",\"fxaa\":" << (renderer.options.fxaa ? "true" : "false")
           << ",\"vsync\":" << (preferences.vsync ? "true" : "false") << ",\"frames\":" << frames
           << ",\"seconds\":" << seconds << ",\"mean_frame_ms\":" << (frames ? totalFrameMs / frames : 0)
           << ",\"visible_triangles\":" << renderer.stats().triangles
           << ",\"loaded_sections\":" << renderer.stats().loadedSections
           << ",\"dynamic_draws\":" << renderer.stats().dynamicDraws
           << ",\"dynamic_vertices\":" << renderer.stats().dynamicVertices;
    if (!benchmarkTimes.empty()) {
        std::sort(benchmarkTimes.begin(), benchmarkTimes.end());
        double sum = 0;
        for (double ms : benchmarkTimes)
            sum += ms;
        report << ",\"benchmark_samples\":" << benchmarkTimes.size()
               << ",\"benchmark_mean_ms\":" << sum / benchmarkTimes.size()
               << ",\"benchmark_p50_ms\":" << benchmarkTimes[benchmarkTimes.size() / 2]
               << ",\"benchmark_p95_ms\":" << benchmarkTimes[std::size_t((benchmarkTimes.size() - 1) * .95)]
               << ",\"benchmark_fps\":" << 1000 * benchmarkTimes.size() / sum
               << ",\"renderer_submit_mean_ms\":" << measuredRendererMs / benchmarkTimes.size()
               << ",\"mesh_update_mean_ms\":" << measuredMeshesMs / benchmarkTimes.size()
               << ",\"dynamic_prepare_mean_ms\":" << measuredDynamicMs / benchmarkTimes.size();
    }
    report << ",\"network\":" << gameReport(game, seconds) << '}';
    writeReport(o.report, report.str());
    std::cout << report.str() << '\n';
    if (!o.saveWorld.empty()) {
        if (game.online)
            throw std::runtime_error("--save-world is only available for local worlds");
        if (!local.saveNew(game, o.saveWorld, error))
            throw std::runtime_error(error);
    } else if (!o.hidden && !o.benchmark && !o.frames && !saveSnapshot())
        std::cerr << "Save failed: " << notice << '\n';
    if (!o.hidden && !o.benchmark && !preferences.save(preferencesPath, &error))
        std::cerr << "Options save failed: " << error << '\n';
    game.disconnect();
    return 0;
}
} // namespace
int main() {
    try {
        SetConsoleOutputCP(CP_UTF8);
        int argc = 0;
        wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv)
            throw std::runtime_error("Cannot read command line");
        std::vector<std::string> args;
        for (int i = 0; i < argc; ++i)
            args.push_back(mc::utf8(argv[i]));
        LocalFree(argv);
        Options o = parseOptions(args);
        if (o.status) {
            auto status = mc::net::queryStatus(o.server, std::uint16_t(o.port));
            if (!status.ok) {
                std::cerr << status.error << '\n';
                return 2;
            }
            std::cout << status.json << "\nLatency: " << status.latencyMs << " ms\n";
            return 0;
        }
        if (o.headless)
            return headless(o);
        return graphical(o);
    } catch (const std::exception &e) {
        std::cerr << "NativeVoxel: " << e.what() << '\n';
        return 1;
    }
}
