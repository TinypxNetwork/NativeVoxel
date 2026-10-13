#include "mc/item_entities.hpp"
#include "mc/registry.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks=0;float maximum=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
void near(float actual,float expected,const std::string& label){float difference=std::abs(actual-expected);maximum=std::max(maximum,difference);require(std::isfinite(actual)&&difference<=2e-6f,label+" differs: native="+std::to_string(actual)+", Java="+std::to_string(expected));}
struct Reference {
    std::ifstream input;
    explicit Reference(const std::string& path):input(path,std::ios::binary){char magic[8];require(bool(input.read(magic,8))&&!std::memcmp(magic,"NVITENT1",8),"Item entity Java reference header");}
    unsigned u8(){int b=input.get();if(b<0)throw std::runtime_error("Truncated item entity reference");return unsigned(b);}
    std::uint32_t u32(){std::uint32_t v=0;for(int i=0;i<4;++i)v=(v<<8)|u8();return v;}
    int i32(){return static_cast<std::int32_t>(u32());}
    float f32(){auto bits=u32();float value;std::memcpy(&value,&bits,4);return value;}
    double f64(){const std::uint64_t high=u32(),low=u32(),bits=(high<<32)|low;double value;std::memcpy(&value,&bits,8);return value;}
};
std::vector<std::uint8_t> read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot read test cache");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
void write(const std::filesystem::path& path,const std::vector<std::uint8_t>& bytes){std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));if(!out)throw std::runtime_error("Cannot write test cache");}
std::uint32_t little(const std::vector<std::uint8_t>& bytes,std::size_t at){return std::uint32_t(bytes.at(at))|(std::uint32_t(bytes.at(at+1))<<8)|(std::uint32_t(bytes.at(at+2))<<16)|(std::uint32_t(bytes.at(at+3))<<24);}
}

int main(int argc,char** argv) {
    try {
        const std::string root=argc>1?argv[1]:".";Reference reference(root+"/tools/reference/item_entities_reference.bin");
        const auto dropCount=reference.u32();require(dropCount==272,"Drop fixture coverage");unsigned copyMatrices=0;
        for(unsigned sample=0;sample<dropCount;++sample){mc::DroppedItemPose p;p.itemId=1;p.count=reference.i32();p.ageTicks=reference.i32();p.partialTicks=reference.f32();p.hoverStart=reference.f32();p.x=reference.f64();p.y=reference.f64();p.z=reference.f64();bool gui3d=reference.u8()!=0;p.cameraOptionsPresent=reference.u8()!=0;std::array<float,3> ground;for(float& v:ground)v=reference.f32();
            auto expected=reference.u32();auto actual=mc::ItemEntityVisuals::droppedTransforms(p,gui3d,ground);require(actual.size()==expected&&mc::ItemEntityVisuals::stackCopies(p.count)==int(expected),"Original stack-count copies");
            for(const auto& matrix:actual){++copyMatrices;for(float value:matrix)near(value,reference.f32(),"RenderEntityItem copy "+std::to_string(sample));}}
        const auto orbCount=reference.u32();require(orbCount==728,"Orb fixture coverage");
        for(unsigned sample=0;sample<orbCount;++sample){mc::XPOrbPose p;p.amount=reference.i32();p.xpColor=reference.i32();p.partialTicks=reference.f32();p.viewYaw=reference.f32();p.viewPitch=reference.f32();p.x=reference.f64();p.y=reference.f64();p.z=reference.f64();p.packedLight=reference.u32();
            const auto texture=reference.u32(),color=reference.u32(),light=reference.u32();auto frame=mc::ItemEntityVisuals::orb(p);require(frame.supported&&!frame.approximate&&frame.copies==1&&frame.draws.size()==1,"Orb draw shape");
            const auto& draw=frame.draws[0];require(draw.texture==mc::ItemEntityTexture::ExperienceOrb&&mc::ItemEntityVisuals::orbTexture(p.amount)==int(texture),"Original XP texture bucket");require(draw.color==color,"Original XP RGB/alpha");require(frame.packedLight==light,"Original XP block light boost");
            for(float value:draw.matrix)near(value,reference.f32(),"RenderXPOrb billboard matrix");
            std::array<std::array<float,8>,4> vertices;for(auto& vertex:vertices)for(float& value:vertex)value=reference.f32();
            require(draw.mesh->vertices.size()==6,"XP triangles");const unsigned indices[]={0,1,2,0,2,3};for(unsigned i=0;i<6;++i){const auto& v=draw.mesh->vertices[i];std::array<float,8> native{{v.x,v.y,v.z,v.u,v.v,v.nx,v.ny,v.nz}};for(unsigned j=0;j<8;++j)near(native[j],vertices[indices[i]][j],"RenderXPOrb original quad");}
        }
        require(reference.input.peek()==std::char_traits<char>::eof(),"No untested reference suffix");
        mc::FirstPersonAssets assets;std::string error;require(assets.load(root+"/assets",error),error);require(assets.worldModelsReady(),"GROUND/FIXED sidecar loaded");
        mc::FirstPersonPose first;first.itemId=1;auto held=assets.build(first);auto ground=assets.itemModel(1,0,mc::ItemModelTransform::Ground),fixed=assets.itemModel(1,0,mc::ItemModelTransform::Fixed);
        require(ground.supported&&fixed.supported&&ground.gui3d&&ground.draws[0].mesh==held.draws[0].mesh&&fixed.draws[0].mesh==held.draws[0].mesh,"World models share held-item geometry");
        mc::DroppedItemPose dropped;dropped.itemId=1;dropped.count=64;dropped.ageTicks=90;dropped.hoverStart=2.1f;dropped.packedLight=0xf00070;
        auto copies=mc::ItemEntityVisuals::dropped(assets,dropped);require(copies.supported&&copies.copies==5&&copies.draws.size()==ground.draws.size()*5&&copies.packedLight==dropped.packedLight,"Five-copy block draw composition");
        for(const auto& draw:copies.draws)require(draw.mesh==held.draws[0].mesh&&draw.texture==mc::ItemEntityTexture::ItemAtlas,"Copies reuse VBO-stable mesh identity");
        dropped.itemId=373;dropped.metadata=8193;dropped.count=17;auto potion=mc::ItemEntityVisuals::dropped(assets,dropped);require(potion.copies==3&&potion.draws.size()==18&&!assets.itemModel(373,8193,mc::ItemModelTransform::Ground).gui3d,"Flat potion copies retain both tint layers and two effect passes");
        require(potion.draws[0].color!=0xffffffffu&&potion.draws[1].color==0xffffffffu,"Dropped potion color layers");
        dropped.colorOverride=0x123456;require(mc::ItemEntityVisuals::dropped(assets,dropped).draws[0].color==0xff123456u,"Dropped NBT primary color override");
        dropped={};require(mc::ItemEntityVisuals::dropped(assets,dropped).draws.empty(),"Null dropped item has no draw");
        mc::XPOrbPose orb;orb.amount=17;auto a=mc::ItemEntityVisuals::orb(orb);orb.amount=36;orb.xpColor=20;auto b=mc::ItemEntityVisuals::orb(orb);require(a.draws[0].mesh==b.draws[0].mesh&&a.draws[0].color!=b.draws[0].color,"XP color animation reuses bucket mesh");
        require(a.shadowSize==.15f&&a.shadowOpacity==.75f,"Original entity shadow parameters");
        unsigned supported=0;for(int id=0;id<65536;++id)if(mc::registry::item(id).valid){auto item=assets.itemModel(id,0,mc::ItemModelTransform::Ground);if(!item.supported)continue;++supported;for(const auto& draw:item.draws){require(bool(draw.mesh)&&!draw.mesh->vertices.empty(),"Registered ground geometry");for(float v:draw.matrix)require(std::isfinite(v),"Finite registered ground matrix");}}
        require(supported==335,"World-item registered coverage");
        const auto cache=std::filesystem::u8path(root+"/assets/runtime/first_person");const auto scratch=std::filesystem::u8path(root+"/build/item_entity_tests/cache/runtime/first_person");
        write(scratch/"models.bin",read(cache/"models.bin"));write(scratch/"atlas.rgba",read(cache/"atlas.rgba"));
        // Keep the sidecar absent even on repeated runs, without recursive removal.
        std::error_code ec;std::filesystem::remove(scratch/"transforms.bin",ec);
        mc::FirstPersonAssets legacy;require(legacy.load((scratch.parent_path().parent_path()).u8string(),error),error);require(!legacy.worldModelsReady()&&legacy.build(first).supported,"Old first-person cache remains compatible without sidecar");
        auto sidecar=read(cache/"transforms.bin");std::size_t at=12,stoneScale=0;for(unsigned i=0;i<little(sidecar,8);++i){auto size=little(sidecar,at);at+=4;std::string name(sidecar.begin()+at,sidecar.begin()+at+size);at+=size;if(name=="minecraft:stone")stoneScale=at+4+6*4;at+=4+18*4;}
        require(stoneScale!=0,"Locate stone GROUND scale in sidecar");float negative=-1;std::memcpy(sidecar.data()+stoneScale,&negative,4);write(scratch/"transforms.bin",sidecar);
        mc::FirstPersonAssets reflected;require(reflected.load((scratch.parent_path().parent_path()).u8string(),error),error);auto reflectedGround=reflected.itemModel(1,0,mc::ItemModelTransform::Ground),unreflected=reflected.itemModel(1,0,mc::ItemModelTransform::Fixed);
        require(reflectedGround.groundScale[0]==-1&&reflectedGround.draws[0].mesh!=unreflected.draws[0].mesh,"Negative ground scale selects a separate winding");
        const auto& flipped=reflectedGround.draws[0].mesh->vertices[1];const auto& original=unreflected.draws[0].mesh->vertices[2];require(flipped.x==original.x&&flipped.y==original.y&&flipped.z==original.z&&flipped.u==original.u&&flipped.v==original.v,"Ground triangle winding is reflected independently");
        require(reflected.build(first).draws[0].mesh==unreflected.draws[0].mesh,"World reflection does not change first-person mesh");
        sidecar[8]=0;sidecar[9]=0;sidecar[10]=0;sidecar[11]=0;write(scratch/"transforms.bin",sidecar);require(!reflected.load((scratch.parent_path().parent_path()).u8string(),error)&&reflected.worldModelsReady(),"Invalid sidecar rejected without replacing current assets");
        std::cout<<checks<<" item-entity checks passed: "<<copyMatrices<<" Java dropped matrices, "<<orbCount<<" Java XP draws, "<<supported<<" item IDs; maximum absolute error="<<maximum<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<"Item-entity tests failed: "<<e.what()<<'\n';return 1;}
}
