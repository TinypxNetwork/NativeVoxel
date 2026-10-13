#include "mc/worldgen_loot.hpp"
#include "worldgen_nbt_fixture.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc,char** argv){
    try{
        if(argc!=3)throw std::runtime_error("worldgen_loot_tests requires fixture/output paths");
        std::ifstream in(argv[1]);std::ofstream out(argv[2]);if(!in||!out)throw std::runtime_error("cannot open loot fixture/output");std::string line;
        while(std::getline(in,line)){
            if(line.empty()||line[0]=='#')continue;std::istringstream row(line);char kind;std::int64_t seed;row>>kind>>seed;mc::worldgen::JavaRandom random(seed);mc::nbt::Tag result;
            if(kind=='B'){int power;row>>power;result=mc::worldgen::makeEnchantedBook(random,power);}
            else{int x,y,z;row>>x>>y>>z;result=kind=='C'?mc::worldgen::makeDungeonChest(random,x,y,z):mc::worldgen::makeDungeonSpawner(random,x,y,z);}
            mc::worldgen::test::printTag(out,result);out<<' '<<random.nextLong()<<'\n';
        }
        if(!out)throw std::runtime_error("failed writing loot result");return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
