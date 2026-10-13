#include "mc/settings.hpp"
#include "mc/platform.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace mc {
namespace {
const char *categories[] = {"master", "music", "record", "weather", "block", "hostile", "neutral", "player", "ambient"};
const char *modelParts[] = {"cape", "jacket", "left_sleeve", "right_sleeve", "left_pants_leg", "right_pants_leg", "hat"};
float number(const std::string &value) {
    if (value == "true")
        return 1;
    if (value == "false")
        return 0;
    std::size_t used = 0;
    float n = std::stof(value, &used);
    if (used != value.size() || !std::isfinite(n))
        throw std::invalid_argument("Invalid finite option");
    return n;
}
std::string numeric(float value) {
    std::ostringstream out;
    out << std::setprecision(9) << value;
    return out.str();
}
} // namespace
Settings::Settings() {
    auto add = [&](const char *name, const char *category, int key) { keys.push_back({name, category, key, key}); };
    for (auto pair : std::vector<std::pair<const char *, int>>{{"forward", 'W'},
                                                               {"left", 'A'},
                                                               {"back", 'S'},
                                                               {"right", 'D'},
                                                               {"jump", VK_SPACE},
                                                               {"sneak", VK_LSHIFT},
                                                               {"sprint", VK_LCONTROL}})
        add(pair.first, "movement", pair.second);
    add("attack", "gameplay", VK_LBUTTON);
    add("use", "gameplay", VK_RBUTTON);
    add("pickItem", "gameplay", VK_MBUTTON);
    add("inventory", "inventory", 'E');
    add("drop", "inventory", 'Q');
    for (int i = 0; i < 9; ++i)
        keys.push_back({"hotbar." + std::to_string(i + 1), "inventory", '1' + i, '1' + i});
    add("chat", "multiplayer", 'T');
    add("playerlist", "multiplayer", VK_TAB);
    add("command", "multiplayer", VK_OEM_2);
    add("screenshot", "misc", VK_F2);
    add("togglePerspective", "misc", VK_F5);
    add("fullscreen", "misc", VK_F11);
}
bool Settings::bind(const std::string &name, int code) {
    if (code < 0 || code > 255)
        return false;
    for (auto &k : keys)
        if (k.name == name) {
            k.key = code;
            return true;
        }
    return false;
}
void Settings::resetKeys() {
    for (auto &k : keys)
        k.key = k.defaultKey;
}
int Settings::fromLegacyKey(int code) {
    if (code == -100)
        return VK_LBUTTON;
    if (code == -99)
        return VK_RBUTTON;
    if (code == -98)
        return VK_MBUTTON;
    if (code == -97)
        return VK_XBUTTON1;
    if (code == -96)
        return VK_XBUTTON2;
    if (code <= 0 || code > 255)
        return 0;
    if (code == 42)
        return VK_LSHIFT;
    if (code == 54)
        return VK_RSHIFT;
    if (code == 29)
        return VK_LCONTROL;
    if (code == 157)
        return VK_RCONTROL;
    if (code == 56)
        return VK_LMENU;
    if (code == 184)
        return VK_RMENU;
    if (code == 87)
        return VK_F11;
    if (code == 88)
        return VK_F12;
    UINT scan = UINT(code & 127);
    if (code & 128)
        scan |= 0xe000;
    return int(MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX));
}
int Settings::toLegacyKey(int code) {
    if (code == VK_LBUTTON)
        return -100;
    if (code == VK_RBUTTON)
        return -99;
    if (code == VK_MBUTTON)
        return -98;
    if (code == VK_XBUTTON1)
        return -97;
    if (code == VK_XBUTTON2)
        return -96;
    if (code == VK_SHIFT || code == VK_LSHIFT)
        return 42;
    if (code == VK_RSHIFT)
        return 54;
    if (code == VK_CONTROL || code == VK_LCONTROL)
        return 29;
    if (code == VK_RCONTROL)
        return 157;
    if (code == VK_MENU || code == VK_LMENU)
        return 56;
    if (code == VK_RMENU)
        return 184;
    if (code == VK_F11)
        return 87;
    if (code == VK_F12)
        return 88;
    if (code <= 0 || code > 255)
        return 0;
    UINT scan = MapVirtualKeyW(UINT(code), MAPVK_VK_TO_VSC_EX);
    return int((scan & 127) | ((scan & 0xff00) ? 128 : 0));
}
std::string Settings::keyName(int code) {
    if (code == 0)
        return "NONE";
    if (code == VK_LBUTTON)
        return "Mouse 1";
    if (code == VK_RBUTTON)
        return "Mouse 2";
    if (code == VK_MBUTTON)
        return "Mouse 3";
    if (code == VK_XBUTTON1)
        return "Mouse 4";
    if (code == VK_XBUTTON2)
        return "Mouse 5";
    UINT scan = MapVirtualKeyW(UINT(code), MAPVK_VK_TO_VSC_EX);
    LONG bits = LONG((scan & 255) << 16);
    if (scan & 0xff00)
        bits |= 1 << 24;
    wchar_t name[96]{};
    if (GetKeyNameTextW(bits, name, 96))
        return utf8(name);
    return std::to_string(code);
}
bool Settings::load(const std::string &path, std::string *error) {
    if (error)
        error->clear();
    std::ifstream in(std::filesystem::u8path(path));
    if (!in)
        return true;
    Settings candidate = *this;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        auto colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        auto name = line.substr(0, colon), value = line.substr(colon + 1);
        candidate.extra_[name] = value;
        try {
            if (name == "fov")
                candidate.fov = std::clamp(number(value) * 40 + 70, 30.f, 110.f);
            else if (name == "mouseSensitivity")
                candidate.mouseSensitivity = std::clamp(number(value), 0.f, 1.f);
            else if (name == "gamma")
                candidate.gamma = std::clamp(number(value), 0.f, 1.f);
            else if (name == "invertYMouse")
                candidate.invertMouse = value == "true";
            else if (name == "bobView")
                candidate.viewBobbing = value == "true";
            else if (name == "enableVsync")
                candidate.vsync = value == "true";
            else if (name == "native.shadows")
                candidate.shadows = value == "true";
            else if (name == "native.fxaa")
                candidate.fxaa = value == "true";
            else if (name == "renderDistance")
                candidate.renderDistance = std::clamp(int(number(value)), 2, 32);
            else if (name == "guiScale")
                candidate.guiScale = std::clamp(int(number(value)), 0, 8);
            else if (name == "maxFps")
                candidate.maxFps = std::clamp(int(number(value)), 10, 260);
            else if (name == "fullscreen") candidate.fullscreen = value == "true";
            else if (name == "forceUnicodeFont") candidate.forceUnicodeFont = value == "true";
            else if (name == "allowBlockAlternatives") candidate.blockAlternatives = value == "true";
            else if (name == "ao") candidate.ambientOcclusion = std::clamp(int(number(value)), 0, 2);
            else if (name == "mipmapLevels") candidate.mipmapLevels = std::clamp(int(number(value)), 0, 4);
            else if (name == "particles") candidate.particles = std::clamp(int(number(value)), 0, 2);
            else if (name == "chatVisibility") candidate.chatVisibility = std::clamp(int(number(value)), 0, 2);
            else if (name == "chatColors") candidate.chatColors = value == "true";
            else if (name == "chatLinks") candidate.chatLinks = value == "true";
            else if (name == "chatLinksPrompt") candidate.chatLinksPrompt = value == "true";
            else if (name == "reducedDebugInfo") candidate.reducedDebugInfo = value == "true";
            else if (name == "chatOpacity") candidate.chatOpacity = std::clamp(number(value), 0.f, 1.f);
            else if (name == "chatScale") candidate.chatScale = std::clamp(number(value), 0.f, 1.f);
            else if (name == "chatWidth") candidate.chatWidth = std::clamp(number(value), 0.f, 1.f);
            else if (name == "chatHeightFocused") candidate.chatHeightFocused = std::clamp(number(value), 0.f, 1.f);
            else if (name == "chatHeightUnfocused") candidate.chatHeightUnfocused = std::clamp(number(value), 0.f, 1.f);
            else if (name == "snooperEnabled") candidate.snooperEnabled = value == "true";
            else if (name == "realmsNotifications") candidate.realmsNotifications = value == "true";
            else if (name == "streamBytesPerPixel") candidate.streamBytesPerPixel = std::clamp(number(value), 0.f, 1.f);
            else if (name == "streamMicVolume") candidate.streamMicVolume = std::clamp(number(value), 0.f, 1.f);
            else if (name == "streamSystemVolume") candidate.streamSystemVolume = std::clamp(number(value), 0.f, 1.f);
            else if (name == "streamKbps") candidate.streamKbps = std::clamp(number(value), 0.f, 1.f);
            else if (name == "streamFps") candidate.streamFps = std::clamp(number(value), 0.f, 1.f);
            else if (name == "streamCompression") candidate.streamCompression = std::clamp(int(number(value)), 0, 2);
            else if (name == "streamChatEnabled") candidate.streamChatEnabled = std::clamp(int(number(value)), 0, 2);
            else if (name == "streamChatUserFilter") candidate.streamChatUserFilter = std::clamp(int(number(value)), 0, 2);
            else if (name == "streamMicToggleBehavior") candidate.streamMicToggleBehavior = std::clamp(int(number(value)), 0, 1);
            else if (name == "streamSendMetadata") candidate.streamSendMetadata = value == "true";
            else if (name.rfind("modelPart_", 0) == 0) {
                for (unsigned i=0;i<7;++i) if(name == std::string("modelPart_") + modelParts[i]) {
                    if(value == "true") candidate.skinParts |= 1u<<i;
                    else candidate.skinParts &= ~(1u<<i);
                }
            }
            else if (name == "lang" && !value.empty() && value.size() < 32)
                candidate.language = value;
            else if (name.rfind("key_key.", 0) == 0)
                candidate.bind(name.substr(8), fromLegacyKey(std::stoi(value)));
            else
                for (std::size_t i = 0; i < 9; ++i)
                    if (name == std::string("soundCategory_") + categories[i])
                        candidate.soundLevels[i] = std::clamp(number(value), 0.f, 1.f);
        } catch (const std::exception &) { /* Java skips malformed individual settings too. */
        }
    }
    if (in.bad()) {
        if (error)
            *error = "Could not read options.txt";
        return false;
    }
    if(!candidate.extra_.count("native.fidelityVersion") && candidate.extra_.count("native.shadows")) {
        candidate.shadows=false;candidate.fxaa=false;candidate.vsync=false;
        if(candidate.maxFps==120)candidate.maxFps=260;
    }
    *this = std::move(candidate);
    return true;
}
bool Settings::save(const std::string &path, std::string *error) const {
    try {
        auto target = std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal();
        auto temp = target;
        temp += ".tmp";
        std::filesystem::create_directories(target.parent_path());
        auto values = extra_;
        values["native.fidelityVersion"] = "2";
        values["fov"] = numeric((fov - 70) / 40);
        values["mouseSensitivity"] = numeric(mouseSensitivity);
        values["gamma"] = numeric(gamma);
        values["invertYMouse"] = invertMouse ? "true" : "false";
        values["bobView"] = viewBobbing ? "true" : "false";
        values["enableVsync"] = vsync ? "true" : "false";
        values["native.shadows"] = shadows ? "true" : "false";
        values["native.fxaa"] = fxaa ? "true" : "false";
        values["renderDistance"] = std::to_string(renderDistance);
        values["guiScale"] = std::to_string(guiScale);
        values["maxFps"] = std::to_string(maxFps);
        auto flag = [&](const char* key,bool value){values[key]=value?"true":"false";};
        flag("fullscreen",fullscreen); flag("forceUnicodeFont",forceUnicodeFont);
        flag("allowBlockAlternatives",blockAlternatives);
        values["ao"]=std::to_string(ambientOcclusion); values["mipmapLevels"]=std::to_string(mipmapLevels);
        values["particles"]=std::to_string(particles); values["chatVisibility"]=std::to_string(chatVisibility);
        flag("chatColors",chatColors); flag("chatLinks",chatLinks); flag("chatLinksPrompt",chatLinksPrompt);
        flag("reducedDebugInfo",reducedDebugInfo); flag("snooperEnabled",snooperEnabled);
        flag("realmsNotifications",realmsNotifications);
        values["chatOpacity"]=numeric(chatOpacity); values["chatScale"]=numeric(chatScale);
        values["chatWidth"]=numeric(chatWidth); values["chatHeightFocused"]=numeric(chatHeightFocused);
        values["chatHeightUnfocused"]=numeric(chatHeightUnfocused);
        for(unsigned i=0;i<7;++i) values[std::string("modelPart_")+modelParts[i]]=(skinParts&(1u<<i))?"true":"false";
        values["streamBytesPerPixel"]=numeric(streamBytesPerPixel); values["streamMicVolume"]=numeric(streamMicVolume);
        values["streamSystemVolume"]=numeric(streamSystemVolume); values["streamKbps"]=numeric(streamKbps);
        values["streamFps"]=numeric(streamFps); values["streamCompression"]=std::to_string(streamCompression);
        values["streamChatEnabled"]=std::to_string(streamChatEnabled); values["streamChatUserFilter"]=std::to_string(streamChatUserFilter);
        values["streamMicToggleBehavior"]=std::to_string(streamMicToggleBehavior); flag("streamSendMetadata",streamSendMetadata);
        values["lang"] = language;
        for (const auto &k : keys)
            values["key_key." + k.name] = std::to_string(toLegacyKey(k.key));
        for (std::size_t i = 0; i < 9; ++i)
            values[std::string("soundCategory_") + categories[i]] = numeric(soundLevels[i]);
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out)
                throw std::runtime_error("Cannot create options temporary file");
            for (const auto &pair : values)
                out << pair.first << ':' << pair.second << '\n';
            out.close();
            if (!out)
                throw std::runtime_error("Cannot write options file");
        }
        if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace options file");
        if (error)
            error->clear();
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = e.what();
        return false;
    }
}
} // namespace mc
