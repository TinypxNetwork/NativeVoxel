#include "worldgen_fixture.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

using namespace mc;
using namespace mc::worldgen;
using namespace mc::worldgen::detail;
using namespace mc::worldgen::test;
int main(int argc,char** argv){
    try{
        if(argc!=3)throw std::runtime_error("worldgen_lake_tests requires fixture/output paths");
        std::ifstream in(argv[1]);std::ofstream out(argv[2]);if(!in||!out)throw std::runtime_error("cannot open lake fixture/output");std::string line;
        while(std::getline(in,line)){
            if(line.empty()||line[0]=='#')continue;std::istringstream row(line);std::int64_t seed;int pattern,ground,x,y,z,liquid,biome,sky;row>>seed>>pattern>>ground>>x>>y>>z>>liquid>>biome>>sky;
            World world;load(world,pattern,ground,16,16);JavaRandom random(seed);int skyQueries=0,freezeQueries=0;LakeHooks hooks;
            hooks.skyLight=[&](Position p){++skyQueries;return p.y>=256?15:sky;};
            hooks.biomeTop=[&](Position){return blockState(biome==14?110:2);};
            hooks.canFreezeWater=[&](Position p){++freezeQueries;const auto block=world.getBlock(p.x,p.y,p.z);return biome==12&&p.y>=0&&p.y<256&&(blockId(block)==8||blockId(block)==9)&&blockMeta(block)==0;};
            const bool result=generateLake(world,random,{x,y,z},liquid,hooks);
            out<<"R "<<int(result)<<' '<<random.nextLong()<<' '<<skyQueries<<' '<<freezeQueries<<'\n';differences(out,world,pattern,ground,16,16);out<<"END\n";
        }
        if(!out)throw std::runtime_error("failed writing lake result");return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
