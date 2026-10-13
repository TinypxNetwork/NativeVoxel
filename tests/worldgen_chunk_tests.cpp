#include "mc/worldgen.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

int main(int argc,char** argv){
    try{
        if(argc!=3)throw std::runtime_error("worldgen_chunk_tests needs fixture/output paths");
        std::ifstream input(argv[1]);std::ofstream out(argv[2],std::ios::binary);if(!input||!out)throw std::runtime_error("cannot open chunk fixture/output");
        std::unique_ptr<mc::worldgen::Generator> generator;std::int64_t previous=0;int previousType=-1,previousStage=-1,previousDimension=99;
        mc::World world;std::string line,error;
        while(std::getline(input,line)){
            if(line.empty()||line[0]=='#')continue;std::istringstream in(line);std::int64_t seed;int type,stage,x,z;in>>seed>>type>>stage>>x>>z;
            int dimension=0;in>>dimension;
            if(!generator||seed!=previous||type!=previousType||stage!=previousStage||dimension!=previousDimension){mc::worldgen::Settings settings;settings.worldType=static_cast<mc::worldgen::WorldType>(type);settings.stage=static_cast<mc::worldgen::ChunkStage>(stage);settings.dimension=static_cast<mc::worldgen::Dimension>(dimension);settings.mapFeatures=false;generator=std::make_unique<mc::worldgen::Generator>(seed,settings);previous=seed;previousType=type;previousStage=stage;previousDimension=dimension;}
            world.clear();if(!generator->generateChunk(world,x,z,&error))throw std::runtime_error(error);
            for(int bx=0;bx<16;++bx)for(int bz=0;bz<16;++bz)for(int y=0;y<256;++y){const auto state=world.getBlock(x*16+bx,y,z*16+bz);out.put(static_cast<char>(state>>8));out.put(static_cast<char>(state));}
            for(int bx=0;bx<16;++bx)for(int bz=0;bz<16;++bz)for(int y=0;y<256;++y)out.put(static_cast<char>((world.getSkyLight(x*16+bx,y,z*16+bz)<<4)|world.getBlockLight(x*16+bx,y,z*16+bz)));
            for(int bz=0;bz<16;++bz)for(int bx=0;bx<16;++bx)out.put(static_cast<char>(world.getBiome(x*16+bx,z*16+bz)));
        }
        if(!out)throw std::runtime_error("failed writing chunk fixture");return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
