#include "worldgen_internal.hpp"
#include "mc/worldgen_biome_data.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace mc::worldgen::detail {
namespace {
NoisePerlin seededPerlin(std::int64_t seed,int octaves){JavaRandom random(seed);return NoisePerlin(random,octaves);}
struct MesaCache {
    bool initialized{};
    std::int64_t previousSeed{};
    std::array<BlockState,64> bands{};
    std::unique_ptr<NoisePerlin> bandNoise,pillarNoise,roofNoise;
    void initialize(std::int64_t seed){
        if(initialized&&previousSeed==seed)return;
        bands.fill(blockState(172));JavaRandom random(seed);
        bandNoise=std::make_unique<NoisePerlin>(random,1);
        for(int y=0;y<64;++y){y+=random.nextInt(5)+1;if(y<64)bands[y]=blockState(159,1);}
        int count=random.nextInt(4)+2;
        for(int i=0;i<count;++i){const int length=random.nextInt(3)+1,start=random.nextInt(64);for(int j=0;start+j<64&&j<length;++j)bands[start+j]=blockState(159,4);}
        count=random.nextInt(4)+2;
        for(int i=0;i<count;++i){const int length=random.nextInt(3)+2,start=random.nextInt(64);for(int j=0;start+j<64&&j<length;++j)bands[start+j]=blockState(159,12);}
        count=random.nextInt(4)+2;
        for(int i=0;i<count;++i){const int length=random.nextInt(3)+1,start=random.nextInt(64);for(int j=0;start+j<64&&j<length;++j)bands[start+j]=blockState(159,14);}
        count=random.nextInt(3)+3;int y=0;
        for(int i=0;i<count;++i){y+=random.nextInt(16)+4;if(y<64){bands[y]=blockState(159,0);if(y>1&&random.nextBoolean())bands[y-1]=blockState(159,8);if(y<63&&random.nextBoolean())bands[y+1]=blockState(159,8);}}
        // Exact 1.8.9 order: pillar generators use the previously stored seed,
        // initially zero; that field is assigned the current seed afterward.
        JavaRandom pillarRandom(previousSeed);pillarNoise=std::make_unique<NoisePerlin>(pillarRandom,4);roofNoise=std::make_unique<NoisePerlin>(pillarRandom,1);
        previousSeed=seed;initialized=true;
    }
    BlockState color(int x,int y) const {
        // The original method samples x twice, not x/z.
        const int shift=static_cast<int>(std::floor(bandNoise->sample(double(x)*1.0/512.0,double(x)*1.0/512.0)*2.0+.5));
        return bands[static_cast<unsigned>(y+shift+64)%64];
    }
};
// BiomeGenBase owns process-wide biome singletons in vanilla. Preserve its Mesa
// cache/history behavior across Generator instances, serialized for safe access.
std::array<MesaCache,256> mesaCaches;
std::mutex mesaMutex;
const BiomeDescription& description(int id){const auto* p=biomeDefinition(id);return p?*p:biomeDescriptions[0];}
}

struct SurfaceGenerator::Impl {
    std::int64_t seed;
    NoisePerlin temperatureNoise,grassNoise;
    std::array<BlockState,256> tops{},fillers{};
    explicit Impl(std::int64_t value):seed(value),temperatureNoise(seededPerlin(1234,1)),grassNoise(seededPerlin(2345,1)){
        for(unsigned i=0;i<256;++i){tops[i]=biomeDescriptions[i].top;fillers[i]=biomeDescriptions[i].filler;}
    }
    float temperature(int id,int x,int y,int z) const {
        const auto& b=description(id);
        if(y>64){const float noise=static_cast<float>(temperatureNoise.sample(double(x)*1.0/8.0,double(z)*1.0/8.0)*4.0);return b.temperature-(noise+float(y)-64.0f)*.05f/30.0f;}
        return b.temperature;
    }
    void base(Primer& primer,JavaRandom& random,int id,int x,int z,double noise){
        BlockState top=tops[id],filler=fillers[id];int remaining=-1;
        const int thickness=static_cast<int>(noise/3.0+3.0+random.nextDouble()*.25);
        const int localZ=x&15,localX=z&15;
        for(int y=255;y>=0;--y){
            if(y<=random.nextInt(5))primer.set(localX,y,localZ,blockState(7));
            else{const auto block=blockId(primer.get(localX,y,localZ));
                if(block==0)remaining=-1;
                else if(block==1){
                    if(remaining==-1){
                        if(thickness<=0){top=0;filler=blockState(1);}
                        else if(y>=59&&y<=64){top=tops[id];filler=fillers[id];}
                        if(y<63&&blockId(top)==0)top=temperature(id,x,y,z)<.15f?blockState(79):blockState(9);
                        remaining=thickness;
                        if(y>=62)primer.set(localX,y,localZ,top);
                        else if(y<56-thickness){top=0;filler=blockState(1);primer.set(localX,y,localZ,blockState(13));}
                        else primer.set(localX,y,localZ,filler);
                    }else if(remaining>0){
                        --remaining;primer.set(localX,y,localZ,filler);
                        if(remaining==0&&blockId(filler)==12){remaining=random.nextInt(4)+std::max(0,y-63);filler=blockMeta(filler)==1?blockState(179):blockState(24);}
                    }
                }
            }
        }
    }
    void mesa(Primer& primer,JavaRandom& random,const BiomeDescription& biome,int x,int z,double noise){
        std::lock_guard<std::mutex> guard(mesaMutex);auto& cache=mesaCaches[biome.biomeID];cache.initialize(seed);
        double pillar=0;
        if(biome.mesaBryce){
            const int px=(x&-16)+(z&15),pz=(z&-16)+(x&15);
            const double value=std::min(std::abs(noise),cache.pillarNoise->sample(double(px)*.25,double(pz)*.25));
            if(value>0){const double roof=std::abs(cache.roofNoise->sample(double(px)*.001953125,double(pz)*.001953125));pillar=value*value*2.5;const double maximum=std::ceil(roof*50.0)+14.0;if(pillar>maximum)pillar=maximum;pillar+=64.0;}
        }
        const int localZ=x&15,localX=z&15,thickness=static_cast<int>(noise/3.0+3.0+random.nextDouble()*.25);
        BlockState top=blockState(159),filler=fillers[biome.biomeID];
        const bool plain=std::cos(noise/3.0*3.14159265358979323846)>0;
        int remaining=-1;bool redSand=false;
        for(int y=255;y>=0;--y){
            if(blockId(primer.get(localX,y,localZ))==0&&y<static_cast<int>(pillar))primer.set(localX,y,localZ,blockState(1));
            if(y<=random.nextInt(5))primer.set(localX,y,localZ,blockState(7));
            else{const auto block=blockId(primer.get(localX,y,localZ));
                if(block==0)remaining=-1;
                else if(block==1){
                    if(remaining==-1){
                        redSand=false;
                        if(thickness<=0){top=0;filler=blockState(1);}
                        else if(y>=59&&y<=64){top=blockState(159);filler=fillers[biome.biomeID];}
                        if(y<63&&blockId(top)==0)top=blockState(9);
                        remaining=thickness+std::max(0,y-63);
                        if(y<62){primer.set(localX,y,localZ,filler);if(blockId(filler)==159)primer.set(localX,y,localZ,blockState(159,1));}
                        else if(biome.mesaForest&&y>86+thickness*2)primer.set(localX,y,localZ,plain?blockState(3,1):blockState(2));
                        else if(y<=66+thickness){primer.set(localX,y,localZ,tops[biome.biomeID]);redSand=true;}
                        else{const auto color=y>=64&&y<=127?(plain?blockState(172):cache.color(x,y)):blockState(159,1);primer.set(localX,y,localZ,color);}
                    }else if(remaining>0){--remaining;primer.set(localX,y,localZ,redSand?blockState(159,1):cache.color(x,y));}
                }
            }
        }
    }
    void column(Primer& primer,JavaRandom& random,int id,int x,int z,double noise){
        const auto& b=description(id);
        if(std::strcmp(b.surfaceClass,"BiomeGenMutated")==0&&b.surfaceDelegate>=0){column(primer,random,b.surfaceDelegate,x,z,noise);return;}
        if(b.mesa){mesa(primer,random,b,x,z,noise);return;}
        if(std::strcmp(b.surfaceClass,"BiomeGenHills")==0){
            tops[id]=blockState(2);fillers[id]=blockState(3);
            if((noise<-1.0||noise>2.0)&&b.terrainVariant==2)tops[id]=fillers[id]=blockState(13);
            else if(noise>1.0&&b.terrainVariant!=1)tops[id]=fillers[id]=blockState(1);
        }else if(std::strcmp(b.surfaceClass,"BiomeGenTaiga")==0&&(b.terrainVariant==1||b.terrainVariant==2)){
            tops[id]=blockState(2);fillers[id]=blockState(3);
            if(noise>1.75)tops[id]=blockState(3,1);else if(noise>-.95)tops[id]=blockState(3,2);
        }else if(std::strcmp(b.surfaceClass,"BiomeGenSavanna_Mutated")==0){
            tops[id]=blockState(2);fillers[id]=blockState(3);
            if(noise>1.75)tops[id]=fillers[id]=blockState(1);else if(noise>-.5)tops[id]=blockState(3,1);
        }else if(std::strcmp(b.surfaceClass,"BiomeGenSwamp")==0){
            const double value=grassNoise.sample(double(x)*.25,double(z)*.25);
            if(value>0){const int localX=z&15,localZ=x&15;for(int y=255;y>=0;--y)if(blockId(primer.get(localX,y,localZ))!=0){
                if(y==62&&blockId(primer.get(localX,y,localZ))!=9){primer.set(localX,y,localZ,blockState(9));if(value<.12)primer.set(localX,y+1,localZ,blockState(111));}break;}}
        }
        base(primer,random,id,x,z,noise);
    }
};
SurfaceGenerator::SurfaceGenerator(std::int64_t seed):impl_(std::make_unique<Impl>(seed)){}
SurfaceGenerator::~SurfaceGenerator()=default;
void SurfaceGenerator::column(Primer& primer,JavaRandom& random,int biome,int x,int z,double noise){impl_->column(primer,random,biome,x,z,noise);}
float SurfaceGenerator::temperature(int biome,int x,int y,int z) const{return impl_->temperature(biome,x,y,z);}
BlockState SurfaceGenerator::currentTop(int biome) const{return biome>=0&&biome<256?impl_->tops[biome]:0;}

} // namespace mc::worldgen::detail
