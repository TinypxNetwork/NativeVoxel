#include "mc/worldgen_biomes.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

int main(int argc,char** argv){
    try {
        if(argc!=3)throw std::runtime_error("worldgen_biome_tests requires fixture and output paths");
        std::ifstream input(argv[1]);std::ofstream out(argv[2],std::ios::binary);
        if(!input||!out)throw std::runtime_error("cannot open biome fixture/output");
        std::unique_ptr<mc::worldgen::BiomeLayers> layers;std::int64_t previous=0;int previousType=-1;std::string line;
        while(std::getline(input,line)){
            if(line.empty()||line[0]=='#')continue;std::istringstream in(line);std::int64_t seed;int type,level,x,z,width,height;in>>seed>>type>>level>>x>>z>>width>>height;
            if(!layers||previous!=seed||previousType!=type){layers=std::make_unique<mc::worldgen::BiomeLayers>(seed,static_cast<mc::worldgen::WorldType>(type));previous=seed;previousType=type;}
            const auto data=level==0?layers->coarse(x,z,width,height):layers->blocks(x,z,width,height);
            for(int value:data)for(int shift=24;shift>=0;shift-=8)out.put(static_cast<char>(static_cast<unsigned>(value)>>shift));
        }
        if(!out)throw std::runtime_error("failed writing biome output");return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
