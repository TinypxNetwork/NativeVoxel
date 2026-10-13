#include "mc/world.hpp"
#include "mc/registry.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
constexpr int lo=-16,side=32,height=64;
int index(int x,int y,int z){return (y*side+z-lo)*side+x-lo;}
bool inside(int x,int y,int z){return x>=lo&&x<lo+side&&z>=lo&&z<lo+side&&y>=0&&y<height;}
// Independent fixed-point oracle: rebuild every value from the remaining
// emitters and sky columns, without using the incremental invalidation queue.
void verify(const mc::World& world,const char* label){
    constexpr int dx[]{1,-1,0,0,0,0},dy[]{0,0,1,-1,0,0},dz[]{0,0,0,0,1,-1};
    for(bool sky:{false,true}){
        std::vector<unsigned char> expected(side*side*height,0),opacity(expected.size());
        std::deque<std::array<int,3>> queue;
        for(int y=0;y<height;++y)for(int z=lo;z<lo+side;++z)for(int x=lo;x<lo+side;++x){
            const auto id=mc::blockId(world.getBlock(x,y,z));const int emission=mc::registry::lightValue(id);
            int attenuation=mc::registry::block(id).lightOpacity;if(attenuation>=15&&emission)attenuation=1;
            attenuation=std::max(1,attenuation);const int at=index(x,y,z);opacity[at]=std::uint8_t(std::min(255,attenuation));
            int light=sky?0:emission;const auto* chunk=world.chunk(mc::floorDiv16(x),mc::floorDiv16(z));
            if(sky&&y>=chunk->heightMap[(z&15)*16+(x&15)])light=15;
            if(sky&&attenuation<15)for(int f=0;f<6;++f)if(!inside(x+dx[f],y+dy[f],z+dz[f]))
                light=std::max(light,int(world.getSkyLight(x+dx[f],y+dy[f],z+dz[f]))-attenuation);
            expected[at]=std::uint8_t(light);if(light)queue.push_back({x,y,z});
        }
        while(!queue.empty()){
            const auto p=queue.front();queue.pop_front();const int value=expected[index(p[0],p[1],p[2])];
            for(int f=0;f<6;++f){const int x=p[0]+dx[f],y=p[1]+dy[f],z=p[2]+dz[f];if(!inside(x,y,z))continue;
                const int at=index(x,y,z),next=value-opacity[at];if(next<=expected[at])continue;
                expected[at]=std::uint8_t(next);queue.push_back({x,y,z});}
        }
        for(int y=0;y<height;++y)for(int z=lo;z<lo+side;++z)for(int x=lo;x<lo+side;++x){
            const int actual=sky?world.getSkyLight(x,y,z):world.getBlockLight(x,y,z);
            if(actual!=expected[index(x,y,z)])throw std::runtime_error(std::string(label)+(sky?" sky":" block")+" at "+std::to_string(x)+","+std::to_string(y)+","+std::to_string(z)+": got "+std::to_string(actual)+" expected "+std::to_string(expected[index(x,y,z)]));
        }
    }
}
}
int main(){try{
    mc::World world;
    constexpr unsigned mask=15;std::vector<std::uint8_t> data(mc::World::chunkDataSize(mask,true,true),0);
    for(int z=0;z<16;++z)for(int x=0;x<16;++x)data[(z*16+x)*2]=std::uint8_t(mc::blockState(1));
    std::fill(data.begin()+4*(8192+2048),data.begin()+4*(8192+2048+2048),255);
    std::fill(data.begin()+4*(8192+2048),data.begin()+4*(8192+2048)+128,0);
    for(int cx=-1;cx<1;++cx)for(int cz=-1;cz<1;++cz)require(world.applyChunk(cx,cz,true,mask,true,data.data(),data.size()),"fixture chunk");
    verify(world,"initial packet");
    world.setBlock(0,15,0,mc::blockState(50));verify(world,"torch added");
    require(world.getBlockLight(0,15,0)==14&&world.getBlockLight(1,15,0)==13,"torch attenuation");
    world.setBlock(-4,15,0,mc::blockState(50));world.setBlock(0,15,0,0);verify(world,"one source removed");
    world.setBlock(-4,15,0,0);verify(world,"last source removed");
    for(int y=20;y<=26;++y)for(int z=-3;z<=3;++z)for(int x=-3;x<=3;++x)
        if(y==20||y==26||x==-3||x==3||z==-3||z==3)world.setBlock(x,y,z,mc::blockState(1));
    verify(world,"sealed room");require(world.getSkyLight(0,23,0)==0,"room receives stale skylight");
    const auto before=world.chunk(-1,-1)->revision;
    world.setBlock(0,26,0,0);verify(world,"roof removed");
    require(world.getSkyLight(0,23,0)==15&&world.getSkyLight(1,23,0)==14,"shaft side propagation");
    require(world.chunk(-1,-1)->revision>before,"adjacent chunk mesh was not invalidated");
    world.setBlock(0,26,0,mc::blockState(1));verify(world,"roof restored");
    world.setBlock(-1,32,-1,mc::blockState(89));verify(world,"four-chunk light source");
    world.setBlock(-1,32,-1,0);verify(world,"four-chunk source removal");
    for(int i=0;i<80;++i){world.setBlock(0,23,0,mc::blockState(50));world.setBlock(0,23,0,0);}
    verify(world,"repeated source replacement");
    world.setBlock(0,26,0,mc::blockState(9));verify(world,"water skylight opacity");
    std::cout<<"PASS: 1,441,792 block/sky samples match independent full relighting after packet load and edits\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
