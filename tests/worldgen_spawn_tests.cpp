#include "mc/worldgen.hpp"
#include "mc/worldgen_biomes.hpp"
#include "mc/worldgen_noise.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

int main(int argc,char** argv){
    try{
        if(argc!=3)throw std::runtime_error("worldgen_spawn_tests requires fixture/output paths");
        std::ifstream in(argv[1]);std::ofstream out(argv[2]);if(!in||!out)throw std::runtime_error("cannot open spawn fixture/output");
        std::string line;
        while(std::getline(in,line)){
            if(line.empty()||line[0]=='#')continue;std::istringstream row(line);char kind;std::int64_t seed;int type;row>>kind>>seed>>type;
            if(kind=='B'){
                int x,z,range,count;std::int64_t randomSeed;row>>x>>z>>range>>randomSeed>>count;std::vector<int> allowed(count);for(auto& value:allowed)row>>value;
                mc::worldgen::BiomeLayers layers(seed,static_cast<mc::worldgen::WorldType>(type));mc::worldgen::JavaRandom random(randomSeed);
                const auto found=layers.findBiomePosition(x,z,range,allowed,random);out<<"B "<<int(found.found)<<' '<<found.x<<' '<<found.z<<' '<<random.nextLong()<<'\n';
            }else{
                int dimension,mode;row>>dimension>>mode;mc::worldgen::Settings settings;settings.dimension=static_cast<mc::worldgen::Dimension>(dimension);settings.worldType=static_cast<mc::worldgen::WorldType>(type);settings.mapFeatures=false;
                mc::worldgen::Generator generator(seed,settings);mc::World world;int calls=0;
                const auto spawn=generator.findSpawn([&](int x,int z){
                    ++calls;out<<"Q "<<x<<' '<<z<<'\n';
                    if(mode==0)return true;if(mode==1)return false;if(mode==2)return ((x*31+z*17)&63)==0;if(mode==3)return calls>=250;
                    if(!world.hasChunk(mc::floorDiv16(x),mc::floorDiv16(z))){std::string error;if(!generator.generateChunk(world,mc::floorDiv16(x),mc::floorDiv16(z),&error))throw std::runtime_error(error);}
                    int y=63;while(y<255&&mc::blockId(world.getBlock(x,y+1,z))!=0)++y;return mc::blockId(world.getBlock(x,y,z))==2;
                });
                out<<"S "<<spawn.x<<' '<<spawn.y<<' '<<spawn.z<<' '<<int(spawn.found)<<' '<<int(spawn.suitableGround)<<' '<<spawn.attempts<<'\n';
            }
        }
        if(!out)throw std::runtime_error("failed writing spawn result");return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
