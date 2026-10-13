#pragma once
#include "mc/assets.hpp"
#include "mc/world.hpp"
#include "mc/registry.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace mc::visual {
inline bool wallConnects(BlockState state) {
    const unsigned id=blockId(state);const auto& b=registry::block(id);
    if(id==166)return false;
    if(id==139||id==107||(id>=183&&id<=187))return true;
    return b.materialOpaque&&b.fullCube&&std::strcmp(b.material,"gourd")!=0;
}
inline bool sameLiquid(BlockState state,bool water) {
    const unsigned id=blockId(state);return water?(id==8||id==9):(id==10||id==11);
}
template<class Get>float fluidHeight(const Get& get,int x,int y,int z,bool water) {
    float total=0;int count=0;
    for(int j=0;j<4;++j){
        const int bx=x-(j&1),bz=z-(j>>1&1);
        if(sameLiquid(get(bx,y+1,bz),water))return 1.f;
        const auto state=get(bx,y,bz);
        if(!sameLiquid(state,water)){
            if(!registry::block(blockId(state)).materialSolid){++total;++count;}
        }else{
            const int level=blockMeta(state);const float height=float((level>=8?0:level)+1)/9.f;
            if(level>=8||level==0){total+=height*10.f;count+=10;}
            total+=height;++count;
        }
    }
    return count?1.f-total/float(count):1.f;
}
template<class Get>float fluidFlowAngle(const Get& get,int x,int y,int z,bool water) {
    auto decay=[&](int bx,int by,int bz){const auto s=get(bx,by,bz);return sameLiquid(s,water)?(blockMeta(s)>=8?0:int(blockMeta(s))):-1;};
    const int level=decay(x,y,z);double vx=0,vz=0;
    constexpr int dx[]{0,1,0,-1},dz[]{-1,0,1,0};
    for(int i=0;i<4;++i){
        const int bx=x+dx[i],bz=z+dz[i];int next=decay(bx,y,bz),slope=0;
        if(next<0){if(!registry::block(blockId(get(bx,y,bz))).materialBlocksMovement){next=decay(bx,y-1,bz);if(next>=0)slope=next-(level-8);}}
        else slope=next-level;
        vx+=dx[i]*slope;vz+=dz[i]*slope;
    }
    return vx==0&&vz==0?-1000.f:float(std::atan2(vz,vx)-1.5707963267948966);
}
struct FluidQuad {
    std::array<ModelVertex,4> vertices;
    std::uint8_t light=0;
    float shade=1;
    bool tinted=true;
};
// BlockFluidRenderer: geometry depends on a 3x3 neighbourhood, not on the
// metadata of this block alone. Both flowing/still IDs share one material.
template<class Get,class Light>std::vector<FluidQuad> fluidQuads(const Get& get,const Light& light,
    const AtlasSprite& still,const AtlasSprite& flow,int x,int y,int z,bool water) {
    constexpr int dx[]{0,0,0,0,-1,1},dy[]{-1,1,0,0,0,0},dz[]{0,0,-1,1,0,0};
    std::array<bool,6> visible{};
    for(int f=0;f<6;++f){const auto n=get(x+dx[f],y+dy[f],z+dz[f]);
        visible[f]=!sameLiquid(n,water)&&(f==1||!registry::block(blockId(n)).normalCube);}
    std::vector<FluidQuad> result;result.reserve(11);
    if(std::none_of(visible.begin(),visible.end(),[](bool value){return value;}))return result;
    auto brightness=[&](int bx,int by,int bz){const auto a=light(bx,by,bz),b=light(bx,by+1,bz);
        return std::uint8_t((std::max(a>>4,b>>4)<<4)|std::max(a&15,b&15));};
    auto heights=std::array<float,4>{{fluidHeight(get,x,y,z,water),fluidHeight(get,x,y,z+1,water),
        fluidHeight(get,x+1,y,z+1,water),fluidHeight(get,x+1,y,z,water)}};
    if(visible[1]){
        for(float& h:heights)h-=.001f;
        const float angle=fluidFlowAngle(get,x,y,z,water);std::array<float,4> u{},v{};
        if(angle<-999){u={{still.u(0),still.u(0),still.u(16),still.u(16)}};v={{still.v(0),still.v(16),still.v(16),still.v(0)}};}
        else {const float s=javaSin(angle)*.25f,c=javaCos(angle)*.25f;
            u={{flow.u(8+(-c-s)*16),flow.u(8+(-c+s)*16),flow.u(8+(c+s)*16),flow.u(8+(c-s)*16)}};
            v={{flow.v(8+(-c+s)*16),flow.v(8+(c+s)*16),flow.v(8+(c-s)*16),flow.v(8+(-c-s)*16)}};}
        FluidQuad q{{{{float(x),y+heights[0],float(z),u[0],v[0]},
            {float(x),y+heights[1],float(z+1),u[1],v[1]},
            {float(x+1),y+heights[2],float(z+1),u[2],v[2]},
            {float(x+1),y+heights[3],float(z),u[3],v[3]}}},brightness(x,y,z),1};
        result.push_back(q);
        bool underside=false;
        for(int bz=-1;bz<=1;++bz)for(int bx=-1;bx<=1;++bx){const auto s=get(x+bx,y+1,z+bz);
            if(!sameLiquid(s,water)&&!registry::block(blockId(s)).fullBlock)underside=true;}
        if(underside){std::reverse(q.vertices.begin()+1,q.vertices.end());result.push_back(q);}
    }
    if(visible[0])result.push_back({{{{float(x),float(y),float(z+1),still.u(0),still.v(16)},
        {float(x),float(y),float(z),still.u(0),still.v(0)},
        {float(x+1),float(y),float(z),still.u(16),still.v(0)},
        {float(x+1),float(y),float(z+1),still.u(16),still.v(16)}}},brightness(x,y-1,z),.5f,false});
    constexpr int first[]{0,2,1,3},second[]{3,1,0,2};
    for(int side=0;side<4;++side)if(visible[side+2]){
        const float h0=heights[first[side]],h1=heights[second[side]];
        const float xs[][2]{{float(x),float(x+1)},{float(x+1),float(x)},{x+.001f,x+.001f},{x+.999f,x+.999f}};
        const float zs[][2]{{z+.001f,z+.001f},{z+.999f,z+.999f},{float(z+1),float(z)},{float(z),float(z+1)}};
        FluidQuad q{{{{xs[side][0],y+h0,zs[side][0],flow.u(0),flow.v((1-h0)*8)},
            {xs[side][1],y+h1,zs[side][1],flow.u(8),flow.v((1-h1)*8)},
            {xs[side][1],float(y),zs[side][1],flow.u(8),flow.v(8)},
            {xs[side][0],float(y),zs[side][0],flow.u(0),flow.v(8)}}},
            brightness(x+dx[side+2],y,z+dz[side+2]),side<2?.8f:.6f};
        result.push_back(q);std::reverse(q.vertices.begin(),q.vertices.end());result.push_back(q);
    }
    return result;
}
} // namespace mc::visual
