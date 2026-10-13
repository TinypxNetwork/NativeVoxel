#pragma once
#include "mc/game.hpp"
#include "mc/math.hpp"
#include "mc/entity_dimensions.hpp"
#include <algorithm>
#include <cmath>

namespace mc::visual {
inline int metadataInt(const Entity& entity,int key,int fallback=0) {
    const auto value=entity.metadata.find(std::uint8_t(key));
    return value==entity.metadata.end()?fallback:int(value->second.integer);
}
struct EntitySize {float width=.6f,height=1.8f;};
// Entity constructors/setSize; child sizes are also used by EntityParticleEmitter.
inline EntitySize entitySize(const Entity& entity) {
    const auto base=entityDimensions(entity.kind==EntityKind::Player?0:entity.kind==EntityKind::Mob?1:2,entity.type);
    EntitySize size=base.valid?EntitySize{base.width,base.height}:EntitySize{};
    if(entity.kind!=EntityKind::Mob)return size;
    switch(entity.type) {
    case 30:if(metadataInt(entity,10)&16)return {0,0};break;
    case 55:case 62:{float side=.51000005f*std::max(1,metadataInt(entity,16,1));size={side,side};break;}
    case 68:size=(metadataInt(entity,16)&4)?EntitySize{1.9975f,1.9975f}:EntitySize{.85f,.85f};break;
    }
    const bool child=entity.type==30?(metadataInt(entity,10)&1)!=0:
        entity.type==54||entity.type==57?metadataInt(entity,12)!=0:metadataInt(entity,12)<0;
    if(child){size.width*=.5f;size.height*=.5f;}
    return size;
}
// RendererLivingEntity.rotateCorpse; squid's renderer deliberately overrides it.
inline float deathRotation(int kind,int type,int deathTime,float partial) {
    if(deathTime<=0||(kind==1&&type==94))return 0;
    const float maximum=kind==1&&(type==52||type==59||type==60||type==67)?180.f:90.f;
    float progress=(float(deathTime)+partial-1.f)/20.f*1.6f;
    return std::min(1.f,float(std::sqrt(std::max(0.f,progress))))*maximum;
}
inline gfx::Mat4 livingOrientation(float yaw,int kind,int type,int deathTime,float partial) {
    const float angle=yaw*.01745329252f,roll=-deathRotation(kind,type,deathTime,partial)*.01745329252f;
    auto y=gfx::Mat4::identity(),z=y;
    y.v[0]=std::cos(angle);y.v[2]=std::sin(angle);y.v[8]=-std::sin(angle);y.v[10]=std::cos(angle);
    z.v[0]=std::cos(roll);z.v[1]=std::sin(roll);z.v[4]=-std::sin(roll);z.v[5]=std::cos(roll);
    return y*z;
}
inline gfx::Mat4 hurtCamera(int hurtTime,int maxHurtTime,float attackedAtYaw,int deathTime,bool dead,float partial) {
    auto result=gfx::Mat4::identity();
    auto rotate=[](int axis,float degrees){auto m=gfx::Mat4::identity();const float angle=degrees*.01745329252f,c=std::cos(angle),s=std::sin(angle);
        if(axis==1){m.v[0]=c;m.v[8]=s;m.v[2]=-s;m.v[10]=c;}
        else{m.v[0]=c;m.v[1]=s;m.v[4]=-s;m.v[5]=c;}return m;};
    if(dead)result=result*rotate(2,40.f-8000.f/(float(deathTime)+partial+200.f));
    float t=float(hurtTime)-partial;if(t<0||maxHurtTime<=0)return result;
    t/=float(maxHurtTime);t=javaSin(t*t*t*t*3.1415927f);
    return result*rotate(1,-attackedAtYaw)*rotate(2,-t*14.f)*rotate(1,attackedAtYaw);
}
struct MediumFog {gfx::Vec3 color{};float density=0;};
inline MediumFog mediumFog(int medium,int respiration,bool waterBreathing) {
    if(medium==2)return {{.6f,.1f,0},2.f};
    if(medium!=1)return {};
    float color=float(std::max(0,respiration))*.2f;
    if(waterBreathing)color=color*.3f+.6f;
    return {{.02f+color,.02f+color,.2f+color},waterBreathing?.01f:std::max(0.f,.1f-float(respiration)*.03f)};
}
} // namespace mc::visual
