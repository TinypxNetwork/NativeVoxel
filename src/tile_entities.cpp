#include "mc/tile_entities.hpp"
#include "mc/skins.hpp"
#include "mc/world.hpp"
#include "mc/nbt.hpp"
#include <openssl/md5.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace mc {
namespace {
using Matrix=FirstPersonMatrix;
constexpr float pi=3.14159265358979323846f;
Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
Matrix multiply(const Matrix& a,const Matrix& b){Matrix r{};for(int c=0;c<4;++c)for(int y=0;y<4;++y)for(int k=0;k<4;++k)r[c*4+y]+=a[k*4+y]*b[c*4+k];return r;}
void translate(Matrix& m,float x,float y,float z){auto t=identity();t[12]=x;t[13]=y;t[14]=z;m=multiply(m,t);}
void scale(Matrix& m,float x,float y,float z){auto t=identity();t[0]=x;t[5]=y;t[10]=z;m=multiply(m,t);}
void rotate(Matrix& m,int axis,float degrees){double angle=double(degrees)*3.14159265358979323846/180.;float c=float(std::cos(angle)),s=float(std::sin(angle));auto r=identity();if(axis==0){r[5]=c;r[9]=-s;r[6]=s;r[10]=c;}else{r[0]=c;r[8]=s;r[2]=-s;r[10]=c;}m=multiply(m,r);}
struct Vec {float x,y,z;};
std::shared_ptr<const FirstPersonMesh> box(Vec start,Vec size,float u,float v,float tw,float th,float inflate=0) {
    const Vec lo{start.x-inflate,start.y-inflate,start.z-inflate},hi{start.x+size.x+inflate,start.y+size.y+inflate,start.z+size.z+inflate};
    const Vec corners[]={{lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,hi.y,lo.z},{lo.x,hi.y,lo.z},{lo.x,lo.y,hi.z},{hi.x,lo.y,hi.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z}};
    constexpr int faces[6][4]={{5,1,2,6},{0,4,7,3},{5,4,0,1},{2,3,7,6},{1,0,3,2},{4,5,6,7}};
    const Vec normals[]={{1,0,0},{-1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};
    float x=size.x,y=size.y,z=size.z;const float rect[6][4]={{u+z+x,v+z,u+z+x+z,v+z+y},{u,v+z,u+z,v+z+y},{u+z,v,u+z+x,v+z},{u+z+x,v+z,u+z+x+x,v},{u+z,v+z,u+z+x,v+z+y},{u+z+x+z,v+z,u+z+x+z+x,v+z+y}};
    auto result=std::make_shared<FirstPersonMesh>();result->vertices.reserve(36);
    for(int face=0;face<6;++face){const float pairs[4][2]={{rect[face][2],rect[face][1]},{rect[face][0],rect[face][1]},{rect[face][0],rect[face][3]},{rect[face][2],rect[face][3]}};
        for(int i:{0,1,2,0,2,3}){const auto& p=corners[faces[face][i]];const auto& n=normals[face];result->vertices.push_back({p.x/16,p.y/16,p.z/16,pairs[i][0]/tw,pairs[i][1]/th,n.x,n.y,n.z});}}
    return result;
}
struct Part {std::shared_ptr<const FirstPersonMesh> mesh;Vec pivot;};
std::array<Part,3> chestParts(bool large){float width=large?30.f:14.f,tw=large?128.f:64.f;return {{{box({0,-5,-14},{width,5,14},0,0,tw,64),{1,7,15}},{box({-1,-2,-15},{2,4,1},0,0,tw,64),{large?16.f:8.f,7,15}},{box({0,0,0},{width,10,14},0,19,tw,64),{1,6,1}}}};}
class File {
    std::ifstream in;
public:
    explicit File(const std::string& name):in(name,std::ios::binary){if(!in)throw std::runtime_error("Missing tile resource: "+name);}
    void bytes(void* value,std::size_t n){if(!in.read(static_cast<char*>(value),static_cast<std::streamsize>(n)))throw std::runtime_error("Truncated tile resource");}
    unsigned u32(){std::uint8_t b[4];bytes(b,4);return unsigned(b[0])|(unsigned(b[1])<<8)|(unsigned(b[2])<<16)|(unsigned(b[3])<<24);}
    std::string string(){unsigned n=u32();if(n>256)throw std::runtime_error("Invalid tile resource name");std::string s(n,'\0');if(n)bytes(&s[0],n);return s;}
    void magic(const char* expected,std::size_t n){char actual[8];bytes(actual,n);if(std::memcmp(actual,expected,n))throw std::runtime_error("Unsupported tile resource format");}
    void end(){if(in.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Unexpected tile resource suffix");}
};
std::shared_ptr<TileTexture> texture(const std::string& path,const std::string& key,bool repeat=false){File file(path);file.magic("NVTX",4);auto result=std::make_shared<TileTexture>();result->key=key;result->width=file.u32();result->height=file.u32();const auto count=std::uint64_t(result->width)*result->height*4;if(!result->width||!result->height||result->width>4096||result->height>4096||count>64u*1024u*1024u)throw std::runtime_error("Invalid tile image dimensions");result->pixels.resize(std::size_t(count));file.bytes(result->pixels.data(),result->pixels.size());file.end();result->repeat=repeat;return result;}
int integer(const nbt::Tag* tag,int fallback=0){if(!tag)return fallback;if(tag->type>=1&&tag->type<=4){std::uint32_t v=std::uint32_t(tag->integer);return v<0x80000000u?int(v):int(std::int64_t(v)-0x100000000ll);}if(tag->type==5||tag->type==6){if(!std::isfinite(tag->number))return 0;return int(std::clamp(std::floor(tag->number),double(std::numeric_limits<int>::min()),double(std::numeric_limits<int>::max())));}return fallback;}
int byte(const nbt::Tag* tag){int value=integer(tag)&255;return value<128?value:value-256;}
std::string string(const nbt::Tag* tag){return tag&&tag->type==8?tag->string:std::string();}
int dye(int value){return value>=0&&value<16?value:0;}
std::string uuid(const std::string& input){std::string value;for(char c:input){if(c=='-')continue;if(c>='A'&&c<='F')c=char(c-'A'+'a');if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return {};value+=c;}return value.size()==32?value:std::string();}
std::string hashedUuid(const std::string& input){unsigned char out[MD5_DIGEST_LENGTH];MD5(reinterpret_cast<const unsigned char*>(input.data()),input.size(),out);out[6]=(out[6]&15)|48;out[8]=(out[8]&63)|128;const char* h="0123456789abcdef";std::string result;for(auto b:out){result+=h[b>>4];result+=h[b&15];}return result;}
unsigned mul8(unsigned a,unsigned b){return (a*(b*65793u)+(1u<<23))>>24;}
unsigned div8(unsigned value,unsigned alpha){if(!alpha)return 0;if(value>=alpha)return 255;const auto reciprocal=((std::uint64_t(255)<<24)+alpha/2)/alpha;return unsigned((value*reciprocal+(1u<<23))>>24);}
void over(std::uint8_t* dst,const std::uint8_t* src){unsigned sa=src[3];if(!sa)return;if(sa==255){std::copy_n(src,4,dst);return;}unsigned da=mul8(dst[3],255-sa),a=sa+da;for(int c=0;c<3;++c)dst[c]=std::uint8_t(div8(mul8(src[c],sa)+mul8(dst[c],da),a));dst[3]=std::uint8_t(a);}
} // namespace

struct TileEntityVisuals::Impl {
    bool loaded=false;std::array<std::uint32_t,16> dyes{};
    struct Pattern {std::shared_ptr<const TileTexture> image;bool type6=true;};
    std::map<std::string,std::shared_ptr<const TileTexture>> textures;
    std::map<std::string,Pattern> patterns;
    struct Banner {std::shared_ptr<const TileTexture> image;std::chrono::steady_clock::time_point used;};
    std::unordered_map<std::string,Banner> banners;
    std::unordered_map<std::string,std::shared_ptr<const TileTexture>> skins;
    struct Parsed {std::vector<std::uint8_t> source;nbt::Tag root;};
    std::unordered_map<std::uint64_t,Parsed> parsed;std::size_t parsedBytes=0;
    std::array<Part,3> small=chestParts(false),large=chestParts(true);
    std::array<Part,3> banner{{{box({-10,0,-2},{20,40,1},0,0,64,64),{0,-32,0}},{box({-1,-30,-1},{2,42,2},44,0,64,64),{0,0,0}},{box({-10,-32,-1},{20,2,2},0,42,64,64),{0,0,0}}}};
    std::shared_ptr<const FirstPersonMesh> skeleton=box({-4,-8,-4},{8,8,8},0,0,64,32),human=box({-4,-8,-4},{8,8,8},0,0,64,64),hat=box({-4,-8,-4},{8,8,8},32,0,64,64,.25f);
    const auto& tex(const std::string& name)const{auto found=textures.find(name);if(found==textures.end())throw std::runtime_error("Unknown tile texture "+name);return found->second;}
    const nbt::Tag& tileData(const TileEntityPose& p) {
        static const nbt::Tag empty;if(!p.nbt||p.nbt->empty())return empty;
        if(p.nbt->size()>2u*1024u*1024u)throw std::runtime_error("Tile NBT exceeds the original 2 MiB packet budget");
        const auto key=(std::uint64_t(std::uint32_t(p.x)&0x3ffffffu)<<38)|(std::uint64_t(std::uint32_t(p.y)&0xfffu)<<26)|std::uint64_t(std::uint32_t(p.z)&0x3ffffffu);
        auto found=parsed.find(key);if(found!=parsed.end()&&found->second.source==*p.nbt)return found->second.root;
        auto root=nbt::parse(*p.nbt);
        if(parsed.size()>=1024||parsedBytes+p.nbt->size()>16u*1024u*1024u){parsed.clear();parsedBytes=0;found=parsed.end();}
        if(found!=parsed.end())parsedBytes-=found->second.source.size();
        auto& entry=parsed[key];entry.source=*p.nbt;entry.root=std::move(root);parsedBytes+=entry.source.size();return entry.root;
    }
    std::shared_ptr<const TileTexture> playerHead(const nbt::Tag& nbt,SkinCache* cache,bool& pending) {
        const auto* owner=nbt.get("Owner");std::string name,id,value,signature;bool profile=false;
        if(owner&&owner->type==10){name=string(owner->get("Name"));id=uuid(string(owner->get("Id")));profile=!name.empty()||!id.empty();
            const auto* props=owner->get("Properties");const auto* list=props?props->get("textures"):nullptr;if(list&&list->type==9&&list->subtype==10&&!list->list.empty()){value=string(list->list[0].get("Value"));signature=string(list->list[0].get("Signature"));}}
        else {name=string(nbt.get("ExtraType"));profile=!name.empty();}
        if(id.empty()&&!name.empty())id=hashedUuid("OfflinePlayer:"+name);
        auto fallback=tex(profile&&SkinCache::defaultSlim(id)?"alex":"steve");if(!cache||!profile){pending=profile&&!value.empty();return fallback;}
        std::shared_ptr<const PlayerSkin> skin;
        if(!value.empty()){const auto requestId=hashedUuid("NativeVoxelSkull:"+std::to_string(value.size())+":"+value+signature);cache->request(requestId,value,signature);skin=cache->get(requestId);if(!skin||!skin->downloaded){pending=true;return fallback;}}
        else skin=cache->get(id);
        if(!skin)return fallback;
        const auto key=skin->textureKey+":"+std::to_string(skin->revision);auto found=skins.find(key);if(found!=skins.end())return found->second;
        auto image=std::make_shared<TileTexture>();image->key=skin->textureKey;image->width=skin->image.width;image->height=skin->image.height;image->pixels=skin->image.pixels;image->revision=skin->revision;
        if(skins.size()>=256)skins.clear();
        skins[key]=image;return image;
    }
    std::shared_ptr<const TileTexture> bannerImage(const nbt::Tag& nbt) {
        int base=integer(nbt.get("Base"));std::string key="b"+std::to_string(base);std::vector<BannerMask> layers{{patterns.at("b").image,dyes[std::size_t(dye(base))],patterns.at("b").type6}};
        const auto* list=nbt.get("Patterns");if(list&&list->type==9&&list->subtype==10)for(const auto& entry:list->list){auto id=string(entry.get("Pattern"));auto found=patterns.find(id);if(found==patterns.end())continue;int color=integer(entry.get("Color"));key+=id+std::to_string(color);if(layers.size()<17)layers.push_back({found->second.image,dyes[std::size_t(dye(color))],found->second.type6});}
        const auto now=std::chrono::steady_clock::now();auto found=banners.find(key);if(found!=banners.end()){found->second.used=now;return found->second.image;}
        if(banners.size()>=256){for(auto it=banners.begin();it!=banners.end();)if(now-it->second.used>std::chrono::seconds(60))it=banners.erase(it);else ++it;if(banners.size()>=256)return {};}
        auto result=std::make_shared<TileTexture>();std::string error;if(!TileEntityVisuals::composeBanner(*tex("banner_base"),layers,*result,error))throw std::runtime_error(error);result->key="banner:"+key;banners.emplace(key,Banner{result,now});return result;
    }
};

TileEntityVisuals::TileEntityVisuals():impl_(new Impl){}
TileEntityVisuals::~TileEntityVisuals()=default;
bool TileEntityVisuals::handlesBlock(unsigned id)noexcept{return id==54||id==130||id==146||id==144||id==176||id==177;}
void TileEntityVisuals::clear(){impl_->banners.clear();impl_->skins.clear();impl_->parsed.clear();impl_->parsedBytes=0;}
void TileEntityVisuals::blockAction(ChestLidState& lid,int action,int parameter)noexcept{if(action==1)lid.users=parameter;}
void TileEntityVisuals::advanceLid(ChestLidState& lid)noexcept{lid.previous=lid.current;if((lid.users==0&&lid.current>0)||(lid.users>0&&lid.current<1)){lid.current+=lid.users>0?.1f:-.1f;if(lid.current>1)lid.current=1;if(lid.current<0)lid.current=0;}}
float TileEntityVisuals::lidRadians(const ChestLidState& lid,float partial)noexcept{float f=lid.previous+(lid.current-lid.previous)*partial;f=1-f;f=1-f*f*f;return -(f*pi/2.f);}

bool TileEntityVisuals::load(const std::string& directory,std::string& error) {
    try {
        std::unique_ptr<Impl> next(new Impl);File patterns(directory+"/runtime/tiles/patterns.bin");patterns.magic("NVTILE01",8);auto count=patterns.u32();if(count>256)throw std::runtime_error("Too many tile banner patterns");for(auto& color:next->dyes)color=patterns.u32();
        for(unsigned i=0;i<count;++i){auto id=patterns.string(),name=patterns.string();auto type=patterns.u32();if(id.empty()||id.size()>8||name.find("..")!=std::string::npos||name.rfind("banner/",0)!=0||type>1)throw std::runtime_error("Invalid banner pattern descriptor");auto image=texture(directory+"/runtime/entities/minecraft/"+name+".rgba","minecraft:entity/"+name);if(!next->patterns.emplace(id,Impl::Pattern{image,type!=0}).second)throw std::runtime_error("Duplicate banner pattern ID");}patterns.end();if(!next->patterns.count("b"))throw std::runtime_error("Missing banner base mask");
        for(const char* name:{"chest/normal","chest/normal_double","chest/trapped","chest/trapped_double","chest/christmas","chest/christmas_double","chest/ender","skeleton/skeleton","skeleton/wither_skeleton","zombie/zombie","creeper/creeper","steve","alex","banner_base"})next->textures[name]=texture(directory+"/runtime/entities/minecraft/"+name+".rgba",std::string("minecraft:entity/")+name);
        for(int i=0;i<10;++i){auto name="destroy_stage_"+std::to_string(i);next->textures[name]=texture(directory+"/runtime/tiles/"+name+".rgba","minecraft:blocks/"+name,true);}
        next->loaded=true;impl_.swap(next);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}

bool TileEntityVisuals::composeBanner(const TileTexture& base,const std::vector<BannerMask>& layers,TileTexture& output,std::string& error) {
    try {
        const std::uint64_t bytes=std::uint64_t(base.width)*base.height*4;if(!base.width||!base.height||bytes>64u*1024u*1024u||base.pixels.size()!=bytes)throw std::runtime_error("Invalid banner base image");
        TileTexture result;result.width=base.width;result.height=base.height;result.pixels.assign(base.pixels.size(),0);
        for(std::size_t p=0;p<base.pixels.size();p+=4)over(result.pixels.data()+p,base.pixels.data()+p);
        for(std::size_t layer=0;layer<layers.size()&&layer<17;++layer){const auto& mask=layers[layer];if(!mask.image||!mask.javaType6||mask.image->width!=base.width||mask.image->height!=base.height)continue;if(mask.image->pixels.size()!=bytes)throw std::runtime_error("Invalid banner mask image");
            for(std::size_t p=0;p<base.pixels.size();p+=4){const auto* pixel=mask.image->pixels.data()+p;if(!pixel[3])continue;std::uint8_t source[4];source[3]=pixel[0];
                source[0]=std::uint8_t(int(float(base.pixels[p])*float((mask.rgb>>16)&255)/255.f));source[1]=std::uint8_t(int(float(base.pixels[p+1])*float((mask.rgb>>8)&255)/255.f));source[2]=std::uint8_t(int(float(base.pixels[p+2])*float(mask.rgb&255)/255.f));over(result.pixels.data()+p,source);}}
        output=std::move(result);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}

TileEntityFrame TileEntityVisuals::build(const World& world,const TileEntityPose& p,SkinCache* skins) {
    TileEntityFrame result;const auto state=world.getBlock(p.x,p.y,p.z);const unsigned id=blockId(state);int meta=int(blockMeta(state));if(!handlesBlock(id))return result;
    result.kind=id==130?TileEntityKind::EnderChest:id==54||id==146?TileEntityKind::Chest:id==144?TileEntityKind::Skull:TileEntityKind::Banner;
    if(!impl_->loaded){result.status=TileRenderStatus::MissingData;result.diagnostic="Tile visual assets are not loaded";return result;}
    try {
        if(p.destroyStage<-1||p.destroyStage>9)throw std::runtime_error("Invalid tile destroy stage");
        Matrix uv=identity();std::shared_ptr<const TileTexture> image;
        auto add=[&](const Part& part,Matrix m,float angle=0,bool cull=true){translate(m,part.pivot.x/16,part.pivot.y/16,part.pivot.z/16);if(angle!=0)rotate(m,0,angle*(180.f/pi));result.draws.push_back({part.mesh,m,image,uv,0xffffffffu,cull});};
        if(result.kind==TileEntityKind::Chest||result.kind==TileEntityKind::EnderChest) {
            const bool ordinary=id!=130;auto adjacent=[&](int dx,int dz){return ordinary&&blockId(world.getBlock(p.x+dx,p.y,p.z+dz))==id;};
            if(adjacent(-1,0)||adjacent(0,-1)){result.status=TileRenderStatus::Hidden;return result;}const bool east=adjacent(1,0),south=adjacent(0,1),large=east||south;
            if(p.destroyStage>=0){image=impl_->tex("destroy_stage_"+std::to_string(p.destroyStage));scale(uv,large?8.f:4.f,4,1);translate(uv,.0625f,.0625f,.0625f);}
            else image=impl_->tex(id==130?"chest/ender":std::string("chest/")+(p.christmas?"christmas":id==146?"trapped":"normal")+(large?"_double":""));
            int facing=meta%6;if(facing<2)facing=2;float angle=facing==2?180.f:facing==4?90.f:facing==5?-90.f:0.f;auto m=identity();translate(m,float(p.renderX),float(p.renderY)+1,float(p.renderZ)+1);scale(m,1,-1,-1);translate(m,.5f,.5f,.5f);
            if(facing==2&&east)translate(m,1,0,0);
            if(facing==5&&south)translate(m,0,0,-1);
            rotate(m,1,angle);translate(m,-.5f,-.5f,-.5f);
            const auto& parts=large?impl_->large:impl_->small;const float lid=lidRadians(p.lid,p.partialTicks);add(parts[0],m,lid);add(parts[1],m,lid);add(parts[2],m);
        } else {
            const auto& data=impl_->tileData(p);
            if(result.kind==TileEntityKind::Skull) {
                int type=byte(data.get("SkullType")),rotation=byte(data.get("Rot"));bool human=(type==2||type==3)&&p.destroyStage<0;float yaw=float(rotation*360)/16.f;const int facing=(meta&7)%6;
                if(p.destroyStage>=0){image=impl_->tex("destroy_stage_"+std::to_string(p.destroyStage));scale(uv,4,2,1);translate(uv,.0625f,.0625f,.0625f);}
                else if(type==3)image=impl_->playerHead(data,skins,result.approximate);else image=impl_->tex(type==1?"skeleton/wither_skeleton":type==2?"zombie/zombie":type==4?"creeper/creeper":"skeleton/skeleton");
                auto m=identity();float x=float(p.renderX),y=float(p.renderY),z=float(p.renderZ);
                if(facing==1)translate(m,x+.5f,y,z+.5f);else if(facing==2)translate(m,x+.5f,y+.25f,z+.74f);else if(facing==3){translate(m,x+.5f,y+.25f,z+.26f);yaw=180;}else if(facing==4){translate(m,x+.74f,y+.25f,z+.5f);yaw=270;}else{translate(m,x+.26f,y+.25f,z+.5f);yaw=90;}
                scale(m,-1,-1,1);const float radians=yaw/(180.f/pi);rotate(m,1,radians*(180.f/pi));result.draws.push_back({human?impl_->human:impl_->skeleton,m,image,uv,0xffffffffu,false});if(human)result.draws.push_back({impl_->hat,m,image,uv,0xffffffffu,false});
            } else {
                if(data.type!=10){result.status=TileRenderStatus::MissingData;result.diagnostic="Banner NBT has not arrived";return result;}image=impl_->bannerImage(data);if(!image){result.status=TileRenderStatus::MissingData;result.diagnostic="Original 256-design banner texture cache is full";return result;}
                const float f=.6666667f;auto m=identity();bool standing=id==176;
                if(standing){translate(m,float(p.renderX)+.5f,float(p.renderY)+.75f*f,float(p.renderZ)+.5f);rotate(m,1,-float(meta*360)/16.f);}
                else{float angle=meta==2?180.f:meta==4?90.f:meta==5?-90.f:0.f;translate(m,float(p.renderX)+.5f,float(p.renderY)-.25f*f,float(p.renderZ)+.5f);rotate(m,1,-angle);translate(m,0,-.3125f,-.4375f);}
                const std::uint32_t coordinate=std::uint32_t(p.x)*7u+std::uint32_t(p.y)*9u+std::uint32_t(p.z)*13u;const auto signedCoordinate=coordinate<0x80000000u?std::int64_t(coordinate):std::int64_t(coordinate)-0x100000000ll;
                const float phase=float(signedCoordinate)+float(p.worldTime)+p.partialTicks;const float angle=(-.0125f+.01f*javaCos(phase*pi*.02f))*pi;scale(m,f,-f,-f);add(impl_->banner[0],m,angle);if(standing)add(impl_->banner[1],m);add(impl_->banner[2],m);
            }
        }
        result.status=TileRenderStatus::Drawn;return result;
    }catch(const std::exception& e){result.draws.clear();result.status=TileRenderStatus::InvalidData;result.diagnostic=e.what();return result;}
}

TileEntityFrame TileEntityVisuals::skullItem(int type,const std::vector<std::uint8_t>& itemNbt,SkinCache* skins,float yaw) {
    TileEntityFrame result;result.kind=TileEntityKind::Skull;
    if(!impl_->loaded){result.status=TileRenderStatus::MissingData;return result;}
    try {
        nbt::Tag root=nbt::parse(itemNbt),data;data.type=10;const auto* owner=root.get("SkullOwner");
        if(owner&&owner->type==10)data.compound["Owner"]=*owner;else if(owner&&owner->type==8)data.compound["ExtraType"]=*owner;
        std::shared_ptr<const TileTexture> image;bool human=type==2||type==3;
        if(type==3)image=impl_->playerHead(data,skins,result.approximate);else image=impl_->tex(type==1?"skeleton/wither_skeleton":type==2?"zombie/zombie":type==4?"creeper/creeper":"skeleton/skeleton");
        auto m=identity();translate(m,-.5f,0,-.5f);scale(m,2,2,2);translate(m,.5f,0,.5f);scale(m,-1,-1,1);rotate(m,1,(yaw/(180.f/pi))*(180.f/pi));
        result.draws.push_back({human?impl_->human:impl_->skeleton,m,image,identity(),0xffffffffu,false});if(human)result.draws.push_back({impl_->hat,m,image,identity(),0xffffffffu,false});result.status=TileRenderStatus::Drawn;
    }catch(const std::exception& e){result.status=TileRenderStatus::InvalidData;result.diagnostic=e.what();}
    return result;
}
TileEntityFrame TileEntityVisuals::builtinItem(int itemId,int metadata,const std::vector<std::uint8_t>& itemNbt,SkinCache* skins,bool christmas) {
    if(itemId==397)return skullItem(metadata,itemNbt,skins);
    TileEntityFrame result;if(itemId!=54&&itemId!=146&&itemId!=130&&itemId!=425)return result;
    result.kind=itemId==425?TileEntityKind::Banner:itemId==130?TileEntityKind::EnderChest:TileEntityKind::Chest;if(!impl_->loaded){result.status=TileRenderStatus::MissingData;return result;}
    try {
        auto m=identity();std::shared_ptr<const TileTexture> image;
        auto append=[&](const Part& part,float angle=0){auto matrix=m;translate(matrix,part.pivot.x/16,part.pivot.y/16,part.pivot.z/16);if(angle!=0)rotate(matrix,0,angle*(180.f/pi));result.draws.push_back({part.mesh,matrix,image});};
        if(itemId==425){auto root=nbt::parse(itemNbt);nbt::Tag data;data.type=10;const auto* block=root.get("BlockEntityTag");if(block&&block->type==10)data=*block;const auto* base=data.get("Base");if(!base||base->type<1||base->type>6)data.compound["Base"]=nbt::integer(3,metadata&15);image=impl_->bannerImage(data);if(!image){result.status=TileRenderStatus::MissingData;return result;}
            const float f=.6666667f;translate(m,.5f,.75f*f,.5f);scale(m,f,-f,-f);append(impl_->banner[0],(-.0125f+.01f*javaCos(0))*pi);append(impl_->banner[1]);append(impl_->banner[2]);}
        else {image=impl_->tex(itemId==130?"chest/ender":christmas?"chest/christmas":itemId==146?"chest/trapped":"chest/normal");translate(m,0,1,1);scale(m,1,-1,-1);for(const auto& part:impl_->small)append(part);}
        result.status=TileRenderStatus::Drawn;
    }catch(const std::exception& e){result.status=TileRenderStatus::InvalidData;result.diagnostic=e.what();result.draws.clear();}
    return result;
}

namespace {
std::uint64_t chestKey(int x,int y,int z){return (std::uint64_t(std::uint32_t(x)&0x3ffffffu)<<38)|(std::uint64_t(std::uint32_t(y)&4095u)<<26)|std::uint64_t(std::uint32_t(z)&0x3ffffffu);}
bool chestBlock(int id){return id==54||id==146||id==130;}
void chestAnchor(const World& world,int& x,int y,int& z,int id){if(id==130)return;if(blockId(world.getBlock(x-1,y,z))==unsigned(id))--x;else if(blockId(world.getBlock(x,y,z-1))==unsigned(id))--z;}
}
struct ChestAnimationTracker::Impl {struct Entry{ChestVisualState value;std::uint64_t serial=0;};std::map<std::uint64_t,Entry> entries;std::uint64_t lastSerial=0,lastTick=0;bool hasTick=false;};
ChestAnimationTracker::ChestAnimationTracker():impl_(new Impl){}
ChestAnimationTracker::~ChestAnimationTracker()=default;
void ChestAnimationTracker::reset(){impl_.reset(new Impl);}
bool ChestAnimationTracker::action(const World& world,int x,int y,int z,int id,int action,int parameter,std::uint64_t serial) {
    if(serial<=impl_->lastSerial)return false;impl_->lastSerial=serial;
    if(action!=1||!chestBlock(id)||blockId(world.getBlock(x,y,z))!=unsigned(id))return false;
    chestAnchor(world,x,y,z,id);auto& entry=impl_->entries[chestKey(x,y,z)];if(entry.value.blockId!=id)entry.value={x,y,z,id,{}};entry.serial=serial;TileEntityVisuals::blockAction(entry.value.lid,action,parameter);return true;
}
void ChestAnimationTracker::tick(const World& world,std::uint64_t tick) {
    if(impl_->hasTick&&tick<=impl_->lastTick)return;const auto count=impl_->hasTick?std::min<std::uint64_t>(12,tick-impl_->lastTick):1;impl_->lastTick=tick;impl_->hasTick=true;
    std::map<std::uint64_t,Impl::Entry> active;
    for(auto entry:impl_->entries){auto& e=entry.second;auto& p=e.value;if(blockId(world.getBlock(p.x,p.y,p.z))!=unsigned(p.blockId))continue;chestAnchor(world,p.x,p.y,p.z,p.blockId);auto key=chestKey(p.x,p.y,p.z);auto found=active.find(key);if(found==active.end()||found->second.serial<e.serial)active[key]=e;}
    impl_->entries.swap(active);for(auto& entry:impl_->entries)for(std::uint64_t i=0;i<count;++i)TileEntityVisuals::advanceLid(entry.second.value.lid);
}
ChestLidState ChestAnimationTracker::sample(const World& world,int x,int y,int z)const {int id=int(blockId(world.getBlock(x,y,z)));if(!chestBlock(id))return {};chestAnchor(world,x,y,z,id);auto entry=impl_->entries.find(chestKey(x,y,z));return entry!=impl_->entries.end()&&entry->second.value.blockId==id?entry->second.value.lid:ChestLidState{};}
std::vector<ChestVisualState> ChestAnimationTracker::states()const {std::vector<ChestVisualState> result;for(const auto& pair:impl_->entries)result.push_back(pair.second.value);return result;}
} // namespace mc
