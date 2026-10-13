#include "../src/worldgen_features.hpp"
#include "worldgen_fixture.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace mc;
using namespace mc::worldgen;
using namespace mc::worldgen::detail;
using namespace mc::worldgen::test;
int main(int argc,char** argv){
    try{
        if(argc!=3)throw std::runtime_error("worldgen_feature_tests requires fixture/output paths");
        std::ifstream in(argv[1]);std::ofstream out(argv[2]);if(!in||!out)throw std::runtime_error("cannot open feature fixture/output");std::string line,error;
        while(std::getline(in,line)){
            if(line.empty()||line[0]=='#')continue;std::istringstream row(line);std::int64_t seed;int kind,pattern,height,x,z,noSky,light;row>>seed>>kind>>pattern>>height>>x>>z>>noSky>>light;
            World world;load(world,pattern,height,x,z);
            JavaRandom random(seed);PopulationOutput output;Position at{x,height+1,z};bool result=false;
            auto generate=[&](ScatterKind mode,BlockState state=0){return generateScatter(world,random,at,mode,state,noSky!=0,[&](Position p){return light-((p.x+p.z)&1);});};
            switch(kind){
            case 0:result=generate(ScatterKind::Flowers,blockState(37));break;
            case 1:result=generate(ScatterKind::Flowers,blockState(38,8));break;
            case 2:at.y+=10;result=generate(ScatterKind::TallGrass,blockState(31,1));break;
            case 3:at.y+=10;result=generate(ScatterKind::TallGrass,blockState(31,2));break;
            case 4:at.y+=10;result=generate(ScatterKind::DeadBush);break;
            case 5:result=generate(ScatterKind::DoublePlant,blockState(175));break;
            case 6:result=generate(ScatterKind::DoublePlant,blockState(175,4));break;
            case 7:result=generate(ScatterKind::Cactus);break;
            case 8:result=generate(ScatterKind::Reed);break;
            case 9:result=generate(ScatterKind::Pumpkin);break;
            case 10:result=generate(ScatterKind::Melon);break;
            case 11:result=generate(ScatterKind::Waterlily);break;
            case 12:result=generate(ScatterKind::Mushroom,blockState(39));break;
            case 13:result=generate(ScatterKind::Mushroom,blockState(40));break;
            case 14:result=generate(ScatterKind::Fire);break;
            case 15:case 16:at.y+=10;result=generate(ScatterKind::Glowstone);break;
            case 17:--at.y;result=generateSediment(world,random,at,blockState(82),4,true);break;
            case 18:--at.y;result=generateSediment(world,random,at,blockState(12),7);break;
            case 19:--at.y;result=generateSediment(world,random,at,blockState(13),6);break;
            case 20:at.y+=10;result=generateBlob(world,random,at,blockState(48),0);break;
            case 21:case 22:result=generateSpike(world,random,at,kind==21?121:1,output);break;
            case 23:result=generateIceSpike(world,random,at);break;
            case 24:result=generateIcePath(world,random,at);break;
            case 25:result=generateDesertWell(world,at);break;
            case 26:result=generateVines(world,random,at);break;
            default:throw std::runtime_error("unknown feature kind");
            }
            out<<"R "<<int(result)<<' '<<random.nextLong()<<'\n';
            for(auto& e:output.entities)out<<"E "<<e.id<<' '<<bits(e.x)<<' '<<bits(e.y)<<' '<<bits(e.z)<<' '<<bits(e.yaw)<<' '<<bits(e.pitch)<<'\n';
            differences(out,world,pattern,height,x,z);out<<"END\n";
        }
        if(!out)throw std::runtime_error("failed writing feature result");return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
