#include "mc/block_geometry.hpp"
#include "mc/block_lighting.hpp"
#include <map>
#include <tuple>
#include <iostream>
#include <stdexcept>

namespace {void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
int main(){try{
    using namespace mc;
    require(visual::wallConnects(blockState(1)),"wall stone connection");
    for(int id:{139,107,183,184,185,186,187})require(visual::wallConnects(blockState(id,1)),"wall gate/mossy connection");
    for(int id:{0,166,86,91,103,20,85,44})require(!visual::wallConnects(blockState(id)),"wall connects to excluded block");
    std::map<std::tuple<int,int,int>,BlockState> blocks;
    const auto get=[&](int x,int y,int z){const auto found=blocks.find({x,y,z});return found==blocks.end()?BlockState(0):found->second;};
    const auto light=[](int,int,int){return std::uint8_t(0xf0);};
    AtlasSprite still{.1f,.2f,.3f,.4f,true},flow{.5f,.6f,.7f,.8f,true};
    blocks[{0,0,0}]=blockState(8,8);blocks[{0,1,0}]=blockState(9);
    const auto lower=visual::fluidQuads(get,light,still,flow,0,0,0,true);
    require(lower.size()==9,"stacked water exposes an internal top face");
    for(const auto& q:lower)if(q.shade!=.5f){float top=0;for(const auto& v:q.vertices)top=std::max(top,v.y);require(top==1,"waterfall has a horizontal gap");}
    blocks[{0,2,0}]=blockState(8,8);
    const auto middle=visual::fluidQuads(get,light,still,flow,0,1,0,true);
    require(middle.size()==8,"mixed still/flowing IDs expose an internal bottom face");
    for(const auto& q:middle){float low=100,high=-100;for(const auto& v:q.vertices){low=std::min(low,v.y);high=std::max(high,v.y);require(v.u>=flow.u0&&v.u<=flow.u1&&v.v>=flow.v0&&v.v<=flow.v1,"flowing water samples another sprite");}
        require(low==1&&high==2,"middle waterfall segment does not meet its neighbours");}
    blocks.clear();blocks[{0,0,0}]=blockState(9);blocks[{1,0,0}]=blockState(8,6);
    const auto a=visual::fluidHeight(get,1,0,0,true),b=visual::fluidHeight(get,1,0,1,true);
    require(a==b&&a>.4f&&a<1,"adjacent source/flow corner height mismatch");
    require(visual::fluidFlowAngle(get,0,0,0,true)>-999,"flowing surface uses static texture");
    ModelQuad top;top.face=1;top.vertices={{{0,1,0},{0,1,1},{1,1,1},{1,1,0}}};
    blocks.clear();blocks[{0,0,0}]=blockState(1);
    auto flat=visual::quadLighting(get,light,0,0,0,blockState(1),top,true);
    for(const auto& v:flat)require(v.ao==1&&v.sky==15,"unoccluded stone lost light");
    blocks[{-1,1,0}]=blockState(1);blocks[{0,1,-1}]=blockState(1);blocks[{-1,1,-1}]=blockState(1);
    auto shaded=visual::quadLighting(get,light,0,0,0,blockState(1),top,true);
    require(std::abs(shaded[0].ao-.4f)<1e-6f,"vanilla four-sample corner occlusion");
    require(shaded[2].ao==1,"occlusion leaks to opposite corner");
    auto disabled=visual::quadLighting(get,light,0,0,0,blockState(1),top,false);
    for(const auto& v:disabled)require(v.ao==1,"smooth-lighting option ignored");
    std::cout<<"PASS: wall connections, mixed liquid IDs, continuous waterfalls, flow UVs and per-corner AO\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
