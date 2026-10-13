#pragma once
#include "mc/world.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace mc {
struct ParticleEvent;
struct Entity;
struct EntityDeathEvent;
struct WorldEffectEvent;
struct SoundEvent;
class Game;
struct ParticleSprite {
    double x=0,y=0,z=0;
    float size=.1f,r=1,g=1,b=1,a=1,jitterX=0,jitterY=0;
    int texture=0,index=0; // 0 particle sheet, 1 terrain, 2 item icon, 3 explosion, 4 horizontal footprint
    int itemId=-1,metadata=0;
    BlockState block=0;
    bool fullBright=false;
};
// Display effects only: this RNG and movement never touch player/server physics.
class ParticleSystem {
public:
    void clear();
    void configure(int quality,double viewerX,double viewerY,double viewerZ);
    void spawn(const ParticleEvent&);
    void blockHit(const World&,int x,int y,int z,int face);
    void blockDestroyed(BlockState,int x,int y,int z);
    void emitEntity(const Game&,int entityId,int type);
    void tickEmitters(const Game&);
    void entityDeath(const EntityDeathEvent&);
    void worldEffect(const WorldEffectEvent&,const World&);
    SoundEvent worldEffectSound(const WorldEffectEvent&,const Game&);
    void tick(const World&);
    void ambient(const World&,int x,int y,int z);
    std::vector<ParticleSprite> frame(float partialTicks) const;
    std::size_t size() const noexcept{return particles_.size();}
    std::size_t emitterCount() const noexcept{return emitters_.size();}
    static constexpr std::size_t limit=16000;
private:
    struct Particle {
        ParticleSprite sprite;
        double px=0,py=0,pz=0,vx=0,vy=0,vz=0,originX=0,originY=0,originZ=0;
        int type=0,age=0,lifetime=20;
        float gravity=0,drag=.98f,scale=1;
        bool noClip=false;
    };
    std::vector<Particle> particles_;
    struct Emitter {int entityId=0,type=9,age=0;double x=0,y=0,z=0;float width=.6f,height=1.8f;};
    std::vector<Emitter> emitters_;
    std::uint64_t random_=0x5deece66dull^189u;
    int quality_=0;
    bool haveViewer_=false;
    double viewerX_=0,viewerY_=0,viewerZ_=0;
    float random();
    double gaussian();
    Particle make(int type,double x,double y,double z,double vx,double vy,double vz,const std::vector<int>& data);
    void add(Particle,bool ignoreRange=false);
    void emitBurst(Emitter&);
};
}
