#include "mc/particles.hpp"
#include "mc/game.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
unsigned checks=0;
void require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
void near(double a,double b,const char* message){require(std::isfinite(a)&&std::abs(a-b)<1e-6,message);}
mc::ParticleEvent event(int type){mc::ParticleEvent e;e.type=type;e.x=4;e.y=70;e.z=3;e.count=0;e.speed=1;return e;}
}
int main(){try{
    mc::World world;mc::ParticleSystem particles;auto dust=event(38);dust.offsetX=.2f;dust.offsetY=.1f;dust.offsetZ=-.3f;dust.data={5|(2<<12)};particles.spawn(dust);auto start=particles.frame(1);require(start.size()==1&&start[0].block==mc::blockState(5,2),"Protocol blockstate encoding converted for terrain debris");particles.tick(world);auto moving=particles.frame(1);near(moving[0].x-start[0].x,.2f,"Block dust keeps exact supplied X velocity");near(moving[0].y-start[0].y,.1f-.04,"Block dust applies block gravity");near(moving[0].z-start[0].z,-.3f,"Block dust keeps exact supplied Z velocity");
    particles.clear();auto barrier=event(35);particles.spawn(barrier);auto icon=particles.frame(1);require(icon.size()==1&&icon[0].itemId==166&&icon[0].jitterX<0,"Barrier requests the complete icon");near(icon[0].size,.5,"Barrier has half-block billboard radius");particles.tick(world);near(particles.frame(1)[0].y,70,"Barrier stays fixed in place");
    particles.clear();particles.spawn(event(28));auto footprint=particles.frame(0);require(footprint[0].texture==4,"Footstep uses a horizontal footprint texture");near(footprint[0].size,.125,"Footprint has vanilla size");near(footprint[0].a,.2,"Fresh footprint opacity");for(int i=0;i<180;++i)particles.tick(world);near(particles.frame(0)[0].a,(2-2*.9*.9)*.2,"Footprint fades over its final lifetime");for(int i=0;i<20;++i)particles.tick(world);require(particles.frame(0).empty(),"Footprint expires after 200 ticks");
    particles.clear();auto glyph=event(25);glyph.offsetX=2;glyph.offsetY=1;glyph.offsetZ=-3;particles.spawn(glyph);auto orbit=particles.frame(0);near(orbit[0].x,6,"Enchantment glyph starts at the outer end of its path");near(orbit[0].y,71,"Enchantment glyph starts at offset height");near(orbit[0].z,0,"Enchantment glyph starts at offset Z");
    particles.clear();particles.spawn(event(7));auto water=particles.frame(0);near(water[0].y,69.875,"Suspended water particles start below the supplied position");near(water[0].b,.7,"Suspended water particle blue tint");particles.tick(world);require(particles.frame(0).empty(),"Suspended water particle dies outside water");
    particles.clear();auto happy=event(21);particles.spawn(happy);auto aura=particles.frame(1);require(aura[0].index==82&&aura[0].r==1,"Happy villager uses white aura star");particles.tick(world);require(std::abs(particles.frame(1)[0].y-70)<.01,"Happy villager drifts as aura rather than heart");
    particles.clear();auto angry=event(20);particles.spawn(angry);near(particles.frame(0)[0].y,70.5,"Angry villager particle gets source half-block offset");
    particles.clear();auto cloud=event(29);particles.spawn(cloud);near(particles.frame(0)[0].size,0,"Cloud grows from zero using source lifetime scaling");
    particles.clear();auto invalid=event(26);invalid.offsetX=std::numeric_limits<float>::quiet_NaN();particles.spawn(invalid);require(particles.size()==0,"Nonfinite packet offsets cannot contaminate particle positions");
    for(int type=0;type<40;++type){auto e=event(type);e.data={1,0};particles.spawn(e);}for(const auto& s:particles.frame(.5f))require(std::isfinite(s.x)&&std::isfinite(s.y)&&std::isfinite(s.z)&&std::isfinite(s.size)&&std::isfinite(s.a),"All supported particle families produce finite render data");
    particles.clear();particles.blockDestroyed(mc::blockState(1),0,64,0);require(particles.size()==64,"Block destruction creates the original 4x4x4 debris grid");
    std::cout<<checks<<" particle behavior checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Particles failed: "<<e.what()<<'\n';return 1;}}
