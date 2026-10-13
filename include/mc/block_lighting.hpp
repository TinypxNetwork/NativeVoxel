#pragma once
#include "mc/assets.hpp"
#include "mc/world.hpp"
#include "mc/registry.hpp"
#include <algorithm>
#include <array>

namespace mc::visual {
struct VertexLight { float ao=1,sky=0,block=0; };
// BlockModelRenderer.AmbientOcclusionFace, expressed in face-local axes so the
// same four-corner sampling also handles rotated and partial block models.
template<class Get,class Light>std::array<VertexLight,4> quadLighting(const Get& get,const Light& light,
    int x,int y,int z,BlockState state,const ModelQuad& quad,bool smooth) {
    constexpr int dx[]{0,0,0,0,-1,1},dy[]{-1,1,0,0,0,0},dz[]{0,0,-1,1,0,0};
    const int face=quad.face,emission=registry::lightValue(blockId(state));
    auto brightness=[&](int bx,int by,int bz){const auto packed=light(bx,by,bz);
        return std::uint8_t((packed&240)|std::max(int(packed&15),emission));};
    auto opaque=[&](int bx,int by,int bz){const auto& b=registry::block(blockId(get(bx,by,bz)));return b.fullCube&&b.lightOpacity>=15;};
    auto ao=[&](int bx,int by,int bz){const auto& b=registry::block(blockId(get(bx,by,bz)));return b.fullCube&&b.materialBlocksMovement?.2f:1.f;};
    auto coord=[](const ModelVertex& v,int axis){return axis==0?v.x:axis==1?v.y:v.z;};
    int bx=x,by=y,bz=z;
    bool boundary=false;
    if(face>=0&&face<6){
        const int axis=face<2?1:face<4?2:0;
        float low=coord(quad.vertices[0],axis),high=low;
        for(const auto& v:quad.vertices){low=std::min(low,coord(v,axis));high=std::max(high,coord(v,axis));}
        boundary=low==high&&((face%2==0?low<.0001f:high>.9999f)||registry::block(blockId(state)).fullCube);
        if(boundary){bx+=dx[face];by+=dy[face];bz+=dz[face];}
    }
    const auto center=brightness(bx,by,bz);
    std::array<VertexLight,4> result;
    if(!smooth||emission>0||face<0||face>=6){
        for(auto& v:result)v={1,float(center>>4),float(center&15)};
        return result;
    }
    const int axisA=face<2?0:face==2?1:face==3?0:1;
    const int axisB=face<2?2:face==2?0:face==3?1:2;
    std::array<VertexLight,4> corners;
    int faceLight=brightness(x,y,z);
    if(boundary||!opaque(x+dx[face],y+dy[face],z+dz[face]))faceLight=brightness(x+dx[face],y+dy[face],z+dz[face]);
    const float centerAo=ao(bx,by,bz);
    for(int a=0;a<2;++a)for(int b=0;b<2;++b){
        int pa[]{bx,by,bz},pb[]{bx,by,bz},pc[]{bx,by,bz};
        pa[axisA]+=a?1:-1;pb[axisB]+=b?1:-1;
        pc[axisA]+=a?1:-1;pc[axisB]+=b?1:-1;
        float diagonal;int diagonalLight;
        const bool openA=registry::translucent(blockId(get(pa[0]+dx[face],pa[1]+dy[face],pa[2]+dz[face])));
        const bool openB=registry::translucent(blockId(get(pb[0]+dx[face],pb[1]+dy[face],pb[2]+dz[face])));
        const float ca=ao(pa[0],pa[1],pa[2]),cb=ao(pb[0],pb[1],pb[2]);
        int la=brightness(pa[0],pa[1],pa[2]),lb=brightness(pb[0],pb[1],pb[2]);
        if(!openA&&!openB){diagonal=ca;diagonalLight=la;}
        else {diagonal=ao(pc[0],pc[1],pc[2]);diagonalLight=brightness(pc[0],pc[1],pc[2]);}
        if(!la)la=faceLight;if(!lb)lb=faceLight;if(!diagonalLight)diagonalLight=faceLight;
        corners[a*2+b]={(ca+cb+diagonal+centerAo)*.25f,
            float((la>>4)+(lb>>4)+(diagonalLight>>4)+(faceLight>>4))*.25f,
            float((la&15)+(lb&15)+(diagonalLight&15)+(faceLight&15))*.25f};
    }
    for(int v=0;v<4;++v){
        float a=std::clamp(coord(quad.vertices[v],axisA),0.f,1.f),b=std::clamp(coord(quad.vertices[v],axisB),0.f,1.f);
        if(face<2){a=a>.5f?1.f:0.f;b=b>.5f?1.f:0.f;}
        result[v]={0,0,0};
        for(int ca=0;ca<2;++ca)for(int cb=0;cb<2;++cb){const float w=(ca?a:1-a)*(cb?b:1-b);const auto& c=corners[ca*2+cb];
            result[v].ao+=c.ao*w;result[v].sky+=c.sky*w;result[v].block+=c.block*w;}
    }
    return result;
}
} // namespace mc::visual
