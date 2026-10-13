#include "mc/entity_effects.hpp"
#include "mc/entity_visuals.hpp"
#include "mc/particles.hpp"
#include "mc/controller.hpp"
#include "mc/fov.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
unsigned checks=0;
void require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void near(double a,double b,const char* why,double tolerance=1e-6){require(std::isfinite(a)&&std::abs(a-b)<=tolerance,why);}
void packet(mc::Game& game,int id,const mc::net::Writer& writer){game.consumePacket({id,writer.data});}
mc::Entity zombie(int id){mc::Entity entity;entity.id=id;entity.kind=mc::EntityKind::Mob;entity.type=54;entity.x=entity.previousX=3;entity.y=entity.previousY=70;entity.z=entity.previousZ=2;entity.metadata[6].type=3;entity.metadata[6].number=20;return entity;}
void packets(){
    mc::Game game;game.entityId=1;game.entities[7]=zombie(7);
    packet(game,0x1a,mc::net::Writer().i32(7).i8(2));
    require(game.entities[7].hurtTime==10,"S19 hurt status starts ten-tick animation");
    require(game.soundEvents.size()==1&&game.soundEvents.back().name=="mob.zombie.hurt","S19 chooses original entity hurt sound");
    require(game.soundEvents.back().pitch>=.8f&&game.soundEvents.back().pitch<=1.2f,"Entity hurt pitch uses original range");
    game.tick({});require(game.entities[7].hurtTime==9,"Hurt animation advances on game ticks");
    packet(game,0x0b,mc::net::Writer().varInt(7).u8(1));
    require(game.entities[7].hurtTime==10&&game.soundEvents.size()==1,"S0B hurt animation does not duplicate status sound");
    for(int i=0;i<10;++i)game.tick({});require(game.entities[7].hurtTime==0,"Hurt tint expires");
    packet(game,0x1a,mc::net::Writer().i32(7).i8(3));
    require(game.entities[7].dying&&game.entities[7].metadata[6].number==0,"S19 death updates visual health");
    require(game.soundEvents.back().name=="mob.zombie.death","S19 chooses original death sound");
    for(int i=1;i<20;++i){game.tick({});require(game.entities.count(7)&&game.entities.at(7).deathTime==i,"Dying entity remains visible until its twentieth tick");}
    game.tick({});require(!game.entities.count(7)&&game.deathEvents.size()==1,"Death removes entity and emits one smoke burst at twenty ticks");
    near(game.deathEvents[0].height,1.95f,"Death burst uses entity dimensions");
    game.entities[8]=zombie(8);game.entities[8].metadata[6].number=0;game.tick({});
    require(game.entities[8].deathTime==1,"Metadata health alone can begin the death animation");
    game.entities[8].metadata[6].number=10;game.tick({});require(game.entities[8].deathTime==0,"Restored entity health cancels visual death timer");
    game.spawned=true;packet(game,0x06,mc::net::Writer().f32(18).varInt(20).f32(5));
    require(game.hurtTime==10,"Player health packet starts local hurt camera");
    packet(game,0x1a,mc::net::Writer().i32(1).i8(3));game.tick({});require(game.deathTime==1,"Local death advances camera timer");
    packet(game,0x07,mc::net::Writer().i32(0).u8(1).u8(0).string("default"));
    require(game.deathTime==0&&game.hurtTime==0&&game.deathEvents.empty(),"Respawn resets all damage and death overlays");
}
void particles(){
    mc::Game game;game.entityId=1;game.entities[7]=zombie(7);mc::ParticleSystem particles;
    particles.emitEntity(game,7,9);const auto first=particles.size();require(first>0&&particles.emitterCount()==1,"Critical packet immediately emits particles");
    game.entities[7].x=100;particles.tickEmitters(game);const auto second=particles.size();require(second>first,"Critical emitter updates next game tick");
    bool follows=false;for(const auto& p:particles.frame(1))if(p.x>90)follows=true;require(follows,"Critical emitter follows moving entity");
    particles.tickEmitters(game);require(particles.emitterCount()==0&&particles.size()>second,"Critical emitter completes three source bursts");
    const auto complete=particles.size();particles.tickEmitters(game);require(particles.size()==complete,"Completed emitters cannot leak particles");
    particles.clear();particles.entityDeath({3,70,2,.6f,1.95f});require(particles.size()==20,"Death smoke count matches EntityLivingBase");
    for(const auto& p:particles.frame(0)){require(p.x>=2.4-1e-6&&p.x<=3.6+1e-6&&p.y>=70&&p.y<=71.95&&p.z>=1.4-1e-6&&p.z<=2.6+1e-6,"Death smoke spans the source uniform entity bounds");}
    particles.clear();particles.configure(2,3,70,2);game.entities[7].x=3;particles.emitEntity(game,7,10);require(particles.size()==0,"Minimal particle setting suppresses critical sparks");
    particles.entityDeath({3,70,2,.6f,1.95f});require(particles.size()==20,"Source explosion particles ignore particle quality setting");
    particles.clear();particles.blockDestroyed(mc::blockState(1),3,70,2);require(particles.size()==64,"Block destruction remains visible with minimal particles");
    particles.clear();particles.configure(0,3,70,2);mc::WorldEffectEvent splash;splash.effect=2002;splash.x=3;splash.y=70;splash.z=2;splash.data=16389;
    particles.worldEffect(splash,game.world);require(particles.size()==108,"Splash potion creates eight shards and one hundred colored spell particles");
    const auto sound=particles.worldEffectSound(splash,game);require(sound.name=="game.potion.smash"&&sound.pitch>=.9f&&sound.pitch<=1,"Potion effect uses source smash sound and pitch");
    for(int id:{1000,1001,1002,1003,1004,1006,1007,1008,1009,1010,1011,1012,1014,1015,1016,1017,1020,1021,1022}){
        splash.effect=id;const auto s=particles.worldEffectSound(splash,game);require(!s.name.empty()&&s.volume>0&&s.pitch>0,"S28 action sound is resolved");}
}
void camera(){
    const auto water=mc::visual::mediumFog(1,0,false),respiration=mc::visual::mediumFog(1,3,false),breathing=mc::visual::mediumFog(1,0,true),lava=mc::visual::mediumFog(2,0,false);
    near(water.density,.1f,"Water exponential fog density");near(respiration.density,.01,"Respiration reduces underwater fog density");near(breathing.density,.01,"Water breathing fog density");near(lava.density,2,"Lava exponential fog density");
    near(lava.color.x,.6f,"Source lava fog red");near(breathing.color.z,.8f,"Water breathing brightens water fog");
    near(mc::visual::deathRotation(1,54,1,0),0,"Death starts upright");near(mc::visual::deathRotation(1,54,20,1),90,"Humanoids stop at ninety degrees");near(mc::visual::deathRotation(1,52,20,1),180,"Spiders stop upside down");near(mc::visual::deathRotation(1,94,20,1),0,"Squid renderer overrides corpse rolling");
    const auto rotated=mc::visual::livingOrientation(0,1,54,20,0);near(rotated.v[4],1,"Death rotation retains source clockwise handedness");near(rotated.v[5],0,"Corpse vertical axis becomes horizontal");
    const auto normal=mc::visual::hurtCamera(0,10,0,0,false,.5f),hurt=mc::visual::hurtCamera(7,10,0,0,false,.5f);
    near(normal.v[0],1,"Expired hurt effect has identity camera");require(std::abs(hurt.v[1])>.05f,"Recent hurt rotates camera");
}
void useSounds(){
    mc::Game game;game.joined=game.spawned=true;game.food=10;game.inventory[36]={260,1,0,{0}};mc::Controller control;mc::ControllerInput input;input.focused=input.useHeld=true;input.usePressed=true;
    for(int i=0;i<32;++i){control.tick(game,input);input.usePressed=false;++game.ticks;}
    unsigned eats=0;for(const auto& sound:game.soundEvents)if(sound.name=="random.eat")++eats;
    require(eats>=7&&game.inventory[36].empty(),"Eating emits original repeated action sounds and still consumes item");
    require(game.particleEvents.size()>=46,"Eating emits food shards including completion burst");
    control.reset();game.soundEvents.clear();game.inventory[36]={373,1,8193,{0}};input.usePressed=true;
    for(int i=0;i<32;++i){control.tick(game,input);input.usePressed=false;++game.ticks;}
    unsigned drinks=0;for(const auto& sound:game.soundEvents)if(sound.name=="random.drink")++drinks;
    require(drinks>=7,"Drinking emits original repeated action sounds");
}
void lineFixtures(const char* inputPath,const char* outputPath){
    std::ifstream input(inputPath,std::ios::binary);const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(input)),{});mc::net::Reader reader(data);mc::net::Writer output;const int count=reader.i32();output.i32(count);
    for(int i=0;i<count;++i){mc::FishingLinePose pose;pose.hookX=reader.f64();pose.hookY=reader.f64();pose.hookZ=reader.f64();pose.anglerX=reader.f64();pose.anglerY=reader.f64();pose.anglerZ=reader.f64();pose.yaw=reader.f32();pose.pitch=reader.f32();pose.bodyYaw=reader.f32();pose.swing=reader.f32();pose.eyeHeight=reader.f32();pose.localAngler=reader.boolean();pose.thirdPerson=reader.boolean();pose.sneaking=reader.boolean();
        for(const auto& point:mc::EntityVisuals::fishingLine(pose))for(double value:point)output.f64(value);}
    reader.requireEnd();std::ofstream file(outputPath,std::ios::binary);file.write(reinterpret_cast<const char*>(output.data.data()),std::streamsize(output.data.size()));if(!file)throw std::runtime_error("Could not write reference output");
}
}
int main(int argc,char** argv){try{if(argc==4&&std::string(argv[1])=="--line-fixtures"){lineFixtures(argv[2],argv[3]);return 0;}packets();particles();camera();useSounds();std::cout<<checks<<" entity effect behavior checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"Entity effects: "<<e.what()<<'\n';return 1;}}
