#include "mc/worldgen_biomes.hpp"
#include "mc/worldgen_noise.hpp"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <utility>

namespace mc::worldgen {
namespace {
using IntArray=std::vector<int>;
int floorPower2(int value,int power){const int divisor=1<<power;return value/divisor-(value%divisor<0?1:0);}
IntArray makeArray(int length){if(length<0||length>4*1024*1024)throw std::length_error("biome layer allocation exceeds limit");return IntArray(static_cast<std::size_t>(length));}
void copyArray(const IntArray& source,std::size_t offset,IntArray& destination,std::size_t target,std::size_t count){
    if(offset>source.size()||count>source.size()-offset||target>destination.size()||count>destination.size()-target)throw std::out_of_range("biome layer copy exceeds valid region");
    std::copy_n(source.begin()+offset,count,destination.begin()+target);
}
bool isMesa(int id){const auto* b=biomeDefinition(id);return b&&b->mesa;}
class GenLayer;
using Layer=std::shared_ptr<GenLayer>;
class GenLayer {
    std::uint64_t worldGenSeed_{},chunkSeed_{};
    static constexpr std::uint64_t multiplier=6364136223846793005ULL,addend=1442695040888963407ULL;
protected:
    Layer parent;
    std::uint64_t baseSeed_{};
    void initChunkSeed(std::int64_t x,std::int64_t z){
        chunkSeed_=worldGenSeed_;
        for(auto coordinate:{x,z,x,z}){chunkSeed_*=chunkSeed_*multiplier+addend;chunkSeed_+=static_cast<std::uint64_t>(coordinate);}
    }
    int nextInt(int bound){
        if(bound<=0)throw std::invalid_argument("invalid GenLayer random bound");
        std::int64_t signedSeed;std::memcpy(&signedSeed,&chunkSeed_,8);
        std::int64_t shifted=signedSeed/16777216;
        if(signedSeed<0&&(chunkSeed_&0xffffffULL))--shifted;
        int value=static_cast<int>(shifted%bound);if(value<0)value+=bound;
        chunkSeed_*=chunkSeed_*multiplier+addend;chunkSeed_+=worldGenSeed_;return value;
    }
    int selectRandom(std::initializer_list<int> values){return *(values.begin()+nextInt(static_cast<int>(values.size())));}
    virtual int selectModeOrRandom(int a,int b,int c,int d){
        if(b==c&&c==d)return b;
        if(a==b&&a==c)return a;
        if(a==b&&a==d)return a;
        if(a==c&&a==d)return a;
        if(a==b&&c!=d)return a;
        if(a==c&&b!=d)return a;
        if(a==d&&b!=c)return a;
        if(b==c&&a!=d)return b;
        if(b==d&&a!=c)return b;
        if(c==d&&a!=b)return c;
        return selectRandom({a,b,c,d});
    }
    static bool isBiomeOceanic(int id){return id==biome_ids::ocean||id==biome_ids::deepOcean||id==biome_ids::frozenOcean;}
    static bool biomesEqualOrMesaPlateau(int a,int b){
        if(a==b)return true;
        if(a==biome_ids::mesaPlateau_F||a==biome_ids::mesaPlateau)return b==biome_ids::mesaPlateau_F||b==biome_ids::mesaPlateau;
        const auto* first=biomeDefinition(a);const auto* second=biomeDefinition(b);return first&&second&&first->isEqualTo(second);
    }
public:
    explicit GenLayer(std::int64_t seed,Layer input={}):parent(std::move(input)),baseSeed_(static_cast<std::uint64_t>(seed)){
        for(int i=0;i<3;++i){baseSeed_*=baseSeed_*multiplier+addend;baseSeed_+=static_cast<std::uint64_t>(seed);}
    }
    virtual ~GenLayer()=default;
    virtual void initWorldGenSeed(std::int64_t seed){
        worldGenSeed_=static_cast<std::uint64_t>(seed);if(parent)parent->initWorldGenSeed(seed);
        for(int i=0;i<3;++i){worldGenSeed_*=worldGenSeed_*multiplier+addend;worldGenSeed_+=baseSeed_;}
    }
    virtual IntArray getInts(int x,int z,int width,int height)=0;
};

#include "worldgen_layers.inc"

Layer zoom(std::int64_t seed,Layer input,int count){for(int i=0;i<count;++i)input=std::make_shared<GenLayerZoom>(seed+i,input);return input;}
void validateRegion(int x,int z,int width,int height){
    if(x<-30000000||x>30000000||z<-30000000||z>30000000||width<=0||height<=0||width>1024||height>1024)throw std::invalid_argument("invalid biome query region");
}
}

struct BiomeLayers::Impl {
    Layer coarse,voronoi;
    Impl(std::int64_t seed,WorldType type){
        Layer layer=std::make_shared<GenLayerIsland>(1);
        layer=std::make_shared<GenLayerFuzzyZoom>(2000,layer);
        layer=std::make_shared<GenLayerAddIsland>(1,layer);
        layer=std::make_shared<GenLayerZoom>(2001,layer);
        for(int salt:{2,50,70})layer=std::make_shared<GenLayerAddIsland>(salt,layer);
        layer=std::make_shared<GenLayerRemoveTooMuchOcean>(2,layer);
        layer=std::make_shared<GenLayerAddSnow>(2,layer);
        layer=std::make_shared<GenLayerAddIsland>(3,layer);
        layer=std::make_shared<GenLayerEdge>(2,layer,GenLayerEdge::Mode::COOL_WARM);
        layer=std::make_shared<GenLayerEdge>(2,layer,GenLayerEdge::Mode::HEAT_ICE);
        layer=std::make_shared<GenLayerEdge>(3,layer,GenLayerEdge::Mode::SPECIAL);
        layer=std::make_shared<GenLayerZoom>(2002,layer);
        layer=std::make_shared<GenLayerZoom>(2003,layer);
        layer=std::make_shared<GenLayerAddIsland>(4,layer);
        layer=std::make_shared<GenLayerAddMushroomIsland>(5,layer);
        layer=std::make_shared<GenLayerDeepOcean>(4,layer);
        Layer riverInit=std::make_shared<GenLayerRiverInit>(100,layer);
        Layer biomes=std::make_shared<GenLayerBiome>(200,layer,type);
        biomes=zoom(1000,biomes,2);biomes=std::make_shared<GenLayerBiomeEdge>(1000,biomes);
        Layer hillNoise=zoom(1000,riverInit,2);
        Layer hills=std::make_shared<GenLayerHills>(1000,biomes,hillNoise);
        Layer river=zoom(1000,riverInit,2);river=zoom(1000,river,4);
        river=std::make_shared<GenLayerRiver>(1,river);river=std::make_shared<GenLayerSmooth>(1000,river);
        hills=std::make_shared<GenLayerRareBiome>(1001,hills);
        const int biomeSize=type==WorldType::LargeBiomes?6:4;
        for(int i=0;i<biomeSize;++i){
            hills=std::make_shared<GenLayerZoom>(1000+i,hills);
            if(i==0)hills=std::make_shared<GenLayerAddIsland>(3,hills);
            if(i==1||biomeSize==1)hills=std::make_shared<GenLayerShore>(1000,hills);
        }
        hills=std::make_shared<GenLayerSmooth>(1000,hills);
        coarse=std::make_shared<GenLayerRiverMix>(100,hills,river);
        voronoi=std::make_shared<GenLayerVoronoiZoom>(10,coarse);
        // Preserve GenLayerHills' original ownership/init behavior: its secondary
        // zoom nodes are not initialized via parent; do not recursively seed them.
        coarse->initWorldGenSeed(seed);voronoi->initWorldGenSeed(seed);
    }
};
BiomeLayers::BiomeLayers(std::int64_t seed,WorldType type):impl_(std::make_unique<Impl>(seed,type)){}
BiomeLayers::~BiomeLayers()=default;
BiomeLayers::BiomeLayers(BiomeLayers&&) noexcept=default;
BiomeLayers& BiomeLayers::operator=(BiomeLayers&&) noexcept=default;
std::vector<int> BiomeLayers::coarse(int x,int z,int width,int height){validateRegion(x,z,width,height);return impl_->coarse->getInts(x,z,width,height);}
std::vector<int> BiomeLayers::blocks(int x,int z,int width,int height){validateRegion(x,z,width,height);return impl_->voronoi->getInts(x,z,width,height);}
BiomePosition BiomeLayers::findBiomePosition(int x,int z,int range,const std::vector<int>& allowed,JavaRandom& random){
    if(range<0||range>2046||x<-29997952||x>29997952||z<-29997952||z>29997952)throw std::invalid_argument("invalid biome selection region");
    const int minX=floorPower2(x-range,2),minZ=floorPower2(z-range,2);
    const int maxX=floorPower2(x+range,2),maxZ=floorPower2(z+range,2);
    const int width=maxX-minX+1,height=maxZ-minZ+1;
    const auto biomes=coarse(minX,minZ,width,height);BiomePosition result;int replacements=0;
    for(int index=0;index<width*height;++index){
        if(std::find(allowed.begin(),allowed.end(),biomes[index])!=allowed.end()&&(!result.found||random.nextInt(replacements+1)==0)){
            result={(minX+index%width)*4,(minZ+index/width)*4,true};++replacements;
        }
    }
    return result;
}

} // namespace mc::worldgen
