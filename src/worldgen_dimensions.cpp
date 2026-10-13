#include "worldgen_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace mc::worldgen::detail {
namespace {
std::int64_t signedBits(std::uint64_t bits){std::int64_t value;std::memcpy(&value,&bits,8);return value;}
double blend(double a,double b,double t){return t<0?a:t>1?b:a+(b-a)*t;}
}
struct DimensionGenerator::Impl {
    Dimension dimension;
    JavaRandom random;
    NoiseOctaves lower,upper,main,four,five,six,seven;
    std::vector<double> density,lo,hi,mix,sand,gravel,exclusive,unusedA,unusedB;
    Impl(std::int64_t seed,Dimension d):dimension(d),random(seed),lower(random,16),upper(random,16),main(random,8),
        four(random,d==Dimension::Nether?4:10),five(random,d==Dimension::Nether?4:16),six(random,d==Dimension::Nether?10:0),seven(random,d==Dimension::Nether?16:0){
        if(d!=Dimension::Nether&&d!=Dimension::End)throw std::invalid_argument("invalid alternate dimension generator");
    }
    void netherDensity(int x,int z){
        constexpr double horizontal=684.412,vertical=2053.236;
        six.generate(unusedA,x,0,z,5,1,5,1.0,0.0,1.0);seven.generate(unusedB,x,0,z,5,1,5,100.0,0.0,100.0);
        main.generate(mix,x,0,z,5,17,5,horizontal/80.0,vertical/60.0,horizontal/80.0);
        lower.generate(lo,x,0,z,5,17,5,horizontal,vertical,horizontal);upper.generate(hi,x,0,z,5,17,5,horizontal,vertical,horizontal);
        std::array<double,17> verticalShape{};
        for(int y=0;y<17;++y){verticalShape[y]=std::cos(double(y)*3.14159265358979323846*6.0/17.0)*2.0;double edge=double(y);if(y>17/2)edge=double(17-1-y);if(edge<4){edge=4.0-edge;verticalShape[y]-=edge*edge*edge*10.0;}}
        density.resize(5*17*5);int index=0;
        for(int gx=0;gx<5;++gx)for(int gz=0;gz<5;++gz)for(int y=0;y<17;++y){
            double value=blend(lo[index]/512.0,hi[index]/512.0,(mix[index]/10.0+1.0)/2.0)-verticalShape[y];
            if(y>13){const double fade=double(float(y-13)/3.0f);value=value*(1.0-fade)+-10.0*fade;}
            density[index++]=value;
        }
    }
    void endDensity(int x,int z){
        double horizontal=684.412;constexpr double vertical=684.412;
        four.generate2D(unusedA,x,z,3,3,1.121,1.121,.5);five.generate2D(unusedB,x,z,3,3,200,200,.5);horizontal=horizontal*2.0;
        main.generate(mix,x,0,z,3,33,3,horizontal/80.0,vertical/160.0,horizontal/80.0);
        lower.generate(lo,x,0,z,3,33,3,horizontal,vertical,horizontal);upper.generate(hi,x,0,z,3,33,3,horizontal,vertical,horizontal);
        density.resize(3*33*3);int index=0;
        for(int gx=0;gx<3;++gx)for(int gz=0;gz<3;++gz){
            const float px=float(gx+x)/1.0f,pz=float(gz+z)/1.0f;float island=100.0f-static_cast<float>(std::sqrt(double(px*px+pz*pz)))*8.0f;
            if(island>80.0f)island=80.0f;
            if(island<-100.0f)island=-100.0f;
            for(int y=0;y<33;++y){
                double value=blend(lo[index]/512.0,hi[index]/512.0,(mix[index]/10.0+1.0)/2.0);value=value-8.0;value=value+double(island);
                if(y>14){const double fade=std::clamp(double(float(y-14)/64.0f),0.0,1.0);value=value*(1.0-fade)+-3000.0*fade;}
                if(y<8){const double fade=double(float(8-y)/(8.0f-1.0f));value=value*(1.0-fade)+-30.0*fade;}
                density[index++]=value;
            }
        }
    }
    void terrain(Primer& primer,int cx,int cz){
        const bool nether=dimension==Dimension::Nether;const int grid=nether?5:3,vertical=nether?17:33,subX=nether?4:8,subY=nether?8:4;
        if(nether)netherDensity(cx*4,cz*4);else endDensity(cx*2,cz*2);
        const double yStep=1.0/double(subY),xStep=1.0/double(subX);
        for(int gx=0;gx<grid-1;++gx)for(int gz=0;gz<grid-1;++gz)for(int gy=0;gy<vertical-1;++gy){
            const int a=(gx*grid+gz)*vertical+gy,b=(gx*grid+gz+1)*vertical+gy,c=((gx+1)*grid+gz)*vertical+gy,d=((gx+1)*grid+gz+1)*vertical+gy;
            double v0=density[a],v1=density[b],v2=density[c],v3=density[d];
            const double dy0=(density[a+1]-v0)*yStep,dy1=(density[b+1]-v1)*yStep,dy2=(density[c+1]-v2)*yStep,dy3=(density[d+1]-v3)*yStep;
            for(int sy=0;sy<subY;++sy){
                double left=v0,right=v1;const double dx0=(v2-v0)*xStep,dx1=(v3-v1)*xStep;
                for(int sx=0;sx<subX;++sx){double value=left;const double dz=(right-left)*xStep;
                    for(int sz=0;sz<subX;++sz){BlockState state=nether&&gy*subY+sy<32?blockState(11):0;if(value>0.0)state=blockState(nether?87:121);primer.set(gx*subX+sx,gy*subY+sy,gz*subX+sz,state);value+=dz;}
                    left+=dx0;right+=dx1;
                }
                v0+=dy0;v1+=dy1;v2+=dy2;v3+=dy3;
            }
        }
    }
    void netherSurface(Primer& primer,int cx,int cz){
        four.generate(sand,cx*16,cz*16,0,16,16,1,.03125,.03125,1.0);
        four.generate(gravel,cx*16,109,cz*16,16,1,16,.03125,1.0,.03125);
        five.generate(exclusive,cx*16,cz*16,0,16,16,1,.0625,.0625,.0625);
        for(int z=0;z<16;++z)for(int x=0;x<16;++x){
            const bool soul=sand[z+x*16]+random.nextDouble()*.2>0.0,loose=gravel[z+x*16]+random.nextDouble()*.2>0.0;
            const int thickness=static_cast<int>(exclusive[z+x*16]/3.0+3.0+random.nextDouble()*.25);int remaining=-1;BlockState top=blockState(87),filler=blockState(87);
            for(int y=127;y>=0;--y){
                // The second draw is conditional in vanilla near the roof.
                if(y<127-random.nextInt(5)&&y>random.nextInt(5)){
                    const auto id=blockId(primer.get(x,y,z));
                    if(id!=0){if(id==87){
                        if(remaining==-1){
                            if(thickness<=0){top=0;filler=blockState(87);}
                            else if(y>=60&&y<=65){top=filler=blockState(87);if(loose){top=blockState(13);filler=blockState(87);}if(soul)top=filler=blockState(88);}
                            if(y<64&&blockId(top)==0)top=blockState(11);
                            remaining=thickness;primer.set(x,y,z,y>=63?top:filler);
                        }else if(remaining>0){--remaining;primer.set(x,y,z,filler);}
                    }}else remaining=-1;
                }else primer.set(x,y,z,blockState(7));
            }
        }
    }
    void endSurface(Primer& primer){
        for(int x=0;x<16;++x)for(int z=0;z<16;++z){int remaining=-1;
            for(int y=127;y>=0;--y){const auto id=blockId(primer.get(x,y,z));if(id==0)remaining=-1;else if(id==1){if(remaining==-1){remaining=1;primer.set(x,y,z,blockState(121));}else if(remaining>0){--remaining;primer.set(x,y,z,blockState(121));}}}
        }
    }
    void generate(Primer& primer,int cx,int cz,ChunkStage stage){
        random.setSeed(signedBits(std::uint64_t(std::int64_t(cx))*341873128712ULL+std::uint64_t(std::int64_t(cz))*132897987541ULL));
        terrain(primer,cx,cz);if(stage>=ChunkStage::Surface){if(dimension==Dimension::Nether)netherSurface(primer,cx,cz);else endSurface(primer);}
    }
};
DimensionGenerator::DimensionGenerator(std::int64_t seed,Dimension dimension):impl_(std::make_unique<Impl>(seed,dimension)){}
DimensionGenerator::~DimensionGenerator()=default;
void DimensionGenerator::generate(Primer& primer,int cx,int cz,ChunkStage stage){impl_->generate(primer,cx,cz,stage);}
}
