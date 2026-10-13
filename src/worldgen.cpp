#include "mc/worldgen.hpp"
#include "mc/worldgen_biomes.hpp"
#include "mc/worldgen_noise.hpp"
#include "mc/registry.hpp"
#include "worldgen_internal.hpp"
#include "worldgen_features.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mc::worldgen {
namespace {
using detail::Primer;
const BiomeDescription& biomeOrOcean(int id){const auto* value=biomeDefinition(id);return value?*value:biomeDescriptions[0];}
std::int64_t signedBits(std::uint64_t value){std::int64_t result;std::memcpy(&result,&value,8);return result;}
double lerpClamp(double a,double b,double fraction){return fraction<0?a:fraction>1?b:a+(b-a)*fraction;}

bool commit(World& world,int cx,int cz,const Primer& primer,const std::vector<int>& biomes,bool sky,std::string* error){
    std::array<bool,16> present{};std::uint16_t mask=0;int highest=0;
    for(int x=0;x<16;++x)for(int z=0;z<16;++z)for(int y=0;y<256;++y)if(blockId(primer.get(x,y,z))!=0){present[y>>4]=true;highest=std::max(highest,(y>>4)*16);}
    std::array<std::array<std::uint8_t,2048>,16> skylight{};
    if(sky)for(int x=0;x<16;++x)for(int z=0;z<16;++z){
        int light=15,y=highest+15;
        while(true){int opacity=registry::block(blockId(primer.get(x,y,z))).lightOpacity;if(opacity==0&&light!=15)opacity=1;light-=opacity;
            if(light>0&&present[y>>4]){const unsigned index=((y&15)<<8)|(z<<4)|x;skylight[y>>4][index>>1]|=static_cast<std::uint8_t>(light<<((index&1)*4));}
            --y;if(y<=0||light<=0)break;
        }
    }
    for(int section=0;section<16;++section)if(present[section])mask|=static_cast<std::uint16_t>(1u<<section);
    std::vector<std::uint8_t> wire;wire.reserve(World::chunkDataSize(mask,sky,true));
    for(int section=0;section<16;++section)if(present[section])for(unsigned i=0;i<4096;++i){const auto state=primer.get(int(i&15),section*16+int(i>>8),int((i>>4)&15));wire.push_back(static_cast<std::uint8_t>(state));wire.push_back(static_cast<std::uint8_t>(state>>8));}
    for(int section=0;section<16;++section)if(present[section])wire.insert(wire.end(),2048,0);
    if(sky)for(int section=0;section<16;++section)if(present[section])wire.insert(wire.end(),skylight[section].begin(),skylight[section].end());
    for(int biome:biomes)wire.push_back(static_cast<std::uint8_t>(biome));
    return world.applyBulk({{cx,cz,mask}},sky,wire.data(),wire.size(),error);
}
}

struct Generator::Impl {
    std::int64_t worldSeed;
    Settings config;
    JavaRandom random;
    NoiseOctaves lower,upper,main;
    NoisePerlin surface;
    NoiseOctaves unusedScale,depth,mobNoise;
    BiomeLayers layers;
    detail::SurfaceGenerator surfaces;
    std::unique_ptr<detail::DimensionGenerator> alternate;
    std::array<float,25> parabolic{};
    std::array<double,825> density{};
    std::vector<double> depthValues,mainValues,lowerValues,upperValues,surfaceValues;

    Impl(std::int64_t seed,Settings settings):worldSeed(seed),config(settings),random(seed),
        lower(random,16),upper(random,16),main(random,8),surface(random,4),unusedScale(random,10),depth(random,16),mobNoise(random,8),layers(seed,settings.worldType),surfaces(seed){
        for(int x=-2;x<=2;++x)for(int z=-2;z<=2;++z){const float value=float(x*x+z*z)+.2f;parabolic[x+2+(z+2)*5]=10.0f/static_cast<float>(std::sqrt(static_cast<double>(value)));}
        if(config.dimension!=Dimension::Overworld)alternate=std::make_unique<detail::DimensionGenerator>(seed,config.dimension);
    }

    void densityField(int x,int z,const std::vector<int>& biomes){
        depth.generate2D(depthValues,x,z,5,5,200.0,200.0,.5);
        const float coordinateScale=684.412f,heightScale=684.412f;
        main.generate(mainValues,x,0,z,5,33,5,double(coordinateScale/80.0f),double(heightScale/160.0f),double(coordinateScale/80.0f));
        lower.generate(lowerValues,x,0,z,5,33,5,double(coordinateScale),double(heightScale),double(coordinateScale));
        upper.generate(upperValues,x,0,z,5,33,5,double(coordinateScale),double(heightScale),double(coordinateScale));
        int noiseIndex=0,depthIndex=0;
        for(int gx=0;gx<5;++gx)for(int gz=0;gz<5;++gz){
            float variation=0,height=0,weightSum=0;
            const auto& center=biomeOrOcean(biomes[gx+2+(gz+2)*10]);
            for(int dx=-2;dx<=2;++dx)for(int dz=-2;dz<=2;++dz){
                const auto& neighbor=biomeOrOcean(biomes[gx+dx+2+(gz+dz+2)*10]);
                float baseHeight=0.0f+neighbor.height*1.0f,baseVariation=0.0f+neighbor.variation*1.0f;
                if(config.worldType==WorldType::Amplified&&baseHeight>0.0f){baseHeight=1.0f+baseHeight*2.0f;baseVariation=1.0f+baseVariation*4.0f;}
                float weight=parabolic[dx+2+(dz+2)*5]/(baseHeight+2.0f);
                if(neighbor.height>center.height)weight/=2.0f;
                variation+=baseVariation*weight;height+=baseHeight*weight;weightSum+=weight;
            }
            variation=variation/weightSum;height=height/weightSum;
            variation=variation*.9f+.1f;height=(height*4.0f-1.0f)/8.0f;
            double depthValue=depthValues[depthIndex]/8000.0;
            if(depthValue<0.0)depthValue=-depthValue*.3;
            depthValue=depthValue*3.0-2.0;
            if(depthValue<0.0){depthValue=depthValue/2.0;if(depthValue<-1.0)depthValue=-1.0;depthValue=depthValue/1.4;depthValue=depthValue/2.0;}
            else{if(depthValue>1.0)depthValue=1.0;depthValue=depthValue/8.0;}
            ++depthIndex;
            double heightShift=double(height),verticalScale=double(variation);
            heightShift=heightShift+depthValue*.2;heightShift=heightShift*double(8.5f)/8.0;
            const double targetY=double(8.5f)+heightShift*4.0;
            for(int gy=0;gy<33;++gy){
                double slope=(double(gy)-targetY)*double(12.0f)*128.0/256.0/verticalScale;if(slope<0.0)slope*=4.0;
                const double lo=lowerValues[noiseIndex]/double(512.0f),hi=upperValues[noiseIndex]/double(512.0f),mix=(mainValues[noiseIndex]/10.0+1.0)/2.0;
                double value=lerpClamp(lo,hi,mix)-slope;
                if(gy>29){const double fade=double(float(gy-29)/3.0f);value=value*(1.0-fade)+-10.0*fade;}
                density[noiseIndex++]=value;
            }
        }
    }

    void terrain(Primer& primer,int cx,int cz){
        const auto biomes=layers.coarse(cx*4-2,cz*4-2,10,10);densityField(cx*4,cz*4,biomes);
        for(int gx=0;gx<4;++gx){const int row=gx*5,nextRow=(gx+1)*5;
            for(int gz=0;gz<4;++gz){const int a=(row+gz)*33,b=(row+gz+1)*33,c=(nextRow+gz)*33,d=(nextRow+gz+1)*33;
                for(int gy=0;gy<32;++gy){
                    double v0=density[a+gy],v1=density[b+gy],v2=density[c+gy],v3=density[d+gy];
                    const double dy0=(density[a+gy+1]-v0)*.125,dy1=(density[b+gy+1]-v1)*.125,dy2=(density[c+gy+1]-v2)*.125,dy3=(density[d+gy+1]-v3)*.125;
                    for(int sy=0;sy<8;++sy){
                        double left=v0,right=v1;const double dx0=(v2-v0)*.25,dx1=(v3-v1)*.25;
                        for(int sx=0;sx<4;++sx){const double dz=(right-left)*.25;double value=left-dz;
                            for(int sz=0;sz<4;++sz){value+=dz;if(value>0.0)primer.set(gx*4+sx,gy*8+sy,gz*4+sz,blockState(1));else if(gy*8+sy<63)primer.set(gx*4+sx,gy*8+sy,gz*4+sz,blockState(9));}
                            left+=dx0;right+=dx1;
                        }
                        v0+=dy0;v1+=dy1;v2+=dy2;v3+=dy3;
                    }
                }
            }
        }
    }
    void replaceSurface(Primer& primer,int cx,int cz,const std::vector<int>& biomes){
        surface.generate(surfaceValues,double(cx*16),double(cz*16),16,16,.03125*2.0,.03125*2.0,1.0);
        for(int x=0;x<16;++x)for(int z=0;z<16;++z)surfaces.column(primer,random,biomes[z+x*16],cx*16+x,cz*16+z,surfaceValues[z+x*16]);
    }
};

Generator::Generator(std::int64_t seed,Settings settings):impl_(std::make_unique<Impl>(seed,settings)){}
Generator::~Generator()=default;
Generator::Generator(Generator&&) noexcept=default;
Generator& Generator::operator=(Generator&&) noexcept=default;
std::int64_t Generator::seed() const noexcept{return impl_->worldSeed;}
const Settings& Generator::settings() const noexcept{return impl_->config;}
bool Generator::generateChunk(World& destination,int cx,int cz,std::string* error){
    if(error)error->clear();
    try{
        if(cx<-1875000||cx>=1875000||cz<-1875000||cz>=1875000)throw std::invalid_argument("generation coordinate exceeds vanilla world bounds");
        if(impl_->config.stage>ChunkStage::Carved&&impl_->config.mapFeatures&&impl_->config.dimension!=Dimension::End)throw std::runtime_error("requested structure-generation stage is not implemented yet");
        if(impl_->alternate){
            Primer primer;impl_->alternate->generate(primer,cx,cz,impl_->config.stage);
            const std::vector<int> biomes(256,impl_->config.dimension==Dimension::Nether?biome_ids::hell:biome_ids::sky);
            if(impl_->config.dimension==Dimension::Nether&&impl_->config.stage>=ChunkStage::Carved)detail::carveNetherCaves(primer,impl_->worldSeed,cx,cz,biomes,impl_->surfaces);
            return commit(destination,cx,cz,primer,biomes,false,error);
        }
        impl_->random.setSeed(signedBits(std::uint64_t(std::int64_t(cx))*341873128712ULL+std::uint64_t(std::int64_t(cz))*132897987541ULL));
        Primer primer;impl_->terrain(primer,cx,cz);const auto biomes=impl_->layers.blocks(cx*16,cz*16,16,16);
        if(impl_->config.stage>=ChunkStage::Surface)impl_->replaceSurface(primer,cx,cz,biomes);
        if(impl_->config.stage>=ChunkStage::Carved){detail::carveCaves(primer,impl_->worldSeed,cx,cz,biomes,impl_->surfaces);detail::carveRavines(primer,impl_->worldSeed,cx,cz,biomes,impl_->surfaces);}
        return commit(destination,cx,cz,primer,biomes,true,error);
    }catch(const std::exception& ex){if(error)*error=ex.what();return false;}
}
bool Generator::populate(World&,int,int,std::string* error){if(error)*error="population requires an explicit world Random and entity-effect output; use the context overload";return false;}
bool Generator::populate(World& world,int cx,int cz,JavaRandom& worldRandom,PopulationOutput& output,std::string* error){
    if(error)error->clear();
    try{
        if(cx<-1875000||cx>=1875000||cz<-1875000||cz>=1875000)throw std::invalid_argument("population coordinate exceeds vanilla world bounds");
        if(impl_->config.dimension!=Dimension::End)throw std::runtime_error("population for this dimension is not implemented yet");
        for(int x=cx;x<=cx+1;++x)for(int z=cz;z<=cz+1;++z)
            if(x<1875000&&z<1875000&&!world.hasChunk(x,z))throw std::runtime_error("population requires the target and its three positive-axis neighboring chunks");
        detail::populateEnd(world,worldRandom,cx,cz,output);return true;
    }catch(const std::exception& ex){if(error)*error=ex.what();return false;}
}
int Generator::biomeAt(int x,int z){
    if(impl_->config.dimension==Dimension::Nether)return biome_ids::hell;
    if(impl_->config.dimension==Dimension::End)return biome_ids::sky;
    return impl_->layers.blocks(x,z,1,1).front();
}
SpawnCandidate Generator::findSpawn(const std::function<bool(int,int)>& canCoordinateBeSpawn){
    if(impl_->config.dimension==Dimension::Nether)return {0,64,0,true,true,0};
    if(impl_->config.dimension==Dimension::End)return {0,50,0,true,true,0};
    if(!canCoordinateBeSpawn)throw std::invalid_argument("spawn selection requires a ground predicate");
    JavaRandom random(impl_->worldSeed);
    const auto biome=impl_->layers.findBiomePosition(0,0,256,{biome_ids::forest,biome_ids::plains,biome_ids::taiga,biome_ids::taigaHills,biome_ids::forestHills,biome_ids::jungle,biome_ids::jungleHills},random);
    SpawnCandidate result{biome.found?biome.x:0,64,biome.found?biome.z:0,true,false,0};
    while(!(result.suitableGround=canCoordinateBeSpawn(result.x,result.z))){
        const int dx1=random.nextInt(64),dx2=random.nextInt(64);result.x+=dx1-dx2;
        const int dz1=random.nextInt(64),dz2=random.nextInt(64);result.z+=dz1-dz2;
        if(++result.attempts==1000)break;
    }
    return result;
}
SpawnCandidate Generator::findSpawn(){
    World generated;
    return findSpawn([&](int x,int z){
        if(!generated.hasChunk(floorDiv16(x),floorDiv16(z))){
            std::string error;if(!generateChunk(generated,floorDiv16(x),floorDiv16(z),&error))throw std::runtime_error(error);
        }
        int y=63;while(y<255&&blockId(generated.getBlock(x,y+1,z))!=0)++y;
        return blockId(generated.getBlock(x,y,z))==2;
    });
}

} // namespace mc::worldgen
