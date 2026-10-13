#pragma once
#include "mc/game.hpp"
#include "mc/registry.hpp"
#include "mc/worldgen_noise.hpp"
#include <algorithm>
#include <cmath>

namespace mc::visual {

// World.isFlammableWithin(Entity's contracted collision box). Visual query
// only: this helper never changes blocks, health, or authoritative metadata.
inline bool touchesFire(const World& world,const Aabb& bounds) {
    const int minX=floorBlock(bounds.minX+.001),minY=floorBlock(bounds.minY+.001),minZ=floorBlock(bounds.minZ+.001);
    const int maxX=floorBlock(bounds.maxX-.001+1.),maxY=floorBlock(bounds.maxY-.001+1.),maxZ=floorBlock(bounds.maxZ-.001+1.);
    if(maxY<0||minY>=256)return false;
    for(int x=floorDiv16(minX);x<=floorDiv16(maxX);++x)for(int z=floorDiv16(minZ);z<=floorDiv16(maxZ);++z)
        if(!world.hasChunk(x,z))return false;
    for(int x=minX;x<maxX;++x)
        for(int y=minY;y<maxY;++y)
            for(int z=minZ;z<maxZ;++z){
                const auto id=blockId(world.getBlock(x,y,z));
                if(id==51||id==10||id==11)return true;
            }
    return false;
}

// World.isRainingAt, including Chunk's precipitation roof and the original
// BiomeGenBase temperature noise that changes mountain rain into snow.
inline bool rainReaches(const World& world,int dimension,float rainStrength,int x,int y,int z) {
    if(dimension!=0||rainStrength<=.2f)return false;
    const auto* chunk=world.chunk(floorDiv16(x),floorDiv16(z));
    if(!chunk||chunk->heightMap[(z&15)*16+(x&15)]>y)return false;
    const int id=world.getBiome(x,z),base=id>=128?id-128:id;
    const auto& biome=registry::biome(id);
    if(!biome.valid||biome.snow||base==2||base==8||base==9||base==17||(base>=35&&base<=39))return false;
    float temperature=biome.temperature;
    if(y>64){
        static const worldgen::NoisePerlin noise=[](){worldgen::JavaRandom random(1234);return worldgen::NoisePerlin(random,1);}();
        const float offset=float(noise.sample(double(x)/8.,double(z)/8.)*4.);
        temperature-=(offset+float(y)-64.f)*.05f/30.f;
    }
    if(temperature<=.15f)return false;
    for(int above=std::max(1,y);above<256;++above){
        const auto idAbove=blockId(world.getBlock(x,above,z));
        if(registry::block(idAbove).materialBlocksMovement||(idAbove>=8&&idAbove<=11))return false;
    }
    return true;
}

struct LocalFireInput {
    bool active=true,inWater=false,inLava=false,wet=false,flammable=false,disableDamage=false;
    int fireProtection=0;
};

class LocalFireVisuals {
    int fire_=0;
    void ignite(int seconds,int protection) {
        int duration=seconds*20;
        if(protection>0)duration-=int(std::floor(float(duration)*float(protection)*.15f));
        fire_=std::max(fire_,duration);
    }
public:
    void reset(){fire_=0;}
    bool burning() const{return fire_>0;}
    int remainingTicks() const{return std::max(0,fire_);}
    // Entity.onEntityUpdate -> moveEntity -> EntityPlayer.onUpdate. The
    // integrated simulation has no fire timer yet, so retain just its visual
    // lifetime here without inventing damage or modifying network state.
    void tick(const LocalFireInput& input) {
        if(!input.active){reset();return;}
        if(input.inWater)fire_=0;
        if(fire_>0)--fire_;
        if(input.inLava)ignite(15,input.fireProtection);
        const bool wet=input.wet||input.inWater;
        if(input.flammable){
            if(!wet&&++fire_==0)ignite(8,input.fireProtection);
        }else if(fire_<=0)fire_=-20; // EntityPlayer.fireResistance
        if(wet&&fire_>0)fire_=-20;
        if(input.disableDamage&&fire_>0)fire_=0;
    }
    void tick(const Game& game,int protection) {
        LocalFireInput input;input.active=!game.online&&game.spawned&&game.gameMode!=3&&!game.player.spectator;
        if(input.active){
            const auto& player=game.player;
            input.inWater=player.inWater;input.inLava=player.inLava;
            input.wet=rainReaches(game.world,game.dimension,game.rainStrength,floorBlock(player.x),floorBlock(player.y),floorBlock(player.z))||
                rainReaches(game.world,game.dimension,game.rainStrength,floorBlock(player.x),floorBlock(player.bounds().maxY),floorBlock(player.z));
            input.flammable=touchesFire(game.world,player.bounds());
            input.disableDamage=game.invulnerable||game.gameMode==1;input.fireProtection=protection;
        }
        tick(input);
    }
};
} // namespace mc::visual
