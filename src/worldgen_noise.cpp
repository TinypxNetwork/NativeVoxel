#include "mc/worldgen_noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace mc::worldgen {
namespace {
std::int32_t signed32(std::uint32_t value) { std::int32_t result; std::memcpy(&result,&value,4); return result; }
std::int64_t signed64(std::uint64_t value) { std::int64_t result; std::memcpy(&result,&value,8); return result; }
int floorInt(double value) {
    if(!std::isfinite(value)||value<=-2147483647.0||value>=2147483647.0)throw std::out_of_range("noise lattice coordinate outside supported world range");
    const int integer=static_cast<int>(value);return value<double(integer)?integer-1:integer;
}
int simplexFloor(double value) {
    if(!std::isfinite(value)||value<=-2147483647.0||value>=2147483647.0)throw std::out_of_range("simplex lattice coordinate outside supported world range");
    // Vanilla deliberately maps zero and negative integers to one less.
    return value>0.0?static_cast<int>(value):static_cast<int>(value)-1;
}
std::size_t volume(int x,int y,int z) {
    if(x<0||y<0||z<0||x>4096||y>4096||z>4096)throw std::invalid_argument("invalid noise region dimensions");
    const auto size=std::uint64_t(x)*std::uint64_t(y)*std::uint64_t(z);
    if(size>32u*1024*1024)throw std::length_error("noise region exceeds 32 million samples");
    return static_cast<std::size_t>(size);
}
void clearArray(std::vector<double>& output,std::size_t size) {
    if(output.size()<size)output.resize(size);
    std::fill(output.begin(),output.end(),0.0);
}
void validateOctaves(int count) { if(count<0||count>32)throw std::invalid_argument("invalid noise octave count"); }
}

void JavaRandom::setSeed(std::int64_t seed) {
    seed_=(static_cast<std::uint64_t>(seed)^0x5deece66dULL)&((1ULL<<48)-1);
    hasGaussian_=false;
}
std::uint32_t JavaRandom::next(unsigned bits) {
    seed_=(seed_*0x5deece66dULL+11)&((1ULL<<48)-1);
    return static_cast<std::uint32_t>(seed_>>(48-bits));
}
std::int32_t JavaRandom::nextInt() { return signed32(next(32)); }
std::int32_t JavaRandom::nextInt(std::int32_t bound) {
    if(bound<=0)throw std::invalid_argument("Random.nextInt bound must be positive");
    if((bound&-bound)==bound)return static_cast<std::int32_t>((std::uint64_t(bound)*next(31))>>31);
    std::uint32_t bits,value;
    do { bits=next(31);value=bits%static_cast<std::uint32_t>(bound); }
    while(bits-value+static_cast<std::uint32_t>(bound-1)>0x7fffffffU);
    return static_cast<std::int32_t>(value);
}
std::int64_t JavaRandom::nextLong() {
    const auto high=static_cast<std::uint64_t>(static_cast<std::int64_t>(nextInt()));
    const auto low=static_cast<std::uint64_t>(static_cast<std::int64_t>(nextInt()));
    return signed64((high<<32)+low);
}
bool JavaRandom::nextBoolean() { return next(1)!=0; }
float JavaRandom::nextFloat() { return static_cast<float>(next(24))/static_cast<float>(1u<<24); }
double JavaRandom::nextDouble() {
    const std::uint64_t high=next(26),low=next(27);
    return static_cast<double>((high<<27)+low)*0x1.0p-53;
}
double JavaRandom::nextGaussian() {
    if(hasGaussian_){hasGaussian_=false;return gaussian_;}
    double a,b,s;
    do { a=2*nextDouble()-1;b=2*nextDouble()-1;s=a*a+b*b; }while(s>=1||s==0);
    const double multiplier=std::sqrt(-2*std::log(s)/s);
    gaussian_=b*multiplier;hasGaussian_=true;return a*multiplier;
}
void JavaRandom::nextBytes(std::uint8_t* output,std::size_t size) {
    if(size&&!output)throw std::invalid_argument("null Random.nextBytes output");
    for(std::size_t offset=0;offset<size;){auto bits=static_cast<std::uint32_t>(nextInt());const auto count=std::min<std::size_t>(size-offset,4);
        for(std::size_t i=0;i<count;++i){output[offset++]=static_cast<std::uint8_t>(bits);bits>>=8;}}
}

NoiseImproved::NoiseImproved(JavaRandom& random) {
    x_=random.nextDouble()*256.0;y_=random.nextDouble()*256.0;z_=random.nextDouble()*256.0;
    for(int i=0;i<256;++i)permutation_[i]=i;
    for(int i=0;i<256;++i){const int j=random.nextInt(256-i)+i;std::swap(permutation_[i],permutation_[j]);permutation_[i+256]=permutation_[i];}
}
double NoiseImproved::lerp(double amount,double from,double to) { return from+amount*(to-from); }
double NoiseImproved::grad2(int hash,double x,double z) {
    constexpr double a[]={1,-1,1,-1,1,-1,1,-1,0,0,0,0,1,0,-1,0};
    constexpr double b[]={0,0,0,0,1,1,-1,-1,1,1,-1,-1,0,1,0,-1};
    const auto i=hash&15;return a[i]*x+b[i]*z;
}
double NoiseImproved::grad3(int hash,double x,double y,double z) {
    constexpr double a[]={1,-1,1,-1,1,-1,1,-1,0,0,0,0,1,0,-1,0};
    constexpr double b[]={1,1,-1,-1,0,0,0,0,1,-1,1,-1,1,-1,1,-1};
    constexpr double c[]={0,0,0,0,1,1,-1,-1,1,1,-1,-1,0,1,0,-1};
    const auto i=hash&15;return a[i]*x+b[i]*y+c[i]*z;
}
void NoiseImproved::add(double* output,double xOffset,double yOffset,double zOffset,
                        int xSize,int ySize,int zSize,double xScale,double yScale,double zScale,double noiseScale) const {
    if(volume(xSize,ySize,zSize)&&!output)throw std::invalid_argument("null improved-noise output");
    if(ySize==1) {
        std::size_t index=0;const double scale=1.0/noiseScale;
        for(int x=0;x<xSize;++x) {
            double dx=xOffset+double(x)*xScale+x_;const int ix=floorInt(dx),px=ix&255;dx=dx-double(ix);
            const double fadeX=dx*dx*dx*(dx*(dx*6.0-15.0)+10.0);
            for(int z=0;z<zSize;++z){
                double dz=zOffset+double(z)*zScale+z_;const int iz=floorInt(dz),pz=iz&255;dz=dz-double(iz);
                const double fadeZ=dz*dz*dz*(dz*(dz*6.0-15.0)+10.0);
                const int a=permutation_[permutation_[px]]+pz,b=permutation_[permutation_[px+1]]+pz;
                const double from=lerp(fadeX,grad2(permutation_[a],dx,dz),grad3(permutation_[b],dx-1.0,0.0,dz));
                const double to=lerp(fadeX,grad3(permutation_[a+1],dx,0.0,dz-1.0),grad3(permutation_[b+1],dx-1.0,0.0,dz-1.0));
                const double value=lerp(fadeZ,from,to);output[index++]+=value*scale;
            }
        }
    } else {
        std::size_t index=0;const double scale=1.0/noiseScale;int lastY=-1;
        double v0=0,v1=0,v2=0,v3=0;
        for(int x=0;x<xSize;++x) {
            double dx=xOffset+double(x)*xScale+x_;const int ix=floorInt(dx),px=ix&255;dx=dx-double(ix);
            const double fadeX=dx*dx*dx*(dx*(dx*6.0-15.0)+10.0);
            for(int z=0;z<zSize;++z) {
                double dz=zOffset+double(z)*zScale+z_;const int iz=floorInt(dz),pz=iz&255;dz=dz-double(iz);
                const double fadeZ=dz*dz*dz*(dz*(dz*6.0-15.0)+10.0);
                for(int y=0;y<ySize;++y) {
                    double dy=yOffset+double(y)*yScale+y_;const int iy=floorInt(dy),py=iy&255;dy=dy-double(iy);
                    const double fadeY=dy*dy*dy*(dy*(dy*6.0-15.0)+10.0);
                    // Preserve the original within-cell Y-gradient cache. Recomputing
                    // every Y sample changes vanilla terrain even if mathematically cleaner.
                    if(y==0||py!=lastY) {
                        lastY=py;const int a=permutation_[px]+py,b=permutation_[a]+pz,c=permutation_[a+1]+pz;
                        const int d=permutation_[px+1]+py,e=permutation_[d]+pz,f=permutation_[d+1]+pz;
                        v0=lerp(fadeX,grad3(permutation_[b],dx,dy,dz),grad3(permutation_[e],dx-1.0,dy,dz));
                        v1=lerp(fadeX,grad3(permutation_[c],dx,dy-1.0,dz),grad3(permutation_[f],dx-1.0,dy-1.0,dz));
                        v2=lerp(fadeX,grad3(permutation_[b+1],dx,dy,dz-1.0),grad3(permutation_[e+1],dx-1.0,dy,dz-1.0));
                        v3=lerp(fadeX,grad3(permutation_[c+1],dx,dy-1.0,dz-1.0),grad3(permutation_[f+1],dx-1.0,dy-1.0,dz-1.0));
                    }
                    const double from=lerp(fadeY,v0,v1),to=lerp(fadeY,v2,v3),value=lerp(fadeZ,from,to);
                    output[index++]+=value*scale;
                }
            }
        }
    }
}

NoiseOctaves::NoiseOctaves(JavaRandom& random,int count) {
    validateOctaves(count);octaves_.reserve(static_cast<std::size_t>(count));for(int i=0;i<count;++i)octaves_.emplace_back(random);
}
void NoiseOctaves::generate(std::vector<double>& output,int xOffset,int yOffset,int zOffset,
                            int xSize,int ySize,int zSize,double xScale,double yScale,double zScale) const {
    clearArray(output,volume(xSize,ySize,zSize));double frequency=1.0;
    for(const auto& octave:octaves_) {
        double x=double(xOffset)*frequency*xScale,y=double(yOffset)*frequency*yScale,z=double(zOffset)*frequency*zScale;
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||std::abs(x)>9e18||std::abs(z)>9e18)throw std::out_of_range("invalid octave noise coordinate");
        auto ix=static_cast<std::int64_t>(std::floor(x)),iz=static_cast<std::int64_t>(std::floor(z));
        x=x-double(ix);z=z-double(iz);ix%=16777216;iz%=16777216;x=x+double(ix);z=z+double(iz);
        octave.add(output.data(),x,y,z,xSize,ySize,zSize,xScale*frequency,yScale*frequency,zScale*frequency,frequency);
        frequency/=2.0;
    }
}
void NoiseOctaves::generate2D(std::vector<double>& output,int x,int z,int sx,int sz,double scaleX,double scaleZ,double) const {
    generate(output,x,10,z,sx,1,sz,scaleX,1.0,scaleZ);
}

NoiseSimplex::NoiseSimplex(JavaRandom& random) {
    x_=random.nextDouble()*256.0;z_=random.nextDouble()*256.0;unused_=random.nextDouble()*256.0;
    for(int i=0;i<256;++i)permutation_[i]=i;
    for(int i=0;i<256;++i){const int j=random.nextInt(256-i)+i;std::swap(permutation_[i],permutation_[j]);permutation_[i+256]=permutation_[i];}
}
double NoiseSimplex::sample(double x,double z) const {
    constexpr int gradient[12][3]={{1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},{1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},{0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}};
    const double root=std::sqrt(3.0),skew=.5*(root-1.0),unskew=(3.0-root)/6.0;
    const double s=(x+z)*skew;const int ix=simplexFloor(x+s),iz=simplexFloor(z+s);
    const double t=double(ix+iz)*unskew,originX=double(ix)-t,originZ=double(iz)-t;
    const double x0=x-originX,z0=z-originZ;const int stepX=x0>z0?1:0,stepZ=x0>z0?0:1;
    const double x1=x0-double(stepX)+unskew,z1=z0-double(stepZ)+unskew,x2=x0-1.0+2.0*unskew,z2=z0-1.0+2.0*unskew;
    const int px=ix&255,pz=iz&255;
    const int hash0=permutation_[px+permutation_[pz]]%12,hash1=permutation_[px+stepX+permutation_[pz+stepZ]]%12,hash2=permutation_[px+1+permutation_[pz+1]]%12;
    const auto contribution=[&](int hash,double dx,double dz){double a=.5-dx*dx-dz*dz;if(a<0)return 0.0;a=a*a;return a*a*(double(gradient[hash][0])*dx+double(gradient[hash][1])*dz);};
    const double a=contribution(hash0,x0,z0),b=contribution(hash1,x1,z1),c=contribution(hash2,x2,z2);
    return 70.0*(a+b+c);
}
void NoiseSimplex::add(double* output,double xOffset,double zOffset,int xSize,int zSize,double xScale,double zScale,double amplitude) const {
    if(volume(xSize,1,zSize)&&!output)throw std::invalid_argument("null simplex-noise output");
    std::size_t index=0;
    for(int z=0;z<zSize;++z){const double dz=(zOffset+double(z))*zScale+z_;
        for(int x=0;x<xSize;++x){const double dx=(xOffset+double(x))*xScale+x_;output[index++]+=sample(dx,dz)*amplitude;}}
}

NoisePerlin::NoisePerlin(JavaRandom& random,int count) {
    validateOctaves(count);octaves_.reserve(static_cast<std::size_t>(count));for(int i=0;i<count;++i)octaves_.emplace_back(random);
}
double NoisePerlin::sample(double x,double z) const {
    double value=0,frequency=1;
    for(const auto& octave:octaves_){value+=octave.sample(x*frequency,z*frequency)/frequency;frequency/=2.0;}
    return value;
}
void NoisePerlin::generate(std::vector<double>& output,double x,double z,int xSize,int zSize,double scaleX,double scaleZ,double lacunarity,double persistence) const {
    clearArray(output,volume(xSize,1,zSize));double amplitude=1,frequency=1;
    for(const auto& octave:octaves_){octave.add(output.data(),x,z,xSize,zSize,scaleX*frequency*amplitude,scaleZ*frequency*amplitude,.55/amplitude);frequency*=lacunarity;amplitude*=persistence;}
}

} // namespace mc::worldgen
