// Source inclusion also permits independent Java fixtures to check the unused
// left-arm variants. The application compiles first_person.cpp normally.
#include "../src/first_person.cpp"
#include <filesystem>
#include <iostream>

namespace {
unsigned checks=0;float maximumError=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
struct Reference {
    std::ifstream input;
    Reference(const std::string& path,const char* expected):input(path,std::ios::binary){char magic[8];if(!input.read(magic,8)||std::memcmp(magic,expected,8))throw std::runtime_error("Invalid Java reference "+path);}
    std::uint32_t u32(){unsigned char b[4];if(!input.read(reinterpret_cast<char*>(b),4))throw std::runtime_error("Truncated Java reference");return (std::uint32_t(b[0])<<24)|(std::uint32_t(b[1])<<16)|(std::uint32_t(b[2])<<8)|b[3];}
    float f32(){auto bits=u32();float f;std::memcpy(&f,&bits,4);return f;}
};
void near(float actual,float expected,const std::string& label,float tolerance=2e-6f){float error=std::abs(actual-expected);maximumError=std::max(maximumError,error);require(std::isfinite(actual)&&error<=tolerance,label+": native="+std::to_string(actual)+" Java="+std::to_string(expected));}
}
int main(int argc,char** argv) {
    try {
        const std::string root=argc>1?argv[1]:".";mc::FirstPersonAssets assets;std::string error;
        require(assets.build({}).supported,"Arm available before model load");
        require(assets.load(root+"/assets",error),error);require(assets.atlasWidth()>0&&assets.atlasHeight()>0,"Held atlas loaded");
        require(assets.atlasPixels().size()==std::size_t(assets.atlasWidth())*assets.atlasHeight()*4,"Held atlas byte count");
        Reference matrices(root+"/tools/reference/first_person_reference.bin","NVFPREF1");auto count=matrices.u32();require(count==497,"Original pose fixture count");
        for(unsigned test=0;test<count;++test) {
            unsigned kind=matrices.u32();mc::FirstPersonPose p;
            p.swing=matrices.f32();p.equip=matrices.f32();p.cameraPitch=matrices.f32();p.cameraYaw=matrices.f32();p.armPitch=matrices.f32();p.armYaw=matrices.f32();p.partialTicks=matrices.f32();
            int use=static_cast<std::int32_t>(matrices.u32());p.usingItem=use>=0;p.use=use>=0?static_cast<mc::FirstPersonUse>(use):mc::FirstPersonUse::None;
            p.useTicksRemaining=static_cast<int>(matrices.u32());p.maxUseTicks=static_cast<int>(matrices.u32());mc::FirstPersonMatrix actual;
            if(kind==0)actual=mc::FirstPersonAssets::itemTransform(p);
            else if(kind==1)actual=mc::FirstPersonAssets::armTransform(p);
            else {p.itemId=358;auto frame=assets.build(p);require(frame.draws.size()>=5,"Map arms/background draws");actual=frame.draws.at(kind==2?4:kind==3?0:2).matrix;}
            for(unsigned i=0;i<16;++i)near(actual[i],matrices.f32(),"ItemRenderer matrix "+std::to_string(test)+" element "+std::to_string(i));
        }
        Reference arms(root+"/tools/reference/first_person_arms.bin","NVARM001");auto armCount=arms.u32();require(armCount==16,"Original arm fixture count");
        for(unsigned test=0;test<armCount;++test) {
            auto id=arms.u32(),vertices=arms.u32();const auto mesh=mc::arm(id&1,(id&2)!=0,(id&4)!=0,(id&8)!=0);require(mesh->vertices.size()==vertices,"ModelPlayer vertex count");
            for(unsigned i=0;i<vertices;++i){const auto& v=mesh->vertices[i];for(float actual:{v.x,v.y,v.z,v.u,v.v})near(actual,arms.f32(),"ModelPlayer mesh "+std::to_string(id)+" vertex "+std::to_string(i));}
        }
        mc::FirstPersonPose p;auto empty=assets.build(p);require(empty.supported&&empty.draws.size()==2,"Empty right arm and sleeve");
        p.rightSleeve=false;require(assets.build(p).draws.size()==1,"Sleeve visibility");p.invisible=true;require(assets.build(p).draws.empty(),"Invisible empty hand");p={};
        p.itemId=1;auto block=assets.build(p);require(block.supported&&!block.approximate&&!block.draws.empty(),"Full block held model");
        unsigned blockVertices=0;for(const auto& draw:block.draws)blockVertices+=unsigned(draw.mesh->vertices.size());require(blockVertices==36,"Cube has six faces");
        p.itemId=20;require(!assets.build(p).draws[0].translucent,"Plain glass uses CUTOUT and keeps depth writes");p.itemId=95;require(assets.build(p).draws[0].translucent,"Stained glass uses TRANSLUCENT");
        p.itemId=276;auto sword=assets.build(p);require(sword.supported&&!sword.approximate,"Generated sword model");
        bool front=false,back=false,side=false;for(const auto& draw:sword.draws)for(const auto& v:draw.mesh->vertices){front=front||v.z==7.5f/16;back=back||v.z==8.5f/16;side=side||std::abs(v.nx)+std::abs(v.ny)>.5f;}
        require(front&&back&&side,"Generated sword has original thickness and silhouette edges");
        p.metadata=112;require(assets.build(p).draws.front().mesh==sword.draws.front().mesh,"Damageable item metadata keeps same geometry");
        p={};p.itemId=261;auto bow0=assets.build(p);p.usingItem=true;p.use=mc::FirstPersonUse::Bow;p.maxUseTicks=72000;
        p.useTicksRemaining=71999;auto bow1=assets.build(p);p.useTicksRemaining=71986;auto bow2=assets.build(p);p.useTicksRemaining=71982;auto bow3=assets.build(p);
        require(bow0.draws[0].mesh!=bow1.draws[0].mesh&&bow1.draws[0].mesh!=bow2.draws[0].mesh&&bow2.draws[0].mesh!=bow3.draws[0].mesh,"Bow pull thresholds select original models");
        p={};p.itemId=346;auto rod=assets.build(p);p.fishingCast=true;require(assets.build(p).draws[0].mesh!=rod.draws[0].mesh,"Fishing line cast model");
        p={};p.itemId=345;p.textureFrame=0;auto compass=assets.build(p);p.textureFrame=31;require(assets.build(p).draws[0].mesh!=compass.draws[0].mesh,"Compass frame geometry UV changes");
        p={};p.itemId=373;p.metadata=8193;auto potion=assets.build(p);require(potion.supported&&potion.draws.size()==2&&potion.draws[0].color!=0xffffffffu&&potion.draws[1].color==0xffffffffu,"Potion layer tint");
        p.colorOverride=0x123456;require(assets.build(p).draws[0].color==0xff123456u,"NBT primary tint override");
        p={};p.itemId=358;require(assets.build(p).draws.size()==6,"Map has arms, background and original post-map item draw");p.mapDataAvailable=true;auto map=assets.build(p);require(map.draws.size()==7&&map.draws[5].texture==mc::FirstPersonTexture::MapData,"Map pixel layer");
        p.invisible=true;require(assets.build(p).draws.size()==3,"Invisible map hides arms only");
        unsigned supported=0;
        for(int id=0;id<65536;++id){if(!mc::registry::item(id).valid)continue;p={};p.itemId=id;auto frame=assets.build(p);if(!frame.supported)continue;++supported;
            require(!frame.draws.empty(),"Registered item has held geometry");
            for(const auto& draw:frame.draws)for(float v:draw.matrix)require(std::isfinite(v),"Registered item matrix is finite");}
        require(supported==335,"Held model registered item coverage");
        const auto width=assets.atlasWidth();require(!assets.load(root+"/build/nonexistent_first_person_assets",error),"Missing cache rejected");require(assets.atlasWidth()==width&&assets.build({}).supported,"Failed load preserves renderer state");
        std::cout<<checks<<" first-person checks passed; "<<count<<" original Java matrices; "<<armCount<<" arm meshes; "<<supported<<" item IDs; maximum absolute error="<<maximumError<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<"First-person tests failed: "<<e.what()<<'\n';return 1;}
}
