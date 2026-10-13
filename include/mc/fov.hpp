#pragma once
#include "mc/world.hpp"
#include <algorithm>
#include <cmath>

namespace mc::visual {
// ActiveRenderInfo.getBlockAtEntityViewpoint, including the liquid surface.
inline int viewpointMedium(const World& world,double x,double y,double z) {
    const int bx=floorBlock(x),by=floorBlock(y),bz=floorBlock(z);
    auto state=world.getBlock(bx,by,bz);auto id=blockId(state);
    if(id>=8&&id<=11){
        const int level=blockMeta(state)>=8?0:blockMeta(state);
        const float surface=float(by+1)-(float(level+1)/9.f-.11111111f);
        if(y>=double(surface))id=blockId(world.getBlock(bx,by+1,bz));
    }
    return id==8||id==9?1:id==10||id==11?2:0;
}
inline bool viewpointInWater(const World& world,double x,double y,double z){return viewpointMedium(world,x,y,z)==1;}
// AbstractClientPlayer.getFovModifier. movementSpeed excludes only the sprint
// modifier; the server has already included speed/slowness potion attributes.
inline float playerFovMultiplier(double movementSpeed,float walkSpeed,bool sprinting,bool flying,
                                 bool drawingBow,int bowUseTicks) {
    if(sprinting)movementSpeed*=1.+double(.30000001192092896);
    float value=flying?1.1f:1.f;
    value=float(double(value)*((movementSpeed/double(walkSpeed)+1.)/2.));
    if(walkSpeed==0.f||!std::isfinite(value))value=1.f;
    if(drawingBow){
        float draw=float(std::max(0,bowUseTicks))/20.f;
        if(draw>1)draw=1;else draw*=draw;
        value*=1.f-draw*.15f;
    }
    return value;
}
class FovTracker {
    float previous_=1,current_=1;
public:
    void reset(){previous_=current_=1;}
    void tick(float target){previous_=current_;current_=std::clamp(current_+(target-current_)*.5f,.1f,1.5f);}
    float multiplier(float partial) const{return previous_+(current_-previous_)*partial;}
    // EntityRenderer.getFOVModifier(false) keeps the hand at 70 degrees.
    float angle(float setting,float partial,bool useSetting,bool underwater,bool dead,int deathTime) const{
        float value=useSetting?setting*multiplier(partial):70.f;
        if(dead){const float time=float(deathTime)+partial;value/=(1.f-500.f/(time+500.f))*2.f+1.f;}
        if(underwater)value=value*60.f/70.f;
        return value;
    }
};
} // namespace mc::visual
