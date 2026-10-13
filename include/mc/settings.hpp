#pragma once
#include <array>
#include <map>
#include <string>
#include <vector>
namespace mc {
struct KeyOption {
    std::string name,category;
    int key=0,defaultKey=0; // Win32 virtual-key code; zero is unbound.
};
class Settings {
public:
    Settings();
    float fov=70,mouseSensitivity=.5f,gamma=0;
    bool invertMouse=false,viewBobbing=true,vsync=false,shadows=false,fxaa=false;
    int renderDistance=12,guiScale=0,maxFps=260;
    bool fullscreen=false,forceUnicodeFont=false,blockAlternatives=true;
    int ambientOcclusion=2,mipmapLevels=4,particles=0;
    unsigned char skinParts=127;
    int chatVisibility=0;
    bool chatColors=true,chatLinks=true,chatLinksPrompt=true,reducedDebugInfo=false;
    float chatOpacity=1,chatScale=1,chatWidth=1,chatHeightFocused=1,chatHeightUnfocused=.44366196f;
    bool snooperEnabled=true,realmsNotifications=true;
    // Java's shader index is session state, not an options.txt preference.
    int secretShader=-1;
    float streamBytesPerPixel=.5f,streamMicVolume=1,streamSystemVolume=1,streamKbps=.5412844f,streamFps=.31690142f;
    int streamCompression=1,streamChatEnabled=0,streamChatUserFilter=0,streamMicToggleBehavior=0;
    bool streamSendMetadata=true;
    std::string language="zh_CN";
    std::array<float,9> soundLevels{{1,1,1,1,1,1,1,1,1}};
    std::vector<KeyOption> keys;
    int key(const std::string& name) const {for(const auto& k:keys)if(k.name==name)return k.key;return 0;}
    bool bind(const std::string& name,int virtualKey);
    void resetKeys();
    bool load(const std::string& path,std::string* error=nullptr);
    bool save(const std::string& path,std::string* error=nullptr) const;
    static int fromLegacyKey(int lwjglCode);
    static int toLegacyKey(int virtualKey);
    static std::string keyName(int virtualKey);
private:
    std::map<std::string,std::string> extra_;
};
}
