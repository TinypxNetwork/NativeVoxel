#include "mc/assets.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){
    try{
        mc::Assets assets;std::string error;const bool loaded=assets.load(argc>1?argv[1]:"assets",error);check(loaded,error.c_str());unsigned flowers=0,slabs=0;
        for(unsigned upper=8;upper<16;++upper)for(unsigned lower=0;lower<16;++lower){
            const auto state=std::uint16_t(175*16+upper),below=std::uint16_t(175*16+lower);
            const auto& expected=assets.modelWithProperties(below,{{"half","upper"}},-17,64,31);
            check(&assets.doublePlantModel(state,below,-17,64,31)==&expected,"double-plant upper model must inherit lower variant");++flowers;
        }
        for(unsigned upper=8;upper<16;++upper){const auto state=std::uint16_t(175*16+upper);check(&assets.doublePlantModel(state,0)==&assets.model(state,0,0,0),"missing lower block retains sunflower default");}
        for(unsigned lower=0;lower<6;++lower){const auto state=std::uint16_t(175*16+lower);check(&assets.doublePlantModel(state,175*16+4)==&assets.model(state,0,0,0),"lower double plant retains own model");}
        for(unsigned id:{44u,126u,182u})for(unsigned meta=0;meta<(id==44?8u:id==126?6u:1u);++meta)for(unsigned half:{0u,8u}){
            const auto& model=assets.model(std::uint16_t(id*16+meta+half));check(!model.quads.empty(),"slab model must not be empty");float low=2,high=-1;
            for(const auto& quad:model.quads)for(const auto& vertex:quad.vertices){low=std::min(low,vertex.y);high=std::max(high,vertex.y);check(vertex.x>=-1e-6f&&vertex.x<=1.000001f&&vertex.z>=-1e-6f&&vertex.z<=1.000001f,"slab horizontal model bounds");}
            check(std::abs(low-(half?.5f:0.0f))<1e-6f&&std::abs(high-(half?1.0f:.5f))<1e-6f,"slab upper/lower model height");++slabs;
        }
        std::cout<<"PASS: "<<flowers<<" upper double-plant resolutions and "<<slabs<<" slab bounds; invalid-neighbor/lower fallbacks\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
