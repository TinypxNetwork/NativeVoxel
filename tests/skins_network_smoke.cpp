#include "mc/skins.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

// Optional live test. The supplied value must still pass SkinCache's exact
// Mojang-host check. No account token or login session is read by this program.
int main(int argc,char** argv) {
    if(argc!=3){std::cerr<<"Usage: skins_network_smoke <assets> <base64-texture-property.txt>\n";return 2;}
    std::ifstream input(argv[2],std::ios::binary);std::string property((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    mc::SkinCache cache;std::string error;const std::string uuid="00000000-0000-0000-0000-000000000000";
    if(!input||!cache.initialize(argv[1],error)||!cache.request(uuid,property)){std::cerr<<"Cannot initialize live skin test: "<<error<<' '<<cache.lastError(uuid)<<'\n';return 1;}
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(cache.pending()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto skin=cache.get(uuid);if(cache.pending()||!skin||!skin->downloaded){std::cerr<<"Live skin test failed: "<<cache.lastError(uuid)<<'\n';return 1;}
    std::cout<<"Native WinHTTP/WIC skin loaded: "<<skin->image.width<<'x'<<skin->image.height<<", RGBA bytes="<<skin->image.pixels.size()<<", slim="<<skin->slim<<'\n';return 0;
}
