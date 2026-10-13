#pragma once
#include "mc/math.hpp"
#include "mc/world.hpp"

namespace mc::visual {
inline gfx::Mat4 panoramaRotation(float degrees, int axis) {
    const float radians=degrees*.017453292519943295f;
    const float c=std::cos(radians),s=std::sin(radians);
    auto m=gfx::Mat4::identity();
    if(axis==0){m.v[5]=c;m.v[6]=s;m.v[9]=-s;m.v[10]=c;}
    else if(axis==1){m.v[0]=c;m.v[2]=-s;m.v[8]=s;m.v[10]=c;}
    else {m.v[0]=c;m.v[1]=s;m.v[4]=-s;m.v[5]=c;}
    return m;
}
// GuiMainMenu.drawPanorama: the jitter is a translation before the two
// animated rotations. Moving a ray after rotation gives a different image.
inline gfx::Mat4 panoramaMatrix(float ticks, int sample, int face) {
    auto jitter=gfx::Mat4::identity();
    jitter.v[12]=(float(sample%8)/8.f-.5f)/64.f;
    jitter.v[13]=(float(sample/8)/8.f-.5f)/64.f;
    auto view=panoramaRotation(180,0)*panoramaRotation(90,2)*jitter*
        panoramaRotation(javaSin(ticks/400.f)*25.f+20.f,0)*panoramaRotation(-ticks*.1f,1);
    if(face==1)view=view*panoramaRotation(90,1);
    else if(face==2)view=view*panoramaRotation(180,1);
    else if(face==3)view=view*panoramaRotation(-90,1);
    else if(face==4)view=view*panoramaRotation(90,0);
    else if(face==5)view=view*panoramaRotation(-90,0);
    return gfx::perspective(120,1,.05f,10)*view;
}
} // namespace mc::visual
