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
        if(argc!=3)throw std::runtime_error("worldgen_tree_tests requires fixture/output paths");
        std::ifstream in(argv[1]);std::ofstream out(argv[2]);if(!in||!out)throw std::runtime_error("cannot open tree fixture/output");std::string line;unsigned randomMushroomType=0;BigTreeState sharedBig[2];
        while(std::getline(in,line)){
            if(line.empty()||line[0]=='#')continue;std::istringstream row(line);std::int64_t seed;int kind,pattern,height,x,z;row>>seed>>kind>>pattern>>height>>x>>z;
            World world;load(world,pattern,height,x,z);JavaRandom random(seed);Position at{x,height+1,z};TreeOptions options;bool result=false;BigTreeState freshBig;BigTreeState* usedBig=nullptr;
            switch(kind){
            case 0:result=generateTree(world,random,at,TreeKind::Oak);break;
            case 1:case 11:options.minimumHeight=kind==1?7:4;options.log=blockState(17,3);options.leaves=blockState(18,3);options.vines=true;result=generateTree(world,random,at,TreeKind::Oak,options);break;
            case 2:result=generateTree(world,random,at,TreeKind::Birch);break;
            case 3:result=generateTree(world,random,at,TreeKind::TallBirch);break;
            case 4:result=generateTree(world,random,at,TreeKind::Pine);break;
            case 5:result=generateTree(world,random,at,TreeKind::Spruce);break;
            case 6:result=generateTree(world,random,at,TreeKind::Swamp);break;
            case 7:options.log=blockState(17,3);options.leaves=blockState(18);result=generateTree(world,random,at.add(0,10,0),TreeKind::Shrub,options);break;
            case 8:case 9:{unsigned type=kind==8?99:100;result=generateHugeMushroom(world,random,at,type);break;}
            case 10:result=generateHugeMushroom(world,random,at,randomMushroomType);break;
            case 12:result=generateTree(world,random,at,TreeKind::Acacia);break;
            case 13:result=generateTree(world,random,at,TreeKind::DarkOak);break;
            case 14:result=generateTree(world,random,at,TreeKind::MegaJungle);break;
            case 15:case 17:result=generateTree(world,random,at,TreeKind::MegaPine);if(result&&kind==17)decorateTreeBase(world,random,at,TreeKind::MegaPine);break;
            case 16:case 18:result=generateTree(world,random,at,TreeKind::MegaSpruce);if(result&&kind==18)decorateTreeBase(world,random,at,TreeKind::MegaSpruce);break;
            case 19:case 20:usedBig=&freshBig;result=generateBigTree(world,random,at,*usedBig,kind==20);break;
            case 21:case 22:usedBig=&sharedBig[kind-21];result=generateBigTree(world,random,at,*usedBig,kind==22);break;
            default:throw std::runtime_error("unknown tree type");
            }
            out<<"R "<<int(result)<<' '<<random.nextLong()<<'\n';if(usedBig)out<<"S "<<usedBig->heightLimit<<' '<<usedBig->leafDistance<<'\n';differences(out,world,pattern,height,x,z);out<<"END\n";
        }
        if(!out)throw std::runtime_error("failed writing tree result");return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
