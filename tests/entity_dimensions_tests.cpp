#include "mc/entity_dimensions.hpp"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::uint32_t bits(float value){std::uint32_t result;std::memcpy(&result,&value,4);return result;}
void dump(int kind,int type){const auto d=mc::entityDimensions(kind,type);if(d.valid)std::cout<<kind<<' '<<type<<' '<<bits(d.width)<<' '<<bits(d.height)<<' '<<bits(d.collisionBorder)<<' '<<d.collidable<<' '<<d.customBounds<<' '<<d.multipart<<'\n';}
}
int main(int argc,char** argv) {
    try {
        if(argc==2&&std::string(argv[1])=="--dump"){
            dump(0,0);for(int i=0;i<256;++i)dump(1,i);for(int i=0;i<128;++i)dump(2,i);return 0;
        }
        const auto player=mc::entityDimensions(0,0);
        check(player.valid&&player.collidable&&player.width==.6f&&player.height==1.8f&&player.collisionBorder==.1f,"player hitbox");
        check(mc::entityDimensions(1,54).height==1.95f,"zombie height differs from player");
        check(mc::entityDimensions(1,53).width==.6f*6,"giant preserves original float multiplication");
        check(mc::entityDimensions(1,55).width==.51000005f,"slime size-one metadata base");
        check(mc::entityDimensions(2,70).width==.98f&&mc::entityDimensions(2,70).height==.98f,"falling block positional spawn constructor");
        check(mc::entityDimensions(2,63).collisionBorder==1&&mc::entityDimensions(2,63).collidable,"deflectable fireball target border");
        check(!mc::entityDimensions(2,60).collidable&&!mc::entityDimensions(2,2).collidable,"arrows/items are not vanilla pick targets");
        check(mc::entityDimensions(2,71).customBounds&&mc::entityDimensions(2,71).collisionBorder==0,"item frame requires oriented bounds");
        check(mc::entityDimensions(1,63).multipart&&!mc::entityDimensions(1,63).collidable,"dragon parent is not its hittable child parts");
        check(mc::entityDimensions(2,78).height==1.975f&&!mc::entityDimensions(1,255).valid,"armor stand and unknown types");
        unsigned count=1;for(int i=0;i<256;++i)count+=mc::entityDimensions(1,i).valid;for(int i=0;i<128;++i)count+=mc::entityDimensions(2,i).valid;
        check(count==55,"all supported vanilla protocol entity kinds");
        std::cout<<"entity dimensions tests passed (55 original spawn/getter records)\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
