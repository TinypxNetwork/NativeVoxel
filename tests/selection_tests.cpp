#include "mc/world.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("selection_tests requires a fixture path");
        std::ifstream input(argv[1]);if(!input)throw std::runtime_error("cannot open selection fixture");
        mc::World world;std::string line;std::cout<<std::setprecision(17);
        while(std::getline(input,line)) {
            if(line.empty()||line[0]=='#')continue;
            std::istringstream in(line);char command;in>>command;
            if(command=='W')world.clear();
            else if(command=='B'){int x,y,z,state;in>>x>>y>>z>>state;world.setBlock(x,y,z,static_cast<mc::BlockState>(state));}
            else if(command=='Q'){int x,y,z;in>>x>>y>>z;const auto b=world.selectionBounds(x,y,z);std::cout<<"Q "<<b.minX<<' '<<b.minY<<' '<<b.minZ<<' '<<b.maxX<<' '<<b.maxY<<' '<<b.maxZ<<'\n';}
            else if(command=='R'){
                double sx,sy,sz,ex,ey,ez;bool liquid,ignore,miss;in>>sx>>sy>>sz>>ex>>ey>>ez>>liquid>>ignore>>miss;
                const auto hit=world.raycast(sx,sy,sz,ex,ey,ez,liquid,ignore,miss);
                if(!hit.hit&&!hit.miss)std::cout<<"R 0\n";
                else std::cout<<"R "<<(hit.hit?1:2)<<' '<<hit.x<<' '<<hit.y<<' '<<hit.z<<' '<<hit.face<<' '<<hit.hitX<<' '<<hit.hitY<<' '<<hit.hitZ<<'\n';
            }else throw std::runtime_error("unknown selection fixture command");
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
