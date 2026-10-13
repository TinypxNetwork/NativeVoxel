#include "mc/platform.hpp"
#include "mc/renderer.hpp"
#include "mc/assets.hpp"
#include "mc/world.hpp"
#include "mc/gl_api.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
constexpr int width=960,height=540;
std::vector<unsigned char> pixels(){std::vector<unsigned char> image(width*height*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,image.data());return image;}
std::size_t difference(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b){std::size_t count=0;for(std::size_t i=0;i<a.size();i+=3)if(std::abs(int(a[i])-int(b[i]))+std::abs(int(a[i+1])-int(b[i+1]))+std::abs(int(a[i+2])-int(b[i+2]))>15)++count;return count;}
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
}
int main(){try{
    mc::Assets assets;std::string error;require(assets.load("assets",error),error.c_str());mc::Window window;require(window.create(width,height,L"NativeVoxel entity effect validation",true,error),error.c_str());window.setVsync(false);
    mc::Renderer renderer;require(renderer.initialize(assets,error),error.c_str());mc::World world;world.generatePreview(1);mc::Camera camera{.5,100.2,.5,0,0,70};renderer.options.distance=3;
    auto draw=[&](const std::vector<mc::RenderEntity>& entities=std::vector<mc::RenderEntity>{}){renderer.render(world,camera,width,height,0,6000,entities);return pixels();};
    auto settle=[&](){for(int i=0;i<1000;++i){window.pump();draw();if(i>10&&!renderer.stats().pendingMeshes)return;std::this_thread::sleep_for(std::chrono::milliseconds(2));}throw std::runtime_error("Effect test terrain did not settle");};
    settle();const auto normal=draw();renderer.options.firstPersonEffects=renderer.options.burning=true;const auto fire=draw();const auto fireChanged=difference(normal,fire);
    require(fireChanged>10000,"Original fire overlay is missing");require(renderer.screenshot("build/effects-fire.ppm",width,height,error),error.c_str());
    renderer.options.firstPersonEffects=false;require(draw()==normal,"Fire screen overlay must be absent in third person");renderer.options.burning=false;
    world.setBlock(0,100,0,mc::blockState(9));settle();const auto waterFog=draw();const auto center=(height/2*width+width/2)*3;
    require(waterFog[center+2]>waterFog[center]*2,"Water does not use its original blue fog");renderer.options.firstPersonEffects=true;const auto waterOverlay=draw();
    require(difference(waterFog,waterOverlay)>20000,"Underwater texture overlay is missing");require(renderer.screenshot("build/effects-water.ppm",width,height,error),error.c_str());
    renderer.options.firstPersonEffects=false;world.setBlock(0,100,0,mc::blockState(11));settle();const auto shallowLava=draw();
    require(shallowLava[center]>shallowLava[center+1]*1.8&&shallowLava[center+2]<40,"Lava close to its surface must remain orange while its texture is visible");
    // At 0.5 blocks the original density of 2 retains exp(-1) of the lava
    // surface texture. Validate dense fog several blocks inside the liquid.
    for(int y=99;y<=101;++y)for(int z=-3;z<=3;++z)for(int x=-3;x<=3;++x)world.setBlock(x,y,z,mc::blockState(11));
    settle();const auto lava=draw();require(renderer.screenshot("build/effects-lava.ppm",width,height,error),error.c_str());
    require(std::abs(int(lava[center])-153)<=3&&std::abs(int(lava[center+1])-25)<=3&&lava[center+2]<3,"Deep lava does not converge to the original dense orange fog color");
    for(int y=99;y<=101;++y)for(int z=-3;z<=3;++z)for(int x=-3;x<=3;++x)world.setBlock(x,y,z,0);
    settle();camera={.5,100,-4,0,8,70};std::vector<mc::RenderEntity> entities(1);auto& entity=entities[0];entity.id=7;entity.kind=1;entity.type=54;entity.x=.5;entity.y=98.5;entity.z=.5;entity.yaw=entity.headYaw=180;entity.width=.6f;entity.height=1.95f;
    const auto empty=draw();const auto healthy=draw(entities);require(difference(empty,healthy)>1500,"Test zombie did not render");entity.hurtTime=10;const auto hurt=draw(entities);
    std::size_t reddened=0;for(std::size_t i=0;i<hurt.size();i+=3)if(int(hurt[i])>int(healthy[i])+12&&hurt[i+1]<healthy[i+1])++reddened;
    require(reddened>1000,"Entity hurt texture color combiner is missing");require(renderer.screenshot("build/effects-hurt.ppm",width,height,error),error.c_str());
    entity.hurtTime=0;entity.deathTime=20;const auto death=draw(entities);require(difference(healthy,death)>1500,"Dying entity did not rotate onto its side");require(renderer.screenshot("build/effects-death.ppm",width,height,error),error.c_str());
    renderer.options.firstPersonEffects=true;renderer.options.burning=true;draw(entities);renderer.options.firstPersonEffects=false;renderer.options.burning=false;entity.deathTime=0;
    require(draw(entities)==healthy,"Environment overlays leaked OpenGL state into subsequent frame");GLboolean write;glGetBooleanv(GL_DEPTH_WRITEMASK,&write);require(write==GL_TRUE,"Overlay pass leaked depth mask");require(glGetError()==GL_NO_ERROR,"Effect renderer generated OpenGL error");
    std::cout<<"GPU effect smoke passed: fire="<<fireChanged<<" pixels, hurt="<<reddened<<" pixels; water/lava/death and state restoration verified\n";return 0;
}catch(const std::exception& e){std::cerr<<"GPU entity effects: "<<e.what()<<'\n';return 1;}}
