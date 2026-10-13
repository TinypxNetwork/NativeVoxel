#include "worldgen_fixture.hpp"
#include "worldgen_nbt_fixture.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <tuple>

using namespace mc;
using namespace mc::worldgen;
using namespace mc::worldgen::detail;
using namespace mc::worldgen::test;
int main(int argc,char** argv){
    try{
        if(argc!=3)throw std::runtime_error("worldgen_dungeon_tests requires fixture/output paths");
        std::ifstream in(argv[1]);std::ofstream out(argv[2]);if(!in||!out)throw std::runtime_error("cannot open dungeon fixture/output");std::string line;
        while(std::getline(in,line)){
            if(line.empty()||line[0]=='#')continue;std::istringstream row(line);std::int64_t seed;int pattern,y,repeats;row>>seed>>pattern>>y>>repeats;
            World world;load(world,pattern,y,0,0);JavaRandom random(seed);PopulationOutput output;std::map<std::tuple<int,int,int>,nbt::Tag> tiles;
            if(pattern==26){nbt::Tag initial;initial.type=10;initial.compound["id"]=nbt::text("Chest");initial.compound["Lock"]=nbt::text("");initial.compound["x"]=nbt::integer(3,0);initial.compound["y"]=nbt::integer(3,y);initial.compound["z"]=nbt::integer(3,-1);nbt::Tag items;items.type=9;initial.compound["Items"]=items;tiles[{0,-1,y}]=initial;}
            out<<"R";for(int i=0;i<repeats;++i)out<<' '<<int(generateDungeon(world,random,{0,y,0},output));out<<' '<<random.nextLong()<<'\n';
            for(const auto& update:output.blockEntities){const auto key=std::make_tuple(update.x,update.z,update.y);if(update.data.type)tiles[key]=update.data;else tiles.erase(key);}
            for(const auto& pair:tiles){out<<"T ";printTag(out,pair.second);out<<'\n';}
            differences(out,world,pattern,y,0,0);out<<"END\n";
        }
        if(!out)throw std::runtime_error("failed writing dungeon result");return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
