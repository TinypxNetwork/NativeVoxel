#include "mc/tile_entities.hpp"
#include "mc/world.hpp"
#include "mc/nbt.hpp"
#include "mc/skins.hpp"
#include <openssl/sha.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
unsigned checks=0;
float maximumError=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
void near(float actual,float expected,const std::string& message){float error=std::abs(actual-expected);maximumError=std::max(maximumError,error);require(std::isfinite(actual)&&error<=2e-6f,message+": native="+std::to_string(actual)+", Java="+std::to_string(expected));}
struct Reference {
    std::ifstream in;
    Reference(const std::string& path,const char* magic):in(path,std::ios::binary){char bytes[8];require(bool(in.read(bytes,8))&&!std::memcmp(bytes,magic,8),"Invalid reference "+path);}
    unsigned u32(){unsigned result=0;for(int i=0;i<4;++i){int b=in.get();if(b<0)throw std::runtime_error("Truncated reference");result=(result<<8)|unsigned(b);}return result;}
    int i32(){auto v=u32();return v<0x80000000u?int(v):int(std::int64_t(v)-0x100000000ll);}
    std::int64_t i64(){std::uint64_t hi=u32(),lo=u32(),bits=(hi<<32)|lo;std::int64_t value;std::memcpy(&value,&bits,8);return value;}
    float f32(){auto bits=u32();float value;std::memcpy(&value,&bits,4);return value;}
    bool boolean(){int value=in.get();if(value<0)throw std::runtime_error("Truncated reference boolean");return value!=0;}
    std::string string(){unsigned n=u32();std::string out(n,'\0');if(n&&!in.read(&out[0],n))throw std::runtime_error("Truncated reference string");return out;}
    std::vector<std::uint8_t> bytes(std::size_t n){std::vector<std::uint8_t> out(n);if(n&&!in.read(reinterpret_cast<char*>(out.data()),std::streamsize(n)))throw std::runtime_error("Truncated reference image");return out;}
};
void bannerReference(const std::string& root) {
    Reference reference(root+"/tools/reference/banner_reference.bin","NVBANNR1");unsigned images=reference.u32(),cases=reference.u32();std::vector<std::shared_ptr<const mc::TileTexture>> textures;std::vector<bool> types;
    mc::TileEntityVisuals runtime;std::string loadError;require(runtime.load(root+"/assets",loadError),loadError);mc::World world;world.setBlock(0,64,0,mc::blockState(176));mc::TileEntityPose pose;pose.y=64;
    for(unsigned i=0;i<images;++i){auto t=std::make_shared<mc::TileTexture>();t->key=reference.string();t->width=reference.u32();t->height=reference.u32();types.push_back(reference.u32()!=0);t->pixels=reference.bytes(std::size_t(t->width)*t->height*4);textures.push_back(t);}
    for(unsigned sample=0;sample<cases;++sample){auto name=reference.string();unsigned base=reference.u32(),count=reference.u32();std::vector<mc::BannerMask> masks;for(unsigned i=0;i<count;++i){auto index=reference.u32(),color=reference.u32();masks.push_back({textures.at(index),color,types.at(index)});}
        auto expected=reference.bytes(textures.at(base)->pixels.size());mc::TileTexture actual;std::string error;require(mc::TileEntityVisuals::composeBanner(*textures.at(base),masks,actual,error),error);require(actual.pixels.size()==expected.size(),"Banner image size");std::size_t differences=0,first=0;
        for(std::size_t i=0;i<expected.size();++i){++checks;if(actual.pixels[i]!=expected[i]){if(!differences)first=i;++differences;}}
        if(differences)throw std::runtime_error(name+": "+std::to_string(differences)+" banner byte differences; first byte "+std::to_string(first)+", native "+std::to_string(actual.pixels[first])+", Java "+std::to_string(expected[first]));
        if(name.rfind("base_",0)==0){mc::nbt::Tag tag;tag.type=10;tag.compound["Base"]=mc::nbt::integer(3,std::stoi(name.substr(5)));auto encoded=mc::nbt::encode(tag);pose.nbt=&encoded;auto frame=runtime.build(world,pose);require(frame.status==mc::TileRenderStatus::Drawn&&frame.draws[0].texture->pixels==expected,"Imported runtime MapColor/base/mask pixels match Java");}
    }
    require(reference.in.peek()==std::char_traits<char>::eof(),"No reference suffix");std::cout<<cases<<" original Java banner composites matched\n";
}
void geometryReference(const std::string& root) {
    Reference reference(root+"/tools/reference/tile_geometry_reference.bin","NVTILREF");const auto count=reference.u32();require(count==518,"Tile geometry case coverage");mc::TileEntityVisuals tiles;std::string error;require(tiles.load(root+"/assets",error),error);unsigned quads=0;
    for(unsigned sample=0;sample<count;++sample){int id=reference.i32(),meta=reference.i32(),neighbors=reference.i32();mc::TileEntityPose pose;pose.christmas=reference.i32()!=0;pose.destroyStage=reference.i32();int skull=reference.i32(),rotation=reference.i32();pose.x=reference.i32();pose.y=reference.i32();pose.z=reference.i32();pose.worldTime=reference.i64();pose.lid.previous=reference.f32();pose.lid.current=reference.f32();pose.partialTicks=reference.f32();pose.renderX=2.5;pose.renderY=-.75;pose.renderZ=11.25;
        mc::World world;world.setBlock(pose.x,pose.y,pose.z,mc::blockState(id,meta));const int offsets[4][2]={{1,0},{0,1},{-1,0},{0,-1}};for(int i=0;i<4;++i)if(neighbors&(1<<i))world.setBlock(pose.x+offsets[i][0],pose.y,pose.z+offsets[i][1],mc::blockState(id,meta));
        mc::nbt::Tag data;data.type=10;if(id==144){data.compound["SkullType"]=mc::nbt::integer(1,skull);data.compound["Rot"]=mc::nbt::integer(1,rotation);}else data.compound["Base"]=mc::nbt::integer(3,0);auto nbt=mc::nbt::encode(data);pose.nbt=&nbt;
        const auto frame=tiles.build(world,pose);const auto expected=reference.u32();require(frame.draws.size()*6==expected,"Original tile face count at "+std::to_string(sample)+": "+frame.diagnostic);require(frame.status==(expected?mc::TileRenderStatus::Drawn:mc::TileRenderStatus::Hidden),"Original tile render/suppression status");
        for(unsigned q=0;q<expected;++q){++quads;std::string resource=reference.string();bool cull=reference.boolean();const auto& draw=frame.draws[q/6];require(draw.cull==cull,"Original culling mode");
            std::string texture=resource=="minecraft:b0"?"banner:b0":resource;if(texture.rfind("minecraft:textures/",0)==0){texture.erase(10,9);texture.resize(texture.size()-4);}require(draw.texture&&draw.texture->key==texture,"Original tile texture selection: "+resource);
            for(float value:draw.matrix)near(value,reference.f32(),"Tile matrix case "+std::to_string(sample)+" quad "+std::to_string(q));
            for(float value:draw.textureMatrix)near(value,reference.f32(),"Tile damage UV matrix");
            std::array<std::array<float,8>,4> vertices;for(auto& vertex:vertices)for(float& value:vertex)value=reference.f32();const unsigned indices[]={0,1,2,0,2,3};require(draw.mesh&&draw.mesh->vertices.size()==36,"Original ModelBox triangle count");
            for(unsigned i=0;i<6;++i){const auto& v=draw.mesh->vertices[(q%6)*6+i];const float values[]={v.x,v.y,v.z,v.u,v.v,v.nx,v.ny,v.nz};for(unsigned k=0;k<8;++k)near(values[k],vertices[indices[i]][k],"Original ModelBox/TexturedQuad vertices");}
        }
    }
    const auto ticks=reference.u32();require(ticks==80,"Chest animation tick coverage");mc::ChestLidState lid;
    for(unsigned i=0;i<ticks;++i){int action=reference.i32(),parameter=reference.i32(),users=reference.i32();mc::TileEntityVisuals::blockAction(lid,action,parameter);mc::TileEntityVisuals::advanceLid(lid);require(lid.users==users,"Original chest viewer event");near(lid.previous,reference.f32(),"Original previous lid state");near(lid.current,reference.f32(),"Original current lid state");}
    require(reference.in.peek()==std::char_traits<char>::eof(),"No tile reference suffix");std::cout<<count<<" original tile cases / "<<quads<<" quads / "<<ticks<<" original chest ticks matched\n";
}
std::vector<std::uint8_t> readFile(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot read test fixture "+path.u8string());return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
void copyFile(const std::filesystem::path& source,const std::filesystem::path& target){auto bytes=readFile(source);std::filesystem::create_directories(target.parent_path());std::ofstream out(target,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));if(!out)throw std::runtime_error("Cannot write skin test fixture");}
std::string base64(const std::string& text){const char* chars="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";unsigned value=0;int bits=0;std::string result;for(unsigned char c:text){value=(value<<8)|c;bits+=8;while(bits>=6){bits-=6;result+=chars[(value>>bits)&63];}}if(bits)result+=chars[(value<<(6-bits))&63];while(result.size()%4)result+='=';return result;}
std::string digest(const std::string& text){unsigned char hash[32];SHA256(reinterpret_cast<const unsigned char*>(text.data()),text.size(),hash);const char* digits="0123456789abcdef";std::string out;for(auto c:hash){out+=digits[c>>4];out+=digits[c&15];}return out;}
mc::nbt::Tag banner(int color,const std::vector<std::pair<std::string,int>>& patterns={}){mc::nbt::Tag root;root.type=10;root.compound["Base"]=mc::nbt::integer(3,color);mc::nbt::Tag list;list.type=9;list.subtype=10;for(const auto& entry:patterns){mc::nbt::Tag tag;tag.type=10;tag.compound["Pattern"]=mc::nbt::text(entry.first);tag.compound["Color"]=mc::nbt::integer(3,entry.second);list.list.push_back(tag);}root.compound["Patterns"]=list;return root;}
void behaviors(const std::string& root) {
    mc::TileEntityVisuals tiles;mc::World world;mc::TileEntityPose p;p.y=64;world.setBlock(0,64,0,mc::blockState(54,3));
    require(tiles.build(world,p).status==mc::TileRenderStatus::MissingData,"Explicit unloaded resource status");std::string error;require(tiles.load(root+"/assets",error),error);
    auto normal=tiles.build(world,p);require(normal.kind==mc::TileEntityKind::Chest&&normal.draws.size()==3&&normal.draws[0].texture->key=="minecraft:entity/chest/normal","Chest needs no S35 inventory NBT");
    world.setBlock(1,64,0,mc::blockState(146,3));require(tiles.build(world,p).draws[0].texture->key==normal.draws[0].texture->key,"Normal and trapped chests do not join");
    world.setBlock(1,64,0,mc::blockState(54,5));require(tiles.build(world,p).draws[0].texture->key=="minecraft:entity/chest/normal_double","Same chest type joins by neighbors");
    p.x=1;require(tiles.build(world,p).status==mc::TileRenderStatus::Hidden,"Double chest second half suppressed");p.x=0;
    world.setBlock(0,64,0,mc::blockState(130,3));world.setBlock(1,64,0,mc::blockState(130,3));require(tiles.build(world,p).draws[0].texture->key=="minecraft:entity/chest/ender","Ender chests remain single");
    p.destroyStage=9;auto broken=tiles.build(world,p);require(broken.draws[0].texture->repeat&&broken.draws[0].texture->key=="minecraft:blocks/destroy_stage_9","Damage texture requests repeat sampling");p.destroyStage=10;require(tiles.build(world,p).status==mc::TileRenderStatus::InvalidData,"Invalid destroy stage explicit");p.destroyStage=-1;
    for(unsigned id:{0u,63u,68u,1u,52u}){world.setBlock(0,64,0,mc::blockState(id));require(!mc::TileEntityVisuals::handlesBlock(id)&&tiles.build(world,p).status==mc::TileRenderStatus::Unsupported,"Signs and unsupported tiles remain outside this renderer");}
    world.setBlock(0,64,0,mc::blockState(176));require(tiles.build(world,p).status==mc::TileRenderStatus::MissingData,"Banner waits for actual NBT initialization");
    auto bytes=mc::nbt::encode(banner(0));p.nbt=&bytes;auto black=tiles.build(world,p);require(black.status==mc::TileRenderStatus::Drawn&&black.draws.size()==3&&black.draws[0].texture->key=="banner:b0","Base banner NBT");
    auto again=tiles.build(world,p);require(again.draws[0].mesh==black.draws[0].mesh&&again.draws[0].texture==black.draws[0].texture,"Banner shares immutable mesh/texture");
    bytes=mc::nbt::encode(banner(1));auto red=tiles.build(world,p);require(red.draws[0].texture->key=="banner:b1"&&red.draws[0].texture->pixels!=black.draws[0].texture->pixels,"Changed raw NBT invalidates cached parse and selects new dye");
    bytes=mc::nbt::encode(banner(0,{{"not_a_pattern",13}}));require(tiles.build(world,p).draws[0].texture==black.draws[0].texture,"Unknown pattern IDs are ignored like vanilla");
    bytes=mc::nbt::encode(banner(-1));require(tiles.build(world,p).draws[0].texture->pixels==black.draws[0].texture->pixels,"Invalid dye uses original black fallback");
    std::vector<std::pair<std::string,int>> layers;for(int i=0;i<18;++i)layers.emplace_back(i%2?"ts":"bs",i%16);bytes=mc::nbt::encode(banner(15,layers));auto capped=tiles.build(world,p);layers.back().second=15;bytes=mc::nbt::encode(banner(15,layers));auto cappedAgain=tiles.build(world,p);require(capped.draws[0].texture->key!=cappedAgain.draws[0].texture->key&&capped.draws[0].texture->pixels==cappedAgain.draws[0].texture->pixels,"Design identity retains all patterns but compositing stops at 17 layers");
    world.setBlock(0,64,0,mc::blockState(177,2));require(tiles.build(world,p).draws.size()==2,"Wall banner has no stand");
    bytes={10,0};require(tiles.build(world,p).status==mc::TileRenderStatus::InvalidData,"Malformed NBT reported without throwing through renderer");bytes.assign(2*1024*1024+1,0);require(tiles.build(world,p).status==mc::TileRenderStatus::InvalidData,"Original NBT packet budget enforced");
    world.setBlock(0,64,0,mc::blockState(144,1));mc::nbt::Tag skull;skull.type=10;skull.compound["SkullType"]=mc::nbt::integer(1,3);skull.compound["Rot"]=mc::nbt::integer(1,5);bytes=mc::nbt::encode(skull);auto player=tiles.build(world,p);require(player.draws.size()==2&&player.draws[0].texture->key=="minecraft:entity/steve","Player head without owner uses legacy Steve plus hat");
    mc::nbt::Tag owner;owner.type=10;const std::string playerUuid="00000000-0000-0000-0000-000000000001";owner.compound["Id"]=mc::nbt::text(playerUuid);skull.compound["Owner"]=owner;bytes=mc::nbt::encode(skull);require(tiles.build(world,p).draws[0].texture->key=="minecraft:entity/alex","Player head owner UUID chooses default Alex");
    const std::string url="https://textures.minecraft.net/texture/0123456789abcdef0123456789abcdef";const auto property=base64("{\"textures\":{\"SKIN\":{\"url\":\""+url+"\"}}}");mc::nbt::Tag props,list,entry;props.type=10;list.type=9;list.subtype=10;entry.type=10;entry.compound["Value"]=mc::nbt::text(property);list.list.push_back(entry);props.compound["textures"]=list;owner.compound["Properties"]=props;skull.compound["Owner"]=owner;bytes=mc::nbt::encode(skull);require(tiles.build(world,p).approximate,"Texture property without SkinCache reports fallback");
    const auto cache=std::filesystem::u8path(root+"/build/tile_entity_tests/skin_assets");for(const char* name:{"steve","alex"})copyFile(std::filesystem::u8path(root+"/assets/runtime/entities/minecraft/"+name+".rgba"),cache/(std::string("runtime/entities/minecraft/")+name+".rgba"));copyFile(std::filesystem::u8path(root+"/tools/reference/skin_input_1.png"),cache/("runtime/skin-cache/"+digest(url)+".png"));
    mc::SkinCache skins;require(skins.initialize(cache.u8string(),error,false),error);tiles.build(world,p,&skins);auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(skins.pending()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));require(skins.pending()==0,"Offline skull skin worker drains");
    auto custom=tiles.build(world,p,&skins);require(!custom.approximate&&custom.draws[0].texture->key=="mojang:"+digest(url)&&custom.draws[0].texture->pixels.size()==16384,"Skull uses normalized SkinCache image");require(!skins.get(playerUuid)->downloaded,"Custom skull texture does not replace a live player's UUID skin entry");require(tiles.build(world,p,&skins).draws[0].texture==custom.draws[0].texture,"Downloaded skull texture cached by revision");
    p.destroyStage=2;auto crack=tiles.build(world,p,&skins);require(crack.draws.size()==1&&crack.draws[0].texture->key=="minecraft:blocks/destroy_stage_2","Original skull damage pass uses skeleton model without hat");p.destroyStage=-1;
    require(!tiles.load(root+"/build/nonexistent_tiles",error)&&tiles.build(world,p,&skins).status==mc::TileRenderStatus::Drawn,"Failed resource reload retains previous valid state");
    tiles.clear();world.setBlock(0,64,0,mc::blockState(176));for(int i=0;i<256;++i){bytes=mc::nbt::encode(banner(i));require(tiles.build(world,p).status==mc::TileRenderStatus::Drawn,"Original 256 banner cache capacity");}bytes=mc::nbt::encode(banner(256));require(tiles.build(world,p).status==mc::TileRenderStatus::MissingData,"Full recently-used banner cache follows source suppression");tiles.clear();require(tiles.build(world,p).status==mc::TileRenderStatus::Drawn,"Clear frees dynamic caches and retains static resources");
}
}
int main(int argc,char** argv){try{const std::string root=argc>1?argv[1]:".";if(std::filesystem::exists(root+"/tools/reference/banner_reference.bin"))bannerReference(root);else std::cout<<"Skipping optional Java banner pixel reference; regenerate with tools/export_banner_reference.py\n";geometryReference(root);behaviors(root);std::cout<<checks<<" tile visual checks passed; maximum geometry/matrix error="<<maximumError<<'\n';return 0;}catch(const std::exception& e){std::cerr<<"Tile visual tests failed: "<<e.what()<<'\n';return 1;}}
