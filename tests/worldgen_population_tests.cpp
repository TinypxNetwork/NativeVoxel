#include "mc/worldgen.hpp"
#include "mc/worldgen_noise.hpp"
#include "../src/worldgen_features.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

static void integer(std::ostream& out,std::uint64_t value,int bytes){for(int i=bytes-1;i>=0;--i)out.put(static_cast<char>(value>>(i*8)));}
static void real(std::ostream& out,double value){std::uint64_t bits;std::memcpy(&bits,&value,8);integer(out,bits,8);}
static void real(std::ostream& out,float value){std::uint32_t bits;std::memcpy(&bits,&value,4);integer(out,bits,4);}
int main(int argc,char** argv){
    try{
        if(argc!=3)throw std::runtime_error("worldgen_population_tests requires fixture/output paths");
        std::ifstream input(argv[1]);std::ofstream out(argv[2],std::ios::binary);if(!input||!out)throw std::runtime_error("cannot open population fixture/output");
        std::string line,error;
        while(std::getline(input,line)){
            if(line.empty()||line[0]=='#')continue;std::istringstream row(line);std::int64_t seed,randomSeed;int type,dimension,x,z;row>>seed>>type>>dimension>>x>>z>>randomSeed;
            mc::worldgen::Settings settings;settings.worldType=static_cast<mc::worldgen::WorldType>(type);settings.dimension=static_cast<mc::worldgen::Dimension>(dimension);settings.mapFeatures=false;
            mc::worldgen::Generator generator(seed,settings);mc::World world;
            for(int cx=x;cx<=x+1;++cx)for(int cz=z;cz<=z+1;++cz)if(!generator.generateChunk(world,cx,cz,&error))throw std::runtime_error(error);
            mc::worldgen::JavaRandom random(randomSeed);mc::worldgen::PopulationOutput output;
            if(dimension==1){if(!generator.populate(world,x,z,random,output,&error))throw std::runtime_error(error);}
            else mc::worldgen::detail::generateDefaultOres(world,random,{x*16,0,z*16});
            if(world.chunks().size()!=4)throw std::runtime_error("population unexpectedly touched outside supplied columns");
            for(int cx=x;cx<=x+1;++cx)for(int cz=z;cz<=z+1;++cz)for(int bx=0;bx<16;++bx)for(int bz=0;bz<16;++bz)for(int y=0;y<256;++y)integer(out,world.getBlock(cx*16+bx,y,cz*16+bz),2);
            integer(out,output.entities.size(),4);
            for(const auto& entity:output.entities){integer(out,entity.id.size(),2);out.write(entity.id.data(),static_cast<std::streamsize>(entity.id.size()));real(out,entity.x);real(out,entity.y);real(out,entity.z);real(out,entity.yaw);real(out,entity.pitch);}
            integer(out,static_cast<std::uint64_t>(random.nextLong()),8);
        }
        if(!out)throw std::runtime_error("failed writing population result");return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
