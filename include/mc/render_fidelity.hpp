#pragma once
#include "mc/math.hpp"
#include "mc/world.hpp"
#include "mc/registry.hpp"
#include <array>
#include <cstdint>

namespace mc::visual {
struct InventoryPreviewRotation {float yaw=0,pitch=0;};
// GuiInventory.drawEntityOnScreen receives the GUI-space distance from the
// cursor to the model anchor; positive entity pitch looks down.
inline InventoryPreviewRotation inventoryPreviewRotation(float mouseX,float mouseY) {
    return {float(std::atan(double(mouseX/40.f)))*20.f,-float(std::atan(double(mouseY/40.f)))*20.f};
}
inline gfx::Mat4 viewBobbing(float previousDistance,float distance,float previousYaw,float yaw,
                            float previousPitch,float pitch,float partial) {
    const float phase=-(distance+(distance-previousDistance)*partial)*3.1415927f;
    const float amount=previousYaw+(yaw-previousYaw)*partial;
    const float tilt=previousPitch+(pitch-previousPitch)*partial;
    const float roll=javaSin(phase)*amount*3.f*.01745329252f;
    const float lean=(std::abs(javaCos(phase-.2f)*amount)*5.f+tilt)*.01745329252f;
    auto t=gfx::Mat4::identity(),rz=t,rx=t;
    t.v[12]=javaSin(phase)*amount*.5f;t.v[13]=-std::abs(javaCos(phase)*amount);
    rz.v[0]=std::cos(roll);rz.v[1]=std::sin(roll);rz.v[4]=-std::sin(roll);rz.v[5]=std::cos(roll);
    rx.v[5]=std::cos(lean);rx.v[6]=std::sin(lean);rx.v[9]=-std::sin(lean);rx.v[10]=std::cos(lean);
    return t*rz*rx;
}
// WorldProvider / EntityRenderer.updateLightmap, Minecraft 1.8.9.
inline float celestialAngle(std::int64_t ticks, float partial = 0) {
    if (ticks < 0) ticks = -ticks; // negative S03 time freezes the day cycle
    float f = (float(ticks % 24000) + partial) / 24000.f - .25f;
    if (f < 0) ++f;
    if (f > 1) --f;
    const float initial = f;
    f = 1.f - float((std::cos(double(f) * 3.141592653589793) + 1.) / 2.);
    return initial + (f - initial) / 3.f;
}
inline float sunBrightness(float angle, float rain = 0, float thunder = 0) {
    float f = 1.f - (std::cos(angle * 6.28318530718f) * 2.f + .2f);
    f = 1.f - std::clamp(f, 0.f, 1.f);
    f = float(double(f) * (1. - double(rain * 5.f) / 16.));
    f = float(double(f) * (1. - double(thunder * 5.f) / 16.));
    return f * .8f + .2f;
}
inline float brightness(int level, int dimension) {
    const float ambient = dimension == -1 ? .1f : 0.f;
    float f = 1.f - float(level) / 15.f;
    return (1.f - f) / (f * 3.f + 1.f) * (1.f - ambient) + ambient;
}
inline std::array<std::uint8_t,1024> lightmap(float sun, float gamma, int dimension = 0,
                                           float flicker = 0, float nightVision = 0) {
    std::array<std::uint8_t,1024> pixels{};
    for (int i=0;i<256;++i) {
        const float sky=brightness(i/16,dimension)*(sun*.95f+.05f);
        const float block=brightness(i%16,dimension)*(flicker*.1f+1.5f);
        const float green=block*((block*.6f+.4f)*.6f+.4f);
        const float blue=block*(block*block*.6f+.4f);
        std::array<float,3> c{{sky*(sun*.65f+.35f)+block, sky*(sun*.65f+.35f)+green, sky+blue}};
        for(float& x:c) x=x*.96f+.03f;
        if(dimension==1)c={{.22f+block*.75f,.28f+green*.75f,.25f+blue*.75f}};
        if(nightVision>0) {
            const float mult=1.f/std::max({c[0],c[1],c[2]});
            for(float& x:c)x=x*(1.f-nightVision)+x*mult*nightVision;
        }
        for(int k=0;k<3;++k) {
            float x=std::min(c[k],1.f), inv=1.f-x;
            x=x*(1.f-gamma)+(1.f-inv*inv*inv*inv)*gamma;
            pixels[i*4+k]=std::uint8_t(std::clamp(x*.96f+.03f,0.f,1.f)*255.f);
        }
        pixels[i*4+3]=255;
    }
    return pixels;
}
inline bool neighborBrightness(unsigned id) {
    if(id==0)return false;
    const auto& b=registry::block(id);
    if(b.lightOpacity==0 || registry::translucent(id) || id==60)return true;
    switch(id) {
    case 43:case 44:case 53:case 67:case 108:case 109:case 114:case 125:case 126:
    case 128:case 134:case 135:case 136:case 156:case 163:case 164:case 180:case 181:case 182:return true;
    default:return false;
    }
}
inline std::uint8_t combinedLight(const World& w,int x,int y,int z) {
    int sky=w.getSkyLight(x,y,z),block=w.getBlockLight(x,y,z);
    if(neighborBrightness(blockId(w.getBlock(x,y,z)))) {
        // ChunkCache.getLightForExt uses all six raw neighbours, without recursion.
        sky=block=0;
        constexpr int dx[]{0,0,0,0,-1,1},dy[]{-1,1,0,0,0,0},dz[]{0,0,-1,1,0,0};
        for(int i=0;i<6;++i){sky=std::max(sky,int(w.getSkyLight(x+dx[i],y+dy[i],z+dz[i])));
            block=std::max(block,int(w.getBlockLight(x+dx[i],y+dy[i],z+dz[i])));}
    }
    return std::uint8_t(sky<<4|block);
}
inline std::uint32_t skyColor(float temperature) {
    const float t=std::clamp(temperature/3.f,-1.f,1.f);
    const float h=(.62222224f-t*.05f)*6.f,s=.5f+t*.1f;
    const int sector=int(h)%6;
    const float f=h-float(int(h)),p=1.f-s,q=1.f-f*s,r=1.f-(1.f-f)*s;
    const std::array<gfx::Vec3,6> rgb{{{1,r,p},{q,1,p},{p,1,r},{p,q,1},{r,p,1},{1,p,q}}};
    auto c=rgb[sector];return (std::uint32_t(c.x*255)<<16)|(std::uint32_t(c.y*255)<<8)|std::uint32_t(c.z*255);
}
} // namespace mc::visual
