#include "mc/world.hpp"
#include "mc/registry.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>

namespace mc {
namespace {
constexpr int dx[]{0,0,0,0,-1,1},dy[]{-1,1,0,0,0,0},dz[]{0,0,-1,1,0,0};
}
bool World::setLightValue(int x,int y,int z,bool sky,int value) {
    if(y<0||y>=256)return false;
    auto found=chunks_.find(key(floorDiv16(x),floorDiv16(z)));
    if(found==chunks_.end()||(sky&&!found->second.hasSkyLight))return false;
    auto& c=found->second;
    value=std::clamp(value,0,15);
    if((sky?c.getSkyLight(x,y,z):c.getBlockLight(x,y,z))==value)return false;
    auto& section=c.sections[y>>4];
    if(!section){
        section=std::make_unique<ChunkSection>();
        if(c.hasSkyLight)for(int ly=0;ly<16;++ly)for(int lz=0;lz<16;++lz)for(int lx=0;lx<16;++lx){
            const int i=ly*256+lz*16+lx;
            if((y&~15)+ly>=c.heightMap[lz*16+lx])section->skyLight[i>>1]|=std::uint8_t(15<<((i&1)*4));
        }
    }
    auto& data=sky?section->skyLight:section->blockLight;
    const int i=(y&15)*256+(z&15)*16+(x&15),shift=(i&1)*4;
    data[i>>1]=std::uint8_t((data[i>>1]&~(15<<shift))|(value<<shift));
    return true;
}

void World::updateLightAt(int x,int y,int z,bool sky) {
    const auto* initial=chunk(floorDiv16(x),floorDiv16(z));
    if(!initial||y<0||y>=256||(sky&&!initial->hasSkyLight))return;
    auto loaded=[&](int bx,int by,int bz){return by>=0&&by<256&&hasChunk(floorDiv16(bx),floorDiv16(bz));};
    auto read=[&](int bx,int by,int bz){return int(sky?getSkyLight(bx,by,bz):getBlockLight(bx,by,bz));};
    auto raw=[&](int bx,int by,int bz){
        const auto* c=chunk(floorDiv16(bx),floorDiv16(bz));
        if(sky&&c&&by>=c->heightMap[(bz&15)*16+(bx&15)])return 15;
        const unsigned id=blockId(getBlock(bx,by,bz));
        int value=sky?0:registry::lightValue(id),opacity=registry::block(id).lightOpacity;
        if(opacity>=15&&registry::lightValue(id)>0)opacity=1;
        opacity=std::max(1,opacity);
        if(opacity>=15)return 0;
        for(int side=0;side<6&&value<14;++side)value=std::max(value,read(bx+dx[side],by+dy[side],bz+dz[side])-opacity);
        return value;
    };
    std::array<std::pair<int,int>,25> changed{};std::size_t changedCount=0;
    auto write=[&](int bx,int by,int bz,int value){
        if(!setLightValue(bx,by,bz,sky,value))return;
        const auto ck=std::make_pair(floorDiv16(bx),floorDiv16(bz));
        if(std::find(changed.begin(),changed.begin()+changedCount,ck)==changed.begin()+changedCount&&changedCount<changed.size())changed[changedCount++]=ck;
    };
    // World.checkLightFor uses a bounded 32,768-entry queue and a Manhattan
    // radius of 17. First invalidate paths fed by removed light, then re-light
    // from surviving sources. A simple flood-fill only handles additions.
    std::array<std::uint32_t,32768> queue;
    std::size_t begin=0,end=0;
    auto encode=[&](int bx,int by,int bz,int value){return std::uint32_t(bx-x+32)|std::uint32_t(by-y+32)<<6|std::uint32_t(bz-z+32)<<12|std::uint32_t(value)<<18;};
    const int oldValue=read(x,y,z),nextValue=raw(x,y,z);
    if(nextValue>oldValue)queue[end++]=133152;
    else if(nextValue<oldValue){
        queue[end++]=133152|std::uint32_t(oldValue)<<18;
        while(begin<end){
            const auto entry=queue[begin++];
            const int bx=int(entry&63)-32+x,by=int(entry>>6&63)-32+y,bz=int(entry>>12&63)-32+z,value=entry>>18&15;
            if(!loaded(bx,by,bz)||read(bx,by,bz)!=value)continue;
            write(bx,by,bz,0);
            if(value<=0||std::abs(bx-x)+std::abs(by-y)+std::abs(bz-z)>=17)continue;
            for(int side=0;side<6;++side){
                const int nx=bx+dx[side],ny=by+dy[side],nz=bz+dz[side];
                const int opacity=std::max(1,registry::block(blockId(getBlock(nx,ny,nz))).lightOpacity);
                if(loaded(nx,ny,nz)&&read(nx,ny,nz)==value-opacity&&end<queue.size())queue[end++]=encode(nx,ny,nz,value-opacity);
            }
        }
        begin=0;
    }
    constexpr int growOrder[]{4,5,0,1,2,3};
    while(begin<end){
        const auto entry=queue[begin++];
        const int bx=int(entry&63)-32+x,by=int(entry>>6&63)-32+y,bz=int(entry>>12&63)-32+z;
        if(!loaded(bx,by,bz))continue;
        const int before=read(bx,by,bz),value=raw(bx,by,bz);
        if(before==value)continue;
        write(bx,by,bz,value);
        if(value<=before||std::abs(bx-x)+std::abs(by-y)+std::abs(bz-z)>=17||end>=queue.size()-6)continue;
        for(int side:growOrder){
            const int nx=bx+dx[side],ny=by+dy[side],nz=bz+dz[side];
            if(loaded(nx,ny,nz)&&read(nx,ny,nz)<value)queue[end++]=encode(nx,ny,nz,0);
        }
    }
    for(std::size_t i=0;i<changedCount;++i)touch(changed[i].first,changed[i].second);
}

void World::relightAfterChange(int x,int y,int z,int oldHeight,BlockState previous) {
    const auto now=getBlock(x,y,z);
    const bool opacityChanged=registry::block(blockId(previous)).lightOpacity!=registry::block(blockId(now)).lightOpacity;
    const bool sourceChanged=registry::lightValue(blockId(previous))!=registry::lightValue(blockId(now));
    if(!opacityChanged&&!sourceChanged)return;
    const auto* c=chunk(floorDiv16(x),floorDiv16(z));
    const int height=c->heightMap[(z&15)*16+(x&15)];
    if(c->hasSkyLight&&opacityChanged){
        if(height!=oldHeight){
            const int low=std::min(height,oldHeight),high=std::max(height,oldHeight);
            for(int by=low;by<high;++by)setLightValue(x,by,z,true,height<oldHeight?15:0);
            // A height change alters the entire newly covered/exposed shaft.
            // Neighbour updates also invalidate lateral paths into caves.
            for(int by=low;by<high;++by){
                for(int side=2;side<6;++side)updateLightAt(x+dx[side],by,z+dz[side],true);
                updateLightAt(x,by,z,true);
            }
        }
        updateLightAt(x,y,z,true);
        for(int side=0;side<6;++side)updateLightAt(x+dx[side],y+dy[side],z+dz[side],true);
    }
    updateLightAt(x,y,z,false);
}
} // namespace mc
