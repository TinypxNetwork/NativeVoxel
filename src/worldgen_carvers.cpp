#include "worldgen_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mc::worldgen::detail {
namespace {
constexpr float pi=3.1415927f;
std::int64_t signedBits(std::uint64_t bits){std::int64_t value;std::memcpy(&value,&bits,8);return value;}
bool water(BlockState state){const unsigned id=blockId(state);return id==8||id==9;}
bool caveReplace(BlockState state,BlockState above){
    switch(blockId(state)){case 1:case 2:case 3:case 172:case 159:case 24:case 179:case 110:case 78:return true;
    case 12:case 13:return !water(above);default:return false;}
}
struct Limits {int minX,maxX,minY,maxY,minZ,maxZ;};
Limits limits(int cx,int cz,double x,double y,double z,double horizontal,double vertical){
    return {std::max(0,floorBlock(x-horizontal)-cx*16-1),std::min(16,floorBlock(x+horizontal)-cx*16+1),
            std::max(1,floorBlock(y-vertical)-1),std::min(248,floorBlock(y+vertical)+1),
            std::max(0,floorBlock(z-horizontal)-cz*16-1),std::min(16,floorBlock(z+horizontal)-cz*16+1)};
}
bool waterAtBoundary(const Primer& primer,const Limits& b,bool nether=false){
    bool found=false;
    for(int x=b.minX;!found&&x<b.maxX;++x)for(int z=b.minZ;!found&&z<b.maxZ;++z)for(int y=b.maxY+1;!found&&y>=b.minY-1;--y){
        if(y>=0&&y<(nether?128:256)){const auto state=primer.get(x,y,z);if(nether?(blockId(state)==10||blockId(state)==11):water(state))found=true;
            if(y!=b.minY-1&&x!=b.minX&&x!=b.maxX-1&&z!=b.minZ&&z!=b.maxZ-1)y=b.minY;}
    }
    return found;
}
float randomDrift(JavaRandom& random,float multiplier){
    // Java evaluates these three Random calls left-to-right. Keep them outside
    // arithmetic operands and function arguments whose C++ order is unspecified.
    const float a=random.nextFloat(),b=random.nextFloat(),c=random.nextFloat();return (a-b)*c*multiplier;
}

class Carver {
    Primer& primer_;
    int cx_,cz_;
    const std::vector<int>& biomes_;
    const SurfaceGenerator& surface_;
    bool nether_;
    BlockState top(int x,int z) const{return surface_.currentTop(biomes_[x+z*16]);}
public:
    Carver(Primer& primer,int cx,int cz,const std::vector<int>& biomes,const SurfaceGenerator& surface,bool nether=false):primer_(primer),cx_(cx),cz_(cz),biomes_(biomes),surface_(surface),nether_(nether){}

    void caveTunnel(std::int64_t seed,double x,double y,double z,float size,float yaw,float pitch,int step,int length,double verticalScale){
        const double centerX=double(cx_*16+8),centerZ=double(cz_*16+8);float yawVelocity=0,pitchVelocity=0;JavaRandom random(seed);
        if(length<=0){const int maximum=8*16-16;length=maximum-random.nextInt(maximum/4);}
        bool room=false;if(step==-1){step=length/2;room=true;}
        const int branch=random.nextInt(length/2)+length/4;
        const bool gentle=random.nextInt(6)==0;
        for(;step<length;++step){
            const double horizontal=1.5+double(javaSin(float(step)*pi/float(length))*size*1.0f),vertical=horizontal*verticalScale;
            const float cosPitch=javaCos(pitch),sinPitch=javaSin(pitch);
            x+=double(javaCos(yaw)*cosPitch);y+=double(sinPitch);z+=double(javaSin(yaw)*cosPitch);
            pitch=pitch*(gentle?.92f:.7f);pitch=pitch+pitchVelocity*.1f;yaw+=yawVelocity*.1f;
            pitchVelocity=pitchVelocity*.9f;yawVelocity=yawVelocity*.75f;
            pitchVelocity=pitchVelocity+randomDrift(random,2.0f);yawVelocity=yawVelocity+randomDrift(random,4.0f);
            if(!room&&step==branch&&size>1.0f&&length>0){
                const auto firstSeed=random.nextLong();const float firstSize=random.nextFloat()*.5f+.5f;
                caveTunnel(firstSeed,x,y,z,firstSize,yaw-pi/2.0f,pitch/3.0f,step,length,1.0);
                const auto secondSeed=random.nextLong();const float secondSize=random.nextFloat()*.5f+.5f;
                caveTunnel(secondSeed,x,y,z,secondSize,yaw+pi/2.0f,pitch/3.0f,step,length,1.0);return;
            }
            if(room||random.nextInt(4)!=0){
                const double dx=x-centerX,dz=z-centerZ,remaining=double(length-step),reach=double(size+2.0f+16.0f);
                if(dx*dx+dz*dz-remaining*remaining>reach*reach)return;
                if(x>=centerX-16.0-horizontal*2.0&&z>=centerZ-16.0-horizontal*2.0&&x<=centerX+16.0+horizontal*2.0&&z<=centerZ+16.0+horizontal*2.0){
                    auto b=limits(cx_,cz_,x,y,z,horizontal,vertical);if(nether_)b.maxY=std::min(b.maxY,120);
                    if(!waterAtBoundary(primer_,b,nether_)){
                        for(int bx=b.minX;bx<b.maxX;++bx){const double nx=(double(bx+cx_*16)+.5-x)/horizontal;
                            for(int bz=b.minZ;bz<b.maxZ;++bz){const double nz=(double(bz+cz_*16)+.5-z)/horizontal;bool cutTop=false;
                                if(nx*nx+nz*nz<1.0)for(int by=b.maxY;by>b.minY;--by){
                                    const double ny=(double(by-1)+.5-y)/vertical;
                                    if(ny>-.7&&nx*nx+ny*ny+nz*nz<1.0){const auto current=primer_.get(bx,by,bz),above=primer_.get(bx,by+1,bz);
                                        if(nether_){const auto id=blockId(current);if(id==87||id==3||id==2)primer_.set(bx,by,bz,0);continue;}
                                        if(blockId(current)==2||blockId(current)==110)cutTop=true;
                                        if(caveReplace(current,above)){
                                            if(by-1<10)primer_.set(bx,by,bz,blockState(11));
                                            else{primer_.set(bx,by,bz,0);
                                                if(blockId(above)==12)primer_.set(bx,by+1,bz,blockMeta(above)==1?blockState(179):blockState(24));
                                                if(cutTop&&blockId(primer_.get(bx,by-1,bz))==3)primer_.set(bx,by-1,bz,blockState(blockId(top(bx,bz))));
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        if(room)break;
                    }
                }
            }
        }
    }

    void caves(JavaRandom& random,int sourceX,int sourceZ){
        const int first=random.nextInt(nether_?10:15)+1,second=random.nextInt(first)+1;
        int count=random.nextInt(second);if(random.nextInt(nether_?5:7)!=0)count=0;
        for(int i=0;i<count;++i){
            const double x=double(sourceX*16+random.nextInt(16));double y;
            if(nether_)y=double(random.nextInt(128));else{const int yBound=random.nextInt(120)+8;y=double(random.nextInt(yBound));}
            const double z=double(sourceZ*16+random.nextInt(16));
            int paths=1;
            if(random.nextInt(4)==0){const auto roomSeed=random.nextLong();const float roomSize=1.0f+random.nextFloat()*6.0f;caveTunnel(roomSeed,x,y,z,roomSize,0,0,-1,-1,.5);paths+=random.nextInt(4);}
            for(int path=0;path<paths;++path){
                const float yaw=random.nextFloat()*pi*2.0f,pitch=(random.nextFloat()-.5f)*2.0f/8.0f;
                const float widthA=random.nextFloat(),widthB=random.nextFloat();float width=widthA*2.0f+widthB;
                if(!nether_&&random.nextInt(10)==0){const float a=random.nextFloat(),b=random.nextFloat();width*=a*b*3.0f+1.0f;}
                const auto tunnelSeed=random.nextLong();caveTunnel(tunnelSeed,x,y,z,nether_?width*2.0f:width,yaw,pitch,0,0,nether_?.5:1.0);
            }
        }
    }

    void ravineTunnel(std::int64_t seed,double x,double y,double z,float size,float yaw,float pitch,int step,int length,double verticalScale){
        JavaRandom random(seed);const double centerX=double(cx_*16+8),centerZ=double(cz_*16+8);float yawVelocity=0,pitchVelocity=0;
        if(length<=0){const int maximum=8*16-16;length=maximum-random.nextInt(maximum/4);}
        bool room=false;if(step==-1){step=length/2;room=true;}
        std::array<float,256> widthByY{};float factor=1;
        for(int y=0;y<256;++y){if(y==0||random.nextInt(3)==0){const float a=random.nextFloat(),b=random.nextFloat();factor=1.0f+a*b*1.0f;}widthByY[y]=factor*factor;}
        for(;step<length;++step){
            double horizontal=1.5+double(javaSin(float(step)*pi/float(length))*size*1.0f),vertical=horizontal*verticalScale;
            horizontal=horizontal*(double(random.nextFloat())*.25+.75);vertical=vertical*(double(random.nextFloat())*.25+.75);
            const float cosPitch=javaCos(pitch),sinPitch=javaSin(pitch);
            x+=double(javaCos(yaw)*cosPitch);y+=double(sinPitch);z+=double(javaSin(yaw)*cosPitch);
            pitch=pitch*.7f;pitch=pitch+pitchVelocity*.05f;yaw+=yawVelocity*.05f;pitchVelocity=pitchVelocity*.8f;yawVelocity=yawVelocity*.5f;
            pitchVelocity=pitchVelocity+randomDrift(random,2.0f);yawVelocity=yawVelocity+randomDrift(random,4.0f);
            if(room||random.nextInt(4)!=0){
                const double dx=x-centerX,dz=z-centerZ,remaining=double(length-step),reach=double(size+2.0f+16.0f);
                if(dx*dx+dz*dz-remaining*remaining>reach*reach)return;
                if(x>=centerX-16.0-horizontal*2.0&&z>=centerZ-16.0-horizontal*2.0&&x<=centerX+16.0+horizontal*2.0&&z<=centerZ+16.0+horizontal*2.0){
                    const auto b=limits(cx_,cz_,x,y,z,horizontal,vertical);
                    if(!waterAtBoundary(primer_,b)){
                        for(int bx=b.minX;bx<b.maxX;++bx){const double nx=(double(bx+cx_*16)+.5-x)/horizontal;
                            for(int bz=b.minZ;bz<b.maxZ;++bz){const double nz=(double(bz+cz_*16)+.5-z)/horizontal;bool cutTop=false;
                                if(nx*nx+nz*nz<1.0)for(int by=b.maxY;by>b.minY;--by){
                                    const double ny=(double(by-1)+.5-y)/vertical;
                                    if((nx*nx+nz*nz)*double(widthByY[by-1])+ny*ny/6.0<1.0){const auto current=blockId(primer_.get(bx,by,bz));
                                        if(current==2)cutTop=true;
                                        if(current==1||current==3||current==2){
                                            if(by-1<10)primer_.set(bx,by,bz,blockState(10));
                                            else{primer_.set(bx,by,bz,0);if(cutTop&&blockId(primer_.get(bx,by-1,bz))==3)primer_.set(bx,by-1,bz,top(bx,bz));}
                                        }
                                    }
                                }
                            }
                        }
                        if(room)break;
                    }
                }
            }
        }
    }
    void ravines(JavaRandom& random,int sourceX,int sourceZ){
        if(random.nextInt(50)==0){
            const double x=double(sourceX*16+random.nextInt(16));const int yBound=random.nextInt(40)+8;const double y=double(random.nextInt(yBound)+20);const double z=double(sourceZ*16+random.nextInt(16));
            const float yaw=random.nextFloat()*pi*2.0f,pitch=(random.nextFloat()-.5f)*2.0f/8.0f;
            const float a=random.nextFloat(),b=random.nextFloat(),width=(a*2.0f+b)*2.0f;
            const auto tunnelSeed=random.nextLong();ravineTunnel(tunnelSeed,x,y,z,width,yaw,pitch,0,0,3.0);
        }
    }
};
void generate(Primer& primer,std::int64_t seed,int cx,int cz,const std::vector<int>& biomes,const SurfaceGenerator& surface,bool ravines,bool nether=false){
    Carver carver(primer,cx,cz,biomes,surface,nether);JavaRandom random(seed);
    const std::uint64_t xSeed=static_cast<std::uint64_t>(random.nextLong()),zSeed=static_cast<std::uint64_t>(random.nextLong());
    for(int x=cx-8;x<=cx+8;++x)for(int z=cz-8;z<=cz+8;++z){
        const auto local=std::uint64_t(std::int64_t(x))*xSeed^std::uint64_t(std::int64_t(z))*zSeed^static_cast<std::uint64_t>(seed);random.setSeed(signedBits(local));
        if(ravines)carver.ravines(random,x,z);else carver.caves(random,x,z);
    }
}
}
void carveCaves(Primer& primer,std::int64_t seed,int cx,int cz,const std::vector<int>& biomes,const SurfaceGenerator& surface){generate(primer,seed,cx,cz,biomes,surface,false);}
void carveRavines(Primer& primer,std::int64_t seed,int cx,int cz,const std::vector<int>& biomes,const SurfaceGenerator& surface){generate(primer,seed,cx,cz,biomes,surface,true);}
void carveNetherCaves(Primer& primer,std::int64_t seed,int cx,int cz,const std::vector<int>& biomes,const SurfaceGenerator& surface){generate(primer,seed,cx,cz,biomes,surface,false,true);}
}
