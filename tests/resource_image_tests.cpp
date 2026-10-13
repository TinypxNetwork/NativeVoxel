#include "mc/resource_packs.hpp"
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv){
    try{
        const std::string directory=argc>1?argv[1]:"assets";std::vector<std::string> resources={"environment/sun","environment/moon_phases","particle/particles","misc/enchanted_item_glint"};
        for(int i=0;i<10;++i)resources.push_back("blocks/destroy_stage_"+std::to_string(i));std::string error;mc::ResourceImage image;
        for(const auto& resource:resources){if(!mc::readResourceImage(directory,"assets/minecraft/textures/"+resource+".png",image,error))throw std::runtime_error(error);
            if(!image.width||!image.height||image.pixels.size()!=std::size_t(image.width)*image.height*4)throw std::runtime_error("invalid decoded RGBA dimensions");}
        const auto before=image.pixels;const unsigned width=image.width,height=image.height;
        if(mc::readResourceImage(directory,"../outside.png",image,error)||error.empty()||image.pixels!=before||image.width!=width||image.height!=height)throw std::runtime_error("unsafe resource path changed output");
        if(mc::decodeResourcePng({0,1,2},image,error)||image.pixels!=before)throw std::runtime_error("bad PNG changed output");
        std::cout<<"PASS: 14 vanilla renderer PNGs decoded through native WIC; unsafe paths and invalid images preserve output\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
