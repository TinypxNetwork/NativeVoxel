#include "../src/worldgen_internal.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

int main(int argc,char** argv){
    try{
        if(argc!=3)throw std::runtime_error("surface_tests requires fixture/output");std::ifstream in(argv[1]);std::ofstream out(argv[2],std::ios::binary);if(!in||!out)throw std::runtime_error("cannot open surface fixture");
        std::unique_ptr<mc::worldgen::detail::SurfaceGenerator> surface;std::int64_t previous=0;std::string line;
        const auto write=[&](std::uint64_t value,int bytes){for(int i=bytes-1;i>=0;--i)out.put(static_cast<char>(value>>(i*8)));};
        while(std::getline(in,line)){
            if(line.empty()||line[0]=='#')continue;std::istringstream input(line);std::int64_t seed,randomSeed;int biome,x,z,height;double noise;input>>seed>>randomSeed>>biome>>x>>z>>height>>noise;
            if(!surface||previous!=seed){surface=std::make_unique<mc::worldgen::detail::SurfaceGenerator>(seed);previous=seed;}
            mc::worldgen::detail::Primer primer;const int bx=z&15,bz=x&15;
            for(int y=0;y<=height;++y)primer.set(bx,y,bz,mc::blockState(1));for(int y=height+1;y<63;++y)primer.set(bx,y,bz,mc::blockState(9));
            mc::worldgen::JavaRandom random(randomSeed);surface->column(primer,random,biome,x,z,noise);
            for(int y=0;y<256;++y)write(primer.get(bx,y,bz),2);write(static_cast<std::uint64_t>(random.nextLong()),8);
            const float temperature=surface->temperature(biome,x,height,z);std::uint32_t bits;std::memcpy(&bits,&temperature,4);write(bits,4);
        }
        if(!out)throw std::runtime_error("cannot finish surface output");return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
