#pragma once
#include "../src/worldgen_features.hpp"
#include <cstring>
#include <ostream>
#include <stdexcept>
#include <vector>

namespace mc::worldgen::test {
inline BlockState base(int pattern,int height,int x,int y,int z){
    if(y<0||y>=256||x<-30000000||x>=30000000||z<-30000000||z>=30000000)return 0;
    const int bits=(x*31+z*17)&31;
    if(pattern==27){if(y>=128)return 0;if(y==0||y==127)return blockState(7);if(y<32)return blockState(11);if(((y>=40&&y<52)||(y>=66&&y<78)||(y>=105&&y<116))&&((x+z)&7)!=0)return 0;return blockState(87);}
    if(pattern>=19&&pattern<=26){
        if(pattern==26&&y==height&&x==0&&z==-1)return blockState(54,2);
        if(pattern==22&&y==height-1&&x==0&&z==0)return 0;
        if(pattern==23&&y==height+4&&x==0&&z==0)return 0;
        if(pattern==25&&y==height-2&&bits==0)return 0;
        if(y==height||y==height+1){if(pattern==24)return 0;if(pattern!=19&&z==0&&x<=-2)return 0;if(pattern==21&&((z==0&&x>=2)||(x==0&&(z<=-2||z>=2))))return 0;}
        return blockState(1);
    }
    if(pattern==17&&y<height-3&&(x&3)==0&&(z&3)==0)return 0;
    if(pattern==18&&y==height&&((x&15)==0||(x&15)==15)&&((z&15)==0||(z&15)==15))return blockState(9);
    if(pattern==13)return y==height?blockState(80):y<height?blockState(y==height-1&&bits<8?79:3):0;
    if(pattern==14)return y<=height?blockState(12,1):0;
    if(pattern==15&&y>height&&y<130&&((x&15)==1||(z&15)==1))return blockState(17);
    if(pattern==16){if(y>height)return 0;if((y==height-1||y==height-2)&&bits==0)return 0;return blockState(12);}
    if(pattern==3){if(y<=height||y==height+12)return blockState(87);return 0;}
    if(pattern==6)return y<=height?blockState(121):0;
    if(pattern==10)return y==height?blockState(9):y<height?blockState(3):0;
    if(pattern==11)return y==height?blockState(60):y<height?blockState(3):0;
    if(pattern==9){if(y<=height)return blockState(3,y==height?2:0);if(y<=height+4&&(x&15)==0&&(z&15)==0)return blockState(17,3);if(y==height+4&&bits<16)return blockState(18);return 0;}
    if(y>height){if(pattern==0&&y==height+1&&bits%4==0)return blockState(31,1);if(pattern==1&&y==height+1&&bits==0)return blockState(24);if(pattern==8&&y==height+2&&bits%3==0)return blockState(1);if(pattern==12&&y==height+7)return blockState(1);return 0;}
    if(pattern==5)return blockState(1);
    if(pattern==1)return blockState(y>=height-3?12:1);
    if(pattern==2){if(y==height)return blockState(bits<8?2:bits<16?8:9,bits<8?0:bits&7);if(y>=height-2)return blockState(bits<8?3:82);return blockState(1);}
    if(pattern==4){if(y==height)return bits<8?blockState(110):bits<16?blockState(3,2):bits<24?blockState(1):blockState(18,4);return blockState(3);}
    return blockState(y==height?2:y>=height-3?3:1);
}
inline std::int64_t bits(double value){std::int64_t r;std::memcpy(&r,&value,8);return r;}
inline std::uint32_t bits(float value){std::uint32_t r;std::memcpy(&r,&value,4);return r;}
inline void load(World& world,int pattern,int height,int x,int z){
    std::vector<std::uint8_t> wire;std::vector<BulkChunk> columns;
    for(int cx=floorDiv16(x)-1;cx<=floorDiv16(x);++cx)for(int cz=floorDiv16(z)-1;cz<=floorDiv16(z);++cz){
        columns.push_back({cx,cz,65535});
        for(int section=0;section<16;++section)for(int i=0;i<4096;++i){const auto state=base(pattern,height,cx*16+(i&15),section*16+(i>>8),cz*16+((i>>4)&15));wire.push_back(static_cast<std::uint8_t>(state));wire.push_back(static_cast<std::uint8_t>(state>>8));}
        wire.insert(wire.end(),16*2048,0);wire.insert(wire.end(),256,1);
    }
    std::string error;if(!world.applyBulk(columns,false,wire.data(),wire.size(),&error))throw std::runtime_error(error);
}
inline void differences(std::ostream& out,const World& world,int pattern,int height,int x,int z){
    for(int bx=x-16;bx<x+16;++bx)for(int bz=z-16;bz<z+16;++bz)for(int y=0;y<256;++y){const auto actual=world.getBlock(bx,y,bz);if(actual!=base(pattern,height,bx,y,bz))out<<"B "<<bx<<' '<<y<<' '<<bz<<' '<<actual<<'\n';}
    if(world.chunks().size()!=4)throw std::runtime_error("feature exceeded supplied test columns");
}
}
