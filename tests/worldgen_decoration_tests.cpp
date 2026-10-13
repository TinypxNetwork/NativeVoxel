#include "mc/worldgen_decoration.hpp"
#include "worldgen_fixture.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

using namespace mc;
using namespace mc::worldgen;
using namespace mc::worldgen::test;
int main(int argc,char** argv){
    try{
        if(argc!=3)throw std::runtime_error("worldgen_decoration_tests requires fixture/output paths");
        std::ifstream in(argv[1]);std::ofstream out(argv[2]);if(!in||!out)throw std::runtime_error("cannot open decoration fixture/output");std::string line,error;
        while(std::getline(in,line)){
            if(line.empty()||line[0]=='#')continue;std::istringstream row(line);std::int64_t seed;int biome,pattern,cx,cz,springs;row>>biome>>seed>>pattern>>cx>>cz>>springs;
            const int x=cx*16,z=cz*16;World world;load(world,pattern,63,x+16,z+16);JavaRandom random(seed);PopulationOutput output;
            struct Tick {int x,y,z;BlockState state;std::uint64_t seed;};std::vector<Tick> ticks;
            DecorationHooks hooks;hooks.noSky=biome==8||biome==9;hooks.springs=springs!=0;hooks.light=[](int x,int,int z){return 13-((x+z)&1);};
            hooks.forceBlockTick=[&](int x,int y,int z,BlockState block,JavaRandom& rng){ticks.push_back({x,y,z,block,rng.internalSeed()});};
            if(!decorateBiome(world,biome,cx,cz,random,output,hooks,&error))throw std::runtime_error(error);
            out<<"R "<<random.nextLong()<<'\n';
            for(const auto& e:output.entities)out<<"E "<<e.id<<' '<<bits(e.x)<<' '<<bits(e.y)<<' '<<bits(e.z)<<' '<<bits(e.yaw)<<' '<<bits(e.pitch)<<'\n';
            for(const auto& tick:ticks)out<<"F "<<tick.x<<' '<<tick.y<<' '<<tick.z<<' '<<tick.state<<' '<<tick.seed<<'\n';
            differences(out,world,pattern,63,x+16,z+16);out<<"END\n";
        }
        if(!out)throw std::runtime_error("failed writing decoration result");return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
