#pragma once
#include "mc/gui_hud.hpp"
#include "mc/nbt.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace mc { struct ResourceImage; }
namespace mc::serverui {

enum class PingState { Unqueried,Pinging,Ready,Failed };
enum class ResourceMode { Prompt,Enabled,Disabled };
struct Server {
    std::string name,address,motdJson,motd,population,versionName{"1.8.9"},players,iconBase64,iconTexture;
    int protocol{47},pingMilliseconds{-1},onlinePlayers{},maximumPlayers{};
    PingState state{PingState::Unqueried};
    ResourceMode resourceMode{ResourceMode::Prompt};
    bool lan{};
};
struct Input {
    float mouseX{},mouseY{},wheel{};
    bool click{},enter{},escape{},up{},down{},shift{},refresh{},deleteKey{};
    std::uint64_t milliseconds{};
};
struct Action {
    enum class Kind { None,Select,Join,DirectConnect,Add,Edit,Delete,Refresh,Cancel,Reordered } kind{};
    int index{-1};
};
struct Button {int id{};hud::Rect bounds;std::string label;bool enabled{true};};
struct BrowserLayout {
    hud::Layout drawing;
    std::vector<Button> buttons;
    std::vector<hud::Rect> rows;
    std::string tooltip;
    hud::Rect listArea;
};
class ServerList {
public:
    std::vector<Server> servers;
    int selected{-1};
    float scroll{};
    bool read(const std::string& serversDat,std::string& error);
    bool write(const std::string& serversDat,std::string& error) const;
    nbt::Tag toNbt() const;
    bool fromNbt(const nbt::Tag& root,std::string& error);
    void beginPing(std::size_t index);
    bool applyStatus(std::size_t index,const std::string& statusJson,int pingMilliseconds,std::string& error);
    void failPing(std::size_t index,const std::string& message);
    std::vector<std::size_t> unqueried() const;
    bool decodeIcon(std::size_t index,ResourceImage& image,std::string& error) const;
    bool installIcon(std::size_t index,Renderer& renderer,const std::string& cacheDirectory,std::string& error);
    static bool registerTextures(Renderer& renderer,const std::string& assets);
    BrowserLayout layout(const hud::Hud& hud,const I18n& language,int width,int height,const Input& input) const;
    Action interact(const BrowserLayout& layout,const Input& input);
private:
    int lastClicked_{-1};
    std::uint64_t lastClickAt_{};
};

enum class ConnectionPhase { Resolving,Connecting,Authorizing,DownloadingTerrain,Failed,Cancelled };
struct ConnectionView {ConnectionPhase phase{ConnectionPhase::Connecting};std::string address,message,messageJson;bool cancellable{true};};
// All network work belongs to the caller. These are real, callable layouts and
// draw helpers; they never read credentials or initiate a connection themselves.
hud::Layout connection(const hud::Hud& hud,const I18n& language,int width,int height,const ConnectionView& state);
hud::Rect connectionButton(int width,int height);
hud::Rect connectionButton(const hud::Hud& hud,int width,int height,const ConnectionView& state);
bool connectionClicked(int width,int height,const ConnectionView& state,const Input& input);
bool connectionClicked(const hud::Hud& hud,int width,int height,const ConnectionView& state,const Input& input);
void draw(Renderer& renderer,const BrowserLayout& layout,float pixelScale=1);

} // namespace mc::serverui
