#include "mc/particles.hpp"
#include "mc/game.hpp"
#include "mc/entity_effects.hpp"
#include "mc/registry.hpp"
#include "mc/potions.hpp"
#include <algorithm>
#include <cmath>

namespace mc {
float ParticleSystem::random(){random_=(random_*0x5deece66dull+11)&((1ull<<48)-1);return float(random_>>24)/16777216.f;}
double ParticleSystem::gaussian(){double a,b,s;do{a=double(random())*2-1;b=double(random())*2-1;s=a*a+b*b;}while(s>=1||s<1e-15);return a*std::sqrt(-2*std::log(s)/s);}
void ParticleSystem::clear(){particles_.clear();emitters_.clear();}
void ParticleSystem::configure(int quality,double x,double y,double z){quality_=std::clamp(quality,0,2);haveViewer_=true;viewerX_=x;viewerY_=y;viewerZ_=z;}
void ParticleSystem::add(Particle p,bool ignoreRange){
    if(!ignoreRange&&p.type>2){
        if(quality_==2||(quality_==1&&int(random()*3)==0))return;
        const double dx=p.sprite.x-viewerX_,dy=p.sprite.y-viewerY_,dz=p.sprite.z-viewerZ_;
        if(haveViewer_&&dx*dx+dy*dy+dz*dz>256)return;
    }
    if(particles_.size()>=limit)particles_.erase(particles_.begin(),particles_.begin()+std::ptrdiff_t(limit/8));particles_.push_back(std::move(p));
}
ParticleSystem::Particle ParticleSystem::make(int type,double x,double y,double z,double vx,double vy,double vz,const std::vector<int>& data){
    Particle p;p.type=type;auto& s=p.sprite;s.x=p.px=p.originX=x;s.y=p.py=p.originY=y;s.z=p.pz=p.originZ=z;
    s.jitterX=random()*3;s.jitterY=random()*3;p.scale=(random()*.5f+.5f)*2;
    p.lifetime=int(4.f/(random()*.9f+.1f));
    // Most subclasses pass zero to EntityFX; others pass the packet velocity
    // before the superclass adds noise and normalizes it.
    const bool baseSpeed=type==0||type==7||type==8||type==22||type==21||type==24||type==25||type==26||type==32||type==37||type==38;
    const bool spell=type>=13&&type<=17;
    const double baseX=spell?.5-random():baseSpeed?vx:0,baseY=spell||baseSpeed?vy:0,baseZ=spell?.5-random():baseSpeed?vz:0;
    p.vx=baseX+(random()*2-1)*.4000000059604645;p.vy=baseY+(random()*2-1)*.4000000059604645;p.vz=baseZ+(random()*2-1)*.4000000059604645;
    const float force=(random()+random()+1)*.15f;
    const double length=std::max(1e-12,std::sqrt(p.vx*p.vx+p.vy*p.vy+p.vz*p.vz));
    p.vx=p.vx/length*force*.4000000059604645;p.vy=p.vy/length*force*.4000000059604645+.10000000149011612;p.vz=p.vz/length*force*.4000000059604645;
    switch(type){
    case 0:p.vx=vx+(random()*2-1)*.05;p.vy=vy+(random()*2-1)*.05;p.vz=vz+(random()*2-1)*.05;s.r=s.g=s.b=random()*.3f+.7f;p.scale=random()*random()*6+1;p.lifetime=int(16/(random()*.8f+.2f))+2;p.drag=.9f;break;
    case 1:s.texture=3;p.scale=(1-float(vx)*.5f)*20;p.lifetime=6+int(random()*4);p.vx=p.vy=p.vz=0;s.fullBright=true;break;
    case 2:p.lifetime=8;p.vx=p.vy=p.vz=0;break;
    case 3:s.index=160;p.lifetime=48+int(random()*12);p.vx=vx;p.vy=vy;p.vz=vz;p.scale*=.75f;p.gravity=.1f;p.drag=.91f;s.fullBright=true;break;
    case 4:s.index=32;p.scale*=random()*.6f+.2f;p.vx=vx*.2+(random()*2-1)*.02;p.vy=vy*.2+(random()*2-1)*.02;p.vz=vz*.2+(random()*2-1)*.02;p.lifetime=int(8/(random()*.8f+.2f));p.drag=.85f;break;
    case 5:case 39:s.index=19+int(random()*4);p.vx*=.3;p.vz*=.3;p.vy=random()*.2+.1;p.gravity=1.5f;p.lifetime=int(8/(random()*.8f+.2f));if(type==5&&vy==0&&(vx!=0||vz!=0)){p.vx=vx;p.vz=vz;p.vy=.1;}break;
    case 6:s.index=19;p.lifetime=int(8/(random()*.8f+.2f));p.vx=vx;p.vy=vy;p.vz=vz;p.gravity=0;break;
    case 7:s.index=0;p.scale*=random()*.6f+.2f;p.vx=p.vy=p.vz=0;p.lifetime=int(16/(random()*.8f+.2f));s.r=s.g=.4f;s.b=.7f;s.y=p.py=p.originY=y-.125;break;
    case 8:case 21:case 22:s.index=type==21?82:0;p.scale*=random()*.6f+.5f;p.vx*=.02;p.vy*=.02;p.vz*=.02;p.lifetime=int(20/(random()*.8f+.2f));s.r=s.g=s.b=type==21?1:random()*.1f+.2f;p.noClip=true;p.drag=.99f;break;
    case 9:case 10:s.index=type==9?65:66;p.vx=p.vx*.1+vx*.4;p.vy=p.vy*.1+vy*.4;p.vz=p.vz*.1+vz*.4;s.r=s.g=s.b=random()*.3f+.6f;if(type==10){s.r*=.3f;s.g*=.8f;}p.scale*=.75f;p.lifetime=int(6/(random()*.8f+.6f));p.drag=.7f;break;
    case 11:case 12:case 29:case 32:p.vx=p.vx*.1+vx;p.vy=p.vy*.1+vy;p.vz=p.vz*.1+vz;p.scale*=.75f*(type==12?2.5f:type==29?2.5f:1.f);p.lifetime=int(8/(random()*.8f+(type==29?.3f:.2f)));if(type==12||type==29)p.lifetime=int(p.lifetime*2.5f);s.r=s.g=s.b=type==11||type==12?random()*.3f:1-random()*.3f;p.drag=type==32?.99f:.96f;break;
    case 13:case 14:case 15:case 16:case 17:s.index=type==14?144:128;if(vx==0&&vz==0){p.vx*=.1;p.vz*=.1;}p.vy*=.2;p.scale*=.75f;p.drag=.96f;p.lifetime=int(8/(random()*.8f+.2f));if(type==15||type==16){s.r=float(vx);s.g=float(vy);s.b=float(vz);s.a=type==16?.15f:1;}if(type==17){s.r=s.b=.35f+random()*.5f;s.g=0;s.index=144;}break;
    case 18:case 19:s.index=113;s.r=type==18?.2f:1;s.g=type==18?.3f:0;s.b=type==18?1:0;p.vx=p.vy=p.vz=0;p.lifetime=int(64/(random()*.8f+.2f));p.gravity=1.5f;p.scale=.6f;s.fullBright=type==19;break;
    case 20:case 34:s.index=type==20?81:80;p.vx*=.01;p.vy=p.vy*.01+.1;p.vz*=.01;p.scale*=1.5f;p.lifetime=16;p.drag=.86f;if(type==20)s.y=p.py=p.originY=y+.5;break;
    case 23:s.index=64;p.vx*=.01;p.vy=p.vy*.01+.2;p.vz*=.01;p.scale*=1.5f;p.lifetime=6;p.drag=.66f;s.r=std::sin((float(vx)+0)*6.2831853f)*.65f+.35f;s.g=std::sin((float(vx)+.33333334f)*6.2831853f)*.65f+.35f;s.b=std::sin((float(vx)+.6666667f)*6.2831853f)*.65f+.35f;break;
    case 24:case 25:{p.vx=vx;p.vy=vy;p.vz=vz;p.noClip=true;const float color=random()*.6f+.4f;p.scale=type==24?random()*.2f+.5f:random()*.5f+.2f;p.lifetime=type==24?40+int(random()*10):30+int(random()*10);s.index=type==24?int(random()*8):225+int(random()*26);s.r=color*.9f;s.g=color*(type==24?.3f:.9f);s.b=color;if(type==25){s.x=p.px=x+vx;s.y=p.py=y+vy;s.z=p.pz=z+vz;}break;}
    case 26:s.index=48;p.vx=p.vx*.01+vx;p.vy=p.vy*.01+vy;p.vz=p.vz*.01+vz;s.x+=(random()-random())*.05;s.y+=(random()-random())*.05;s.z+=(random()-random())*.05;p.lifetime=int(8/(random()*.8f+.2f))+4;p.noClip=true;p.drag=.96f;s.fullBright=true;break;
    case 27:s.index=49;p.vx*=.8;p.vz*=.8;p.vy=random()*.4+.05;p.scale*=random()*2+.2f;p.lifetime=int(16/(random()*.8f+.2f));p.gravity=.75f;p.drag=.999f;s.fullBright=true;break;
    case 28:s.texture=4;p.lifetime=200;p.vx=p.vy=p.vz=0;p.scale=1.25f;break;
    case 30:{p.vx*=.1;p.vy*=.1;p.vz*=.1;const float f=random()*.4f+.6f;if(vx==0)vx=1;s.r=(random()*.2f+.8f)*float(vx)*f;s.g=(random()*.2f+.8f)*float(vy)*f;s.b=(random()*.2f+.8f)*float(vz)*f;p.scale*=.75f;p.lifetime=int(8/(random()*.8f+.2f));p.drag=.96f;break;}
    case 31:case 33:case 35:case 36:s.texture=2;s.itemId=type==31?332:type==33?341:type==35?166:data.empty()?-1:data[0];s.metadata=data.size()>1?data[1]:0;if(type==36){p.vx=p.vx*.1+vx;p.vy=p.vy*.1+vy;p.vz=p.vz*.1+vz;}p.gravity=1;p.scale*=.5f;if(type==35){p.vx=p.vy=p.vz=0;p.gravity=0;p.scale=5;p.lifetime=80;s.jitterX=s.jitterY=-1;}break;
    case 37:case 38:s.texture=1;s.block=data.empty()?blockState(1):BlockState(data[0]);s.r=s.g=s.b=.6f;p.gravity=1;p.scale*=.5f;if(type==38){p.vx=vx;p.vy=vy;p.vz=vz;}break;
    default:p.lifetime=0;break;
    }
    s.size=p.scale*.1f;return p;
}
void ParticleSystem::spawn(const ParticleEvent& event){
    if(event.type<0||event.type>39||event.count<0||!std::isfinite(event.x)||!std::isfinite(event.y)||!std::isfinite(event.z)||!std::isfinite(event.offsetX)||!std::isfinite(event.offsetY)||!std::isfinite(event.offsetZ)||!std::isfinite(event.speed))return;
    if((event.type==36||event.type==37||event.type==38)&&event.data.empty())return;
    const int count=std::min(event.count?event.count:1,4000);
    auto data=event.data;
    if((event.type==37||event.type==38)&&!data.empty())data[0]=blockState(unsigned(data[0])&4095,unsigned(data[0])>>12);
    for(int i=0;i<count;++i){
        const double x=event.x+(event.count?gaussian()*event.offsetX:0),y=event.y+(event.count?gaussian()*event.offsetY:0),z=event.z+(event.count?gaussian()*event.offsetZ:0);
        const double vx=event.count?gaussian()*event.speed:event.speed*event.offsetX,vy=event.count?gaussian()*event.speed:event.speed*event.offsetY,vz=event.count?gaussian()*event.speed:event.speed*event.offsetZ;
        add(make(event.type,x,y,z,vx,vy,vz,data),event.longDistance);
    }
}
void ParticleSystem::blockHit(const World& world,int x,int y,int z,int face){
    const auto state=world.getBlock(x,y,z);if(!state||face<0||face>5)return;
    const auto b=world.selectionBounds(x,y,z);
    double px=b.minX+.1+random()*std::max(0.,b.maxX-b.minX-.2),py=b.minY+.1+random()*std::max(0.,b.maxY-b.minY-.2),pz=b.minZ+.1+random()*std::max(0.,b.maxZ-b.minZ-.2);
    if(face==0)py=b.minY-.1;if(face==1)py=b.maxY+.1;if(face==2)pz=b.minZ-.1;if(face==3)pz=b.maxZ+.1;if(face==4)px=b.minX-.1;if(face==5)px=b.maxX+.1;
    auto p=make(37,px,py,pz,0,0,0,{state});p.vx*=.2;p.vy=(p.vy-.1)*.2+.1;p.vz*=.2;p.scale*=.6f;add(p,true);
}
void ParticleSystem::blockDestroyed(BlockState state,int x,int y,int z){
    if(!state)return;
    for(int a=0;a<4;++a)for(int b=0;b<4;++b)for(int c=0;c<4;++c){
        const double px=(a+.5)/4.,py=(b+.5)/4.,pz=(c+.5)/4.;add(make(37,x+px,y+py,z+pz,px-.5,py-.5,pz-.5,{state}),true);
    }
}
void ParticleSystem::emitBurst(Emitter& e) {
    // EntityParticleEmitter emits immediately and on each of the next two ticks.
    for(int i=0;i<16;++i){const double dx=double(random()*2.f-1.f),dy=double(random()*2.f-1.f),dz=double(random()*2.f-1.f);
        if(dx*dx+dy*dy+dz*dz<=1.)add(make(e.type,e.x+dx*double(e.width)/4.,e.y+double(e.height/2.f)+dy*double(e.height)/4.,e.z+dz*double(e.width)/4.,dx,dy+.2,dz,{}));}
    ++e.age;
}
void ParticleSystem::emitEntity(const Game& game,int id,int type) {
    if(type!=9&&type!=10)return;
    Emitter emitter;emitter.entityId=id;emitter.type=type;
    if(id==game.entityId){emitter.x=game.player.x;emitter.y=game.player.y;emitter.z=game.player.z;}
    else{const auto found=game.entities.find(id);if(found==game.entities.end())return;const auto& entity=found->second;
        const auto size=visual::entitySize(entity);emitter.x=entity.x;emitter.y=entity.y;emitter.z=entity.z;emitter.width=size.width;emitter.height=size.height;}
    emitBurst(emitter);
    if(emitters_.size()>=512)emitters_.erase(emitters_.begin());
    emitters_.push_back(emitter);
}
void ParticleSystem::tickEmitters(const Game& game) {
    for(auto& emitter:emitters_){
        if(emitter.entityId==game.entityId){emitter.x=game.player.x;emitter.y=game.player.y;emitter.z=game.player.z;}
        else{const auto found=game.entities.find(emitter.entityId);if(found!=game.entities.end()){emitter.x=found->second.x;emitter.y=found->second.y;emitter.z=found->second.z;}}
        emitBurst(emitter);
    }
    emitters_.erase(std::remove_if(emitters_.begin(),emitters_.end(),[](const Emitter& e){return e.age>=3;}),emitters_.end());
}
void ParticleSystem::entityDeath(const EntityDeathEvent& event) {
    for(int i=0;i<20;++i){const double vx=gaussian()*.02,vy=gaussian()*.02,vz=gaussian()*.02;
        const double x=event.x+double(random()*event.width*2.f)-double(event.width),y=event.y+double(random()*event.height),z=event.z+double(random()*event.width*2.f)-double(event.width);
        add(make(0,x,y,z,vx,vy,vz,{}));}
}
void ParticleSystem::worldEffect(const WorldEffectEvent& event,const World& world) {
    if(event.effect==2000){const int dx=event.data%3-1,dz=event.data/3%3-1;
        for(int i=0;i<10;++i){const double speed=double(random())*.2+.01;
            const double x=event.x+double(dx)*.6+.5+double(dx)*.01+(double(random())-.5)*double(dz)*.5;
            const double y=event.y+.5+(double(random())-.5)*.5;
            const double z=event.z+double(dz)*.6+.5+double(dz)*.01+(double(random())-.5)*double(dx)*.5;
            add(make(11,x,y,z,double(dx)*speed+gaussian()*.01,-.03+gaussian()*.01,double(dz)*speed+gaussian()*.01,{}));}
    }else if(event.effect==2001)blockDestroyed(blockState(unsigned(event.data)&4095,unsigned(event.data)>>12),event.x,event.y,event.z);
    else if(event.effect==2002){
        for(int i=0;i<8;++i)add(make(36,event.x,event.y,event.z,gaussian()*.15,double(random())*.2,gaussian()*.15,{373,event.data}));
        const auto color=potions::color(event.data);
        // Instant-health/damage are the instant spell sprite. Other potion
        // effects use the ordinary spell particle, as ItemPotion.isEffectInstant.
        const int type=potions::instant(potions::type(event.data))?14:13;
        for(int i=0;i<100;++i){const double radius=double(random())*4.,angle=double(random())*6.283185307179586,dx=std::cos(angle)*radius,dz=std::sin(angle)*radius,dy=.01+double(random())*.5;
            auto particle=make(type,event.x+dx*.1,event.y+.3,event.z+dz*.1,dx,dy,dz,{});const float tint=.75f+random()*.25f;
            particle.sprite.r=float((color>>16)&255)/255.f*tint;particle.sprite.g=float((color>>8)&255)/255.f*tint;particle.sprite.b=float(color&255)/255.f*tint;
            particle.vx*=float(radius);particle.vy=(particle.vy-.1)*float(radius)+.1;particle.vz*=float(radius);add(particle);}
    }else if(event.effect==2003){
        const double x=event.x+.5,y=event.y,z=event.z+.5;
        for(int i=0;i<8;++i)add(make(36,x,y,z,gaussian()*.15,double(random())*.2,gaussian()*.15,{381}));
        for(double angle=0;angle<6.283185307179586;angle+=.15707963267948966)for(double speed:{-5.,-7.})add(make(24,x+std::cos(angle)*5,y-.4,z+std::sin(angle)*5,std::cos(angle)*speed,0,std::sin(angle)*speed,{}));
    }else if(event.effect==2004){for(int i=0;i<20;++i){const double x=event.x+.5+(double(random())-.5)*2,y=event.y+.5+(double(random())-.5)*2,z=event.z+.5+(double(random())-.5)*2;add(make(11,x,y,z,0,0,0,{}));add(make(26,x,y,z,0,0,0,{}));}}
    else if(event.effect==2005){const int count=event.data?std::clamp(event.data,0,4000):15;const auto bounds=world.selectionBounds(event.x,event.y,event.z);
        for(int i=0;i<count;++i)add(make(21,event.x+random(),event.y+random()*std::max(0.,bounds.maxY-event.y),event.z+random(),gaussian()*.02,gaussian()*.02,gaussian()*.02,{}));}
}
SoundEvent ParticleSystem::worldEffectSound(const WorldEffectEvent& event,const Game& game) {
    SoundEvent sound;sound.x=event.x+.5;sound.y=event.y+.5;sound.z=event.z+.5;sound.volume=sound.pitch=1;
    if(event.global){
        if(event.effect!=1013&&event.effect!=1018)return {};
        const double dx=event.x-game.player.x,dy=event.y-game.player.y,dz=event.z-game.player.z,length=std::sqrt(dx*dx+dy*dy+dz*dz);
        sound.x=game.player.x;sound.y=game.player.y;sound.z=game.player.z;
        if(length>0){sound.x+=dx/length*2;sound.y+=dy/length*2;sound.z+=dz/length*2;}
        sound.name=event.effect==1013?"mob.wither.spawn":"mob.enderdragon.end";sound.volume=event.effect==1013?1:5;return sound;
    }
    auto pitch=[&](float base,float range){const float first=random(),second=random();return base+(first-second)*range;};
    switch(event.effect){
    case 1000:sound.name="random.click";break;
    case 1001:sound.name="random.click";sound.pitch=1.2f;break;
    case 1002:sound.name="random.bow";sound.pitch=1.2f;break;
    case 1003:sound.name="random.door_open";sound.pitch=random()*.1f+.9f;break;
    case 1004:sound.name="random.fizz";sound.volume=.5f;sound.pitch=pitch(2.6f,.8f);break;
    case 1005:{const char* records[]{"13","cat","blocks","chirp","far","mall","mellohi","stal","strad","ward","11","wait"};if(event.data>=2256&&event.data<=2267){sound.name=std::string("records.")+records[event.data-2256];sound.volume=4;}break;}
    case 1006:sound.name="random.door_close";sound.pitch=random()*.1f+.9f;break;
    case 1007:sound.name="mob.ghast.charge";sound.volume=10;sound.pitch=pitch(1,.2f);break;
    case 1008:case 1009:sound.name="mob.ghast.fireball";sound.volume=event.effect==1008?10:2;sound.pitch=pitch(1,.2f);break;
    case 1010:sound.name="mob.zombie.wood";sound.volume=2;sound.pitch=pitch(1,.2f);break;
    case 1011:sound.name="mob.zombie.metal";sound.volume=2;sound.pitch=pitch(1,.2f);break;
    case 1012:sound.name="mob.zombie.woodbreak";sound.volume=2;sound.pitch=pitch(1,.2f);break;
    case 1014:sound.name="mob.wither.shoot";sound.volume=2;sound.pitch=pitch(1,.2f);break;
    case 1015:sound.name="mob.bat.takeoff";sound.volume=.05f;sound.pitch=pitch(1,.2f);break;
    case 1016:sound.name="mob.zombie.infect";sound.volume=2;sound.pitch=pitch(1,.2f);break;
    case 1017:sound.name="mob.zombie.unfect";sound.volume=2;sound.pitch=pitch(1,.2f);break;
    case 1020:sound.name="random.anvil_break";sound.pitch=random()*.1f+.9f;break;
    case 1021:sound.name="random.anvil_use";sound.pitch=random()*.1f+.9f;break;
    case 1022:sound.name="random.anvil_land";sound.volume=.3f;sound.pitch=random()*.1f+.9f;break;
    case 2001:{const auto& block=registry::block(unsigned(event.data)&4095);if(block.valid){sound.name=block.breakSound;sound.volume=(block.soundVolume+1.f)*.5f;sound.pitch=block.soundPitch*.8f;}break;}
    case 2002:sound.name="game.potion.smash";sound.pitch=random()*.1f+.9f;break;
    }
    return sound;
}
void ParticleSystem::ambient(const World& world,int x,int y,int z){
    for(int n=0;n<1000;++n){
        const int bx=x+int(random()*16)-int(random()*16),by=y+int(random()*16)-int(random()*16),bz=z+int(random()*16)-int(random()*16);
        const auto state=world.getBlock(bx,by,bz);const auto id=blockId(state);
        if(id==50||id==76){double px=bx+.5,py=by+.7,pz=bz+.5;const auto meta=blockMeta(state);
            if(meta<5){py+=.22;if(meta==1)px-=.27;if(meta==2)px+=.27;if(meta==3)pz-=.27;if(meta==4)pz+=.27;}
            if(id==50){add(make(11,px,py,pz,0,0,0,{}));add(make(26,px,py,pz,0,0,0,{}));}
            else add(make(30,px,py,pz,1,0,0,{}));
        } else if(id==51){for(int k=0;k<3;++k)add(make(12,bx+random(),by+random()*.5+.5,bz+random(),0,0,0,{}));}
        else if(id==62){double px=bx+.5,py=by+random()*.375,pz=bz+.5;const int f=blockMeta(state);const double o=random()*.6-.3;
            if(f==4){px-=.52;pz+=o;}if(f==5){px+=.52;pz+=o;}if(f==2){pz-=.52;px+=o;}if(f==3){pz+=.52;px+=o;}
            add(make(11,px,py,pz,0,0,0,{}));add(make(26,px,py,pz,0,0,0,{}));
        } else if(id==55&&blockMeta(state)>0){const float power=blockMeta(state)/15.f;add(make(30,bx+.5+(random()-.5)*.2,by+.0625,bz+.5+(random()-.5)*.2,power*.6+.4,std::max(0.f,power*power*.7f-.5f),std::max(0.f,power*power*.6f-.7f),{}));}
        else if(id==90)for(int k=0;k<4;++k)add(make(24,bx+random(),by+random(),bz+random(),(random()-.5)*.5,(random()-.5)*.5,(random()-.5)*.5,{}));
        else if((id==10||id==11)&&world.getBlock(bx,by+1,bz)==0&&random()<.01f)add(make(27,bx+random(),by+1,bz+random(),0,0,0,{}));
        else if(id==130&&random()<.5f)add(make(24,bx+random(),by+random(),bz+random(),random()-.5,-random(),random()-.5,{}));
    }
}
void ParticleSystem::tick(const World& world){
    std::vector<Particle> secondary;
    for(auto& p:particles_){auto& s=p.sprite;p.px=s.x;p.py=s.y;p.pz=s.z;
        if(p.age>=p.lifetime){++p.age;continue;}
        const float progress=float(p.age)/std::max(1,p.lifetime);
        if(p.type==2){for(int n=0;n<6;++n)secondary.push_back(make(1,s.x+(random()-random())*4,s.y+(random()-random())*4,s.z+(random()-random())*4,progress,0,0,{}));++p.age;continue;}
        if(p.type==28){++p.age;continue;}
        if(p.type==24||p.type==25){float f=1-progress;
            if(p.type==24)f=1-(-progress+progress*progress*2);
            s.x=p.originX+p.vx*f;s.z=p.originZ+p.vz*f;
            s.y=p.originY+p.vy*f+(p.type==24?1-f:-progress*progress*progress*progress*1.2f);++p.age;continue;}
        if(p.type==0||p.type==11||p.type==12||(p.type>=13&&p.type<=17))p.vy+=.004;
        if(p.type==4)p.vy+=.002;
        if(p.type==27&&random()>float(p.age+1)/p.lifetime)secondary.push_back(make(11,s.x,s.y,s.z,p.vx,p.vy,p.vz,{}));
        if(p.type==32)p.vy-=.03;
        p.vy-=.04*double(p.gravity);
        if((p.type==18||p.type==19)&&p.age<40){p.vx*=.02;p.vy*=.02;p.vz*=.02;}
        bool ground=false;
        if(p.noClip){s.x+=p.vx;s.y+=p.vy;s.z+=p.vz;}
        else{
            Aabb box{s.x-.1,s.y,s.z-.1,s.x+.1,s.y+.2,s.z+.1};double dx=p.vx,dy=p.vy,dz=p.vz;
            auto collisions=world.collisions(box.addCoord(dx,dy,dz));
            for(const auto& b:collisions)dy=b.clipY(box,dy);box=box.offset(0,dy,0);
            for(const auto& b:collisions)dx=b.clipX(box,dx);box=box.offset(dx,0,0);
            for(const auto& b:collisions)dz=b.clipZ(box,dz);
            s.x+=dx;s.y+=dy;s.z+=dz;ground=dy!=p.vy&&p.vy<0;
            if(dx!=p.vx)p.vx=0;if(dy!=p.vy)p.vy=0;if(dz!=p.vz)p.vz=0;
        }
        if(p.py==s.y&&(p.type==11||p.type==12||p.type==20||p.type==23||p.type==30||p.type==34||(p.type>=13&&p.type<=17))){p.vx*=1.1;p.vz*=1.1;}
        p.vx*=p.drag;p.vy*=p.drag;p.vz*=p.drag;if(ground){p.vx*=.699999988079071;p.vz*=.699999988079071;}
        if(p.type==9||p.type==10){s.g*=.96f;s.b*=.9f;p.vy-=.019999999552965164;}
        if(p.type==4||p.type==7){const auto id=blockId(world.getBlock(floorBlock(s.x),floorBlock(s.y),floorBlock(s.z)));if(id!=8&&id!=9)p.age=p.lifetime;}
        if(p.type==18&&ground)p.age=p.lifetime;
        if((p.type==5||p.type==39)&&ground&&random()<.5f)p.age=p.lifetime;
        ++p.age;
    }
    particles_.erase(std::remove_if(particles_.begin(),particles_.end(),[](const auto& p){return p.age>p.lifetime;}),particles_.end());
    for(auto& p:secondary)add(std::move(p));
}
std::vector<ParticleSprite> ParticleSystem::frame(float partial) const{
    std::vector<ParticleSprite> result;result.reserve(particles_.size());partial=std::isfinite(partial)?std::clamp(partial,0.f,1.f):0.f;
    for(const auto& p:particles_){if(p.type==2||p.age>=p.lifetime)continue;auto s=p.sprite;
        s.x=p.px+(s.x-p.px)*partial;s.y=p.py+(s.y-p.py)*partial;s.z=p.pz+(s.z-p.pz)*partial;
        const float f=(float(p.age)+partial)/std::max(1,p.lifetime);s.size=p.scale*.1f;
        if(p.type==26)s.size*=1-f*f*.5f;
        if(p.type==27)s.size*=1-f*f;
        if(p.type==24)s.size*=1-(1-f)*(1-f);
        if(p.type==9||p.type==10||p.type==11||p.type==12||p.type==20||p.type==23||p.type==29||p.type==30||p.type==32||p.type==34)s.size*=std::clamp(f*32,0.f,1.f);
        if(p.type==6&&p.age>0)s.index=19+(60-p.lifetime+p.age-1)%4;
        if(p.type==28)s.a=std::min(1.f,2.f-f*f*2.f)*.2f;
        if(p.type==0||p.type==11||p.type==12||p.type==29||p.type==30||p.type==32)s.index=std::clamp(7-p.age*8/p.lifetime,0,7);
        if(p.type>=13&&p.type<=17)s.index=(p.type==14||p.type==17?144:128)+std::clamp(7-p.age*8/p.lifetime,0,7);
        if(p.type==1)s.index=std::min(15,int((p.age+partial)*15/p.lifetime));
        if(p.type==3){s.index=160+std::min(7,p.age*8/p.lifetime);if(p.age>p.lifetime/2)s.a=1.f-f;}
        if(p.type==18||p.type==19){s.index=p.age<40?113:112;if(p.type==19){s.g=16.f/(p.age+16);s.b=4.f/(p.age+8);}}
        result.push_back(s);
    }
    return result;
}
}
