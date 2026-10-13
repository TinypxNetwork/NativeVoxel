#include "mc/first_person.hpp"
#include "mc/registry.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace mc {
namespace {
constexpr float pi=3.14159265358979323846f;
using Matrix=FirstPersonMatrix;
Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
Matrix multiply(const Matrix& a,const Matrix& b) {
    Matrix result{};
    for(int column=0;column<4;++column)for(int row=0;row<4;++row)for(int k=0;k<4;++k)
        result[column*4+row]+=a[k*4+row]*b[column*4+k];
    return result;
}
void translate(Matrix& m,float x,float y,float z){auto t=identity();t[12]=x;t[13]=y;t[14]=z;m=multiply(m,t);}
void scale(Matrix& m,float x,float y,float z){auto s=identity();s[0]=x;s[5]=y;s[10]=z;m=multiply(m,s);}
void rotate(Matrix& m,int axis,float degrees) {
    const double radians=double(degrees)*3.14159265358979323846/180.0;
    const float c=float(std::cos(radians)),s=float(std::sin(radians));auto r=identity();
    if(axis==0){r[5]=c;r[9]=-s;r[6]=s;r[10]=c;}
    else if(axis==1){r[0]=c;r[8]=s;r[2]=-s;r[10]=c;}
    else {r[0]=c;r[4]=-s;r[1]=s;r[5]=c;}
    m=multiply(m,r);
}
const std::array<float,65536>& sineTable() {
    static const auto table=[] {std::array<float,65536> result{};for(unsigned i=0;i<65536;++i)result[i]=float(std::sin(double(i)*3.14159265358979323846*2.0/65536.0));return result;}();
    return table;
}
int javaInt(float x){if(std::isnan(x))return 0;if(x>=2147483648.f)return 2147483647;if(x<=-2147483648.f)return (-2147483647-1);return int(x);}
float sine(float x){return sineTable()[unsigned(javaInt(x*10430.378f))&65535u];}
float cosine(float x){return sineTable()[unsigned(javaInt(x*10430.378f+16384.f))&65535u];}
Matrix camera(const FirstPersonPose& p){auto m=identity();rotate(m,0,(p.cameraPitch-p.armPitch)*.1f);rotate(m,1,(p.cameraYaw-p.armYaw)*.1f);return m;}
void swingTranslation(Matrix& m,float swing){const float root=std::sqrt(swing);translate(m,-.4f*sine(root*pi),.2f*sine(root*pi*2.f),-.2f*sine(swing*pi));}
void swingRotation(Matrix& m,float swing){const float a=sine(swing*swing*pi),b=sine(std::sqrt(swing)*pi);rotate(m,1,a*-20.f);rotate(m,2,b*-20.f);rotate(m,0,b*-80.f);}
void commonItem(Matrix& m,float equip,float swing){translate(m,.56f,-.52f,-.71999997f);translate(m,0,equip*-.6f,0);rotate(m,1,45);swingRotation(m,swing);scale(m,.4f,.4f,.4f);}
void drinking(Matrix& m,const FirstPersonPose& p) {
    const float remaining=float(p.useTicksRemaining)-p.partialTicks+1.f;
    const float ratio=remaining/float(std::max(1,p.maxUseTicks));
    float bob=std::abs(cosine(remaining/4.f*pi)*.1f);if(ratio>=.8f)bob=0;
    translate(m,0,bob,0);const float blend=1.f-float(std::pow(double(ratio),27.0));
    translate(m,blend*.6f,blend*-.5f,blend*0.f);rotate(m,1,blend*90.f);rotate(m,0,blend*10.f);rotate(m,2,blend*30.f);
}
void bow(Matrix& m,const FirstPersonPose& p) {
    rotate(m,2,-18);rotate(m,1,-12);rotate(m,0,-8);translate(m,-.9f,.2f,0);
    const float elapsed=float(p.maxUseTicks)-(float(p.useTicksRemaining)-p.partialTicks+1.f);
    float draw=elapsed/20.f;draw=(draw*draw+draw*2.f)/3.f;if(draw>1)draw=1;
    if(draw>.1f){const float shake=sine((elapsed-.1f)*1.3f)*(draw-.1f);translate(m,shake*0.f,shake*.01f,shake*0.f);}
    translate(m,draw*0.f,draw*0.f,draw*.1f);scale(m,1,1,1.f+draw*.2f);
}
Matrix mapBase(const FirstPersonPose& p) {
    auto m=camera(p);swingTranslation(m,p.swing);
    float angle=1.f-p.cameraPitch/45.f+.1f;angle=std::clamp(angle,0.f,1.f);angle=-cosine(angle*pi)*.5f+.5f;
    translate(m,0,.04f,-.72f);translate(m,0,p.equip*-1.2f,0);translate(m,0,angle*-.5f,0);rotate(m,1,90);rotate(m,2,angle*-85.f);return m;
}
Matrix mapSurface(Matrix m,const FirstPersonPose& p) {
    swingRotation(m,p.swing);scale(m,.38f,.38f,.38f);rotate(m,1,90);rotate(m,2,180);translate(m,-1,-1,0);scale(m,.015625f,.015625f,.015625f);return m;
}
struct Vec {float x,y,z;};
Vec point(const Matrix& m,Vec p){return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};}
Vec normal(Vec a,Vec b,Vec c) {
    Vec u{b.x-a.x,b.y-a.y,b.z-a.z},v{c.x-a.x,c.y-a.y,c.z-a.z};Vec n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
    float length=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);return length>1e-10f?Vec{n.x/length,n.y/length,n.z/length}:Vec{};
}
std::shared_ptr<const FirstPersonMesh> arm(bool slim,bool left,bool sleeve,bool map) {
    auto mesh=std::make_shared<FirstPersonMesh>();const float inflate=sleeve?.25f:0;
    const float x=slim?3.f:4.f,y=12,z=4;
    const float startX=left?-1.f:slim?-2.f:-3.f;
    const Vec lo{startX-inflate,-2-inflate,-2-inflate},hi{startX+x+inflate,10+inflate,2+inflate};
    const Vec corners[]={{lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,hi.y,lo.z},{lo.x,hi.y,lo.z},{lo.x,lo.y,hi.z},{hi.x,lo.y,hi.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z}};
    constexpr int faces[6][4]={{5,1,2,6},{0,4,7,3},{5,4,0,1},{2,3,7,6},{1,0,3,2},{4,5,6,7}};
    const float u=left?(sleeve?48.f:32.f):40.f,v=left?48.f:sleeve?32.f:16.f;
    const float uv[6][4]={{u+z+x,v+z,u+z+x+z,v+z+y},{u,v+z,u+z,v+z+y},{u+z,v,u+z+x,v+z},{u+z+x,v+z,u+z+x+x,v},{u+z,v+z,u+z+x,v+z+y},{u+z+x+z,v+z,u+z+x+z+x,v+z+y}};
    auto matrix=identity();scale(matrix,.0625f,.0625f,.0625f);translate(matrix,left?5.f:-5.f,slim?2.5f:2.f,0);
    // RenderPlayer invokes ModelPlayer.setRotationAngles at age=0 before each arm.
    rotate(matrix,2,(left?-.1f:.1f)*180.f/pi);if(map&&!left)rotate(matrix,0,-pi/10.f*180.f/pi);
    for(int face=0;face<6;++face) {
        const float pairs[4][2]={{uv[face][2],uv[face][1]},{uv[face][0],uv[face][1]},{uv[face][0],uv[face][3]},{uv[face][2],uv[face][3]}};
        std::array<FirstPersonVertex,4> vertices{};Vec positions[4]{};
        for(int i=0;i<4;++i){positions[i]=point(matrix,corners[faces[face][i]]);const auto& p=positions[i];vertices[i]={p.x,p.y,p.z,pairs[i][0]/64.f,pairs[i][1]/64.f,0,0,0};}
        const auto n=normal(positions[0],positions[1],positions[2]);for(auto& vertex:vertices){vertex.nx=n.x;vertex.ny=n.y;vertex.nz=n.z;}
        for(int i:{0,1,2,0,2,3})mesh->vertices.push_back(vertices[i]);
    }
    return mesh;
}
std::shared_ptr<const FirstPersonMesh> rectangle(float a,float b,float z,float u0,float v0,float u1,float v1) {
    auto mesh=std::make_shared<FirstPersonMesh>();const FirstPersonVertex v[]={{a,b,z,u0,v1,0,0,-1},{b,b,z,u1,v1,0,0,-1},{b,a,z,u1,v0,0,0,-1},{a,a,z,u0,v0,0,0,-1}};
    for(int i:{0,1,2,0,2,3})mesh->vertices.push_back(v[i]);
    return mesh;
}
class File {
    std::ifstream input_;
public:
    explicit File(const std::string& path):input_(path,std::ios::binary){if(!input_)throw std::runtime_error("Missing first-person cache: "+path+"; run tools/prepare_first_person.py");}
    void bytes(void* out,std::size_t count){if(!input_.read(static_cast<char*>(out),static_cast<std::streamsize>(count)))throw std::runtime_error("Truncated first-person cache");}
    std::uint16_t u16(){std::uint8_t v[2];bytes(v,2);return std::uint16_t(v[0])|(std::uint16_t(v[1])<<8);}
    std::uint32_t u32(){std::uint8_t v[4];bytes(v,4);return std::uint32_t(v[0])|(std::uint32_t(v[1])<<8)|(std::uint32_t(v[2])<<16)|(std::uint32_t(v[3])<<24);}
    float f32(){auto bits=u32();float f;std::memcpy(&f,&bits,4);if(!std::isfinite(f))throw std::runtime_error("Non-finite first-person cache value");return f;}
    std::string string(){auto size=u32();if(size>4096)throw std::runtime_error("First-person model name too long");std::string out(size,'\0');if(size)bytes(&out[0],size);return out;}
    void magic(const char* expected){char value[8];bytes(value,8);if(std::memcmp(value,expected,8))throw std::runtime_error("Unsupported first-person cache format");}
    void end(){if(input_.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Unexpected first-person cache suffix");}
};
std::uint32_t key(int id,int meta){return (std::uint32_t(id)<<16)|(std::uint32_t(meta)&65535u);}
} // namespace

struct FirstPersonAssets::Impl {
    struct Group {int tint=-1;std::shared_ptr<const FirstPersonMesh> mesh,rawMesh,reversedMesh;};
    struct Model {std::string name;unsigned flags=0;Matrix display=identity();std::vector<Group> groups;
        std::array<Matrix,6> worldDisplay{{identity(),identity(),identity(),identity(),identity(),identity()}};
        std::array<bool,6> reversed{{false,false,false,false,false,false}};std::array<float,3> groundScale{{1,1,1}};bool gui3d=true;};
    struct Entry {unsigned model=0;std::array<std::uint32_t,5> colors{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}};};
    unsigned width=0,height=0;std::vector<std::uint8_t> pixels;
    bool worldReady=false,equipmentReady=false;
    std::vector<Model> models;std::map<std::uint32_t,Entry> entries;std::map<unsigned,unsigned> defaults,frames;
    std::array<std::shared_ptr<const FirstPersonMesh>,16> arms;
    std::shared_ptr<const FirstPersonMesh> mapBackground,mapData=rectangle(0,128,-.01f,0,0,1,1);
    Impl(){for(unsigned i=0;i<16;++i)arms[i]=arm(i&1,(i&2)!=0,(i&4)!=0,(i&8)!=0);}
    const auto& armMesh(bool slim,bool left,bool sleeve,bool map)const{return arms[unsigned(slim)|unsigned(left)<<1|unsigned(sleeve)<<2|unsigned(map)<<3];}
};

FirstPersonAssets::FirstPersonAssets():impl_(new Impl){}
FirstPersonAssets::~FirstPersonAssets()=default;
unsigned FirstPersonAssets::atlasWidth()const noexcept{return impl_->width;}
unsigned FirstPersonAssets::atlasHeight()const noexcept{return impl_->height;}
const std::vector<std::uint8_t>& FirstPersonAssets::atlasPixels()const noexcept{return impl_->pixels;}

FirstPersonMatrix FirstPersonAssets::itemTransform(const FirstPersonPose& p) {
    auto m=camera(p);
    if(!p.usingItem){swingTranslation(m,p.swing);commonItem(m,p.equip,p.swing);return m;}
    if(p.use==FirstPersonUse::Eat||p.use==FirstPersonUse::Drink)drinking(m,p);
    commonItem(m,p.equip,0);
    if(p.use==FirstPersonUse::Block){translate(m,-.5f,.2f,0);rotate(m,1,30);rotate(m,0,-80);rotate(m,1,60);}
    else if(p.use==FirstPersonUse::Bow)bow(m,p);
    return m;
}
FirstPersonMatrix FirstPersonAssets::armTransform(const FirstPersonPose& p) {
    auto m=camera(p);const float root=std::sqrt(p.swing);
    translate(m,-.3f*sine(root*pi),.4f*sine(root*pi*2.f),-.4f*sine(p.swing*pi));
    translate(m,.64000005f,-.6f,-.71999997f);translate(m,0,p.equip*-.6f,0);rotate(m,1,45);
    rotate(m,1,sine(root*pi)*70.f);rotate(m,2,sine(p.swing*p.swing*pi)*-20.f);translate(m,-1,3.6f,3.5f);
    rotate(m,2,120);rotate(m,0,200);rotate(m,1,-135);translate(m,5.6f,0,0);return m;
}

bool FirstPersonAssets::load(const std::string& directory,std::string& error) {
    try {
        std::unique_ptr<Impl> next(new Impl);File file(directory+"/runtime/first_person/models.bin");file.magic("NVFP0001");
        next->width=file.u32();next->height=file.u32();const auto modelCount=file.u32(),entryCount=file.u32(),defaultCount=file.u32(),frameCount=file.u32();
        const std::uint64_t imageBytes=std::uint64_t(next->width)*next->height*4;
        if(!next->width||!next->height||next->width>16384||next->height>16384||imageBytes>256u*1024u*1024u||!modelCount||modelCount>20000||entryCount>200000||defaultCount>65536||frameCount>65536)throw std::runtime_error("Invalid first-person cache dimensions/counts");
        float mapUv[4];for(float& f:mapUv){f=file.f32();if(f<0||f>1)throw std::runtime_error("Invalid map background UV");}
        next->mapBackground=rectangle(-7,135,0,mapUv[0],mapUv[1],mapUv[2],mapUv[3]);
        next->models.resize(modelCount);std::uint64_t totalVertices=0;
        for(auto& model:next->models) {
            model.name=file.string();model.flags=file.u32();if(model.flags>7)throw std::runtime_error("Invalid first-person model flags");
            float t[3],r[3],s[3];for(float& v:t)v=file.f32();for(float& v:r)v=file.f32();for(float& v:s)v=file.f32();
            auto m=identity();scale(m,2,2,2);translate(m,t[0],t[1],t[2]);rotate(m,1,r[1]);rotate(m,0,r[0]);rotate(m,2,r[2]);scale(m,s[0],s[1],s[2]);scale(m,.5f,.5f,.5f);
            if(model.flags&4)rotate(m,1,180);
            translate(m,-.5f,-.5f,-.5f);model.display=m;
            const bool reversed=(s[0]<0)^(s[1]<0)^(s[2]<0);auto groups=file.u32();if(groups>16)throw std::runtime_error("Too many first-person mesh groups");
            for(unsigned group=0;group<groups;++group) {
                auto tint=file.u32(),count=file.u32();if((tint>4&&tint!=0xffffffffu)||count%3||count>1000000||(totalVertices+=count)>4000000)throw std::runtime_error("Invalid first-person mesh size/tint");
                auto mesh=std::make_shared<FirstPersonMesh>();mesh->vertices.resize(count);
                for(auto& vertex:mesh->vertices){vertex.x=file.f32();vertex.y=file.f32();vertex.z=file.f32();vertex.u=file.f32();vertex.v=file.f32();vertex.nx=file.f32();vertex.ny=file.f32();vertex.nz=file.f32();}
                std::shared_ptr<const FirstPersonMesh> flipped;
                if(reversed){auto copy=std::make_shared<FirstPersonMesh>(*mesh);for(std::size_t i=0;i<copy->vertices.size();i+=3)std::swap(copy->vertices[i+1],copy->vertices[i+2]);flipped=std::move(copy);}
                model.groups.push_back({tint==0xffffffffu?-1:int(tint),reversed?flipped:mesh,mesh,flipped});
            }
        }
        for(unsigned i=0;i<entryCount;++i){const auto id=file.u16(),meta=file.u16();Impl::Entry entry;entry.model=file.u32();for(auto& color:entry.colors)color=file.u32();if(entry.model>=modelCount||!next->entries.emplace(key(id,meta),entry).second)throw std::runtime_error("Invalid/duplicate first-person item mapping");}
        for(unsigned i=0;i<defaultCount;++i){const auto id=file.u32(),index=file.u32();if(id>65535||index>=modelCount)throw std::runtime_error("Invalid first-person default mapping");next->defaults[id]=index;}
        for(unsigned i=0;i<frameCount;++i){const auto id=file.u32(),index=file.u32();if(index>=modelCount)throw std::runtime_error("Invalid first-person frame mapping");next->frames[id]=index;}
        file.end();File pixels(directory+"/runtime/first_person/atlas.rgba");next->pixels.resize(static_cast<std::size_t>(imageBytes));pixels.bytes(next->pixels.data(),next->pixels.size());pixels.end();
        const auto transformsPath=directory+"/runtime/first_person/transforms.bin";
        if(std::ifstream(transformsPath,std::ios::binary)) {
            File transforms(transformsPath);transforms.magic("NVITRF01");if(transforms.u32()!=modelCount)throw std::runtime_error("Item transform sidecar/model count mismatch");
            for(auto& model:next->models) {
                if(transforms.string()!=model.name)throw std::runtime_error("Item transform sidecar/model name mismatch");
                const auto gui3d=transforms.u32();if(gui3d>1)throw std::runtime_error("Invalid item gui3d flag");model.gui3d=gui3d!=0;
                for(unsigned kind=0;kind<3;++kind) {
                    float t[3]{},r[3]{},s[3]{1,1,1};
                    if(kind){for(float& f:t)f=transforms.f32();for(float& f:r)f=transforms.f32();for(float& f:s)f=transforms.f32();}
                    auto m=identity();translate(m,t[0],t[1],t[2]);rotate(m,1,r[1]);rotate(m,0,r[0]);rotate(m,2,r[2]);scale(m,s[0],s[1],s[2]);scale(m,.5f,.5f,.5f);
                    if(model.flags&4)rotate(m,1,180);
                    translate(m,-.5f,-.5f,-.5f);model.worldDisplay[kind]=m;
                    model.reversed[kind]=(s[0]<0)^(s[1]<0)^(s[2]<0);if(kind==1)model.groundScale={{s[0],s[1],s[2]}};
                }
                if(model.reversed[1]||model.reversed[2])for(auto& group:model.groups)if(!group.reversedMesh){auto copy=std::make_shared<FirstPersonMesh>(*group.rawMesh);for(std::size_t i=0;i<copy->vertices.size();i+=3)std::swap(copy->vertices[i+1],copy->vertices[i+2]);group.reversedMesh=std::move(copy);}
            }
            transforms.end();next->worldReady=true;
        }
        const auto equipmentPath=directory+"/runtime/first_person/equipment.bin";
        if(std::ifstream(equipmentPath,std::ios::binary)) {
            File transforms(equipmentPath);transforms.magic("NVEQP001");if(transforms.u32()!=modelCount)throw std::runtime_error("Equipment transform model count mismatch");
            for(auto& model:next->models){if(transforms.string()!=model.name)throw std::runtime_error("Equipment transform model name mismatch");
                for(unsigned kind=3;kind<6;++kind){float t[3],r[3],s[3];for(float& f:t)f=transforms.f32();for(float& f:r)f=transforms.f32();for(float& f:s)f=transforms.f32();auto m=identity();translate(m,t[0],t[1],t[2]);rotate(m,1,r[1]);rotate(m,0,r[0]);rotate(m,2,r[2]);scale(m,s[0],s[1],s[2]);scale(m,.5f,.5f,.5f);if(model.flags&4)rotate(m,1,180);translate(m,-.5f,-.5f,-.5f);model.worldDisplay[kind]=m;model.reversed[kind]=(s[0]<0)^(s[1]<0)^(s[2]<0);}
                if(model.reversed[3]||model.reversed[4]||model.reversed[5])for(auto& group:model.groups)if(!group.reversedMesh){auto copy=std::make_shared<FirstPersonMesh>(*group.rawMesh);for(std::size_t i=0;i<copy->vertices.size();i+=3)std::swap(copy->vertices[i+1],copy->vertices[i+2]);group.reversedMesh=std::move(copy);}}
            transforms.end();next->equipmentReady=true;
        }
        impl_.swap(next);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}

bool FirstPersonAssets::worldModelsReady()const noexcept{return impl_->worldReady;}
bool FirstPersonAssets::equipmentModelsReady()const noexcept{return impl_->equipmentReady;}
ItemModelFrame FirstPersonAssets::itemModel(int itemId,int metadata,ItemModelTransform transform,int colorOverride,int textureFrame)const {
    ItemModelFrame result;if(!impl_->worldReady||impl_->models.empty()||itemId<0||itemId>65535)return result;
    unsigned kind=static_cast<unsigned>(transform);if(kind>5||(kind>=3&&!impl_->equipmentReady))return result;
    if(registry::item(itemId).maxDamage>0)metadata=0;
    auto found=impl_->entries.find(key(itemId,metadata));const Impl::Entry* entry=found==impl_->entries.end()?nullptr:&found->second;unsigned selected=entry?entry->model:0;
    if(!entry){auto fallback=impl_->defaults.find(unsigned(itemId));if(fallback!=impl_->defaults.end()){selected=fallback->second;auto color=impl_->entries.find(key(itemId,0));if(color!=impl_->entries.end())entry=&color->second;}}
    if(textureFrame>=0){auto variant=impl_->frames.find(key(itemId,textureFrame));if(variant!=impl_->frames.end())selected=variant->second;}
    const auto& model=impl_->models[selected];result.supported=(model.flags&1)!=0;result.approximate=(model.flags&2)!=0;result.gui3d=model.gui3d;result.groundScale=model.groundScale;result.builtIn=(model.flags&4)!=0;
    for(const auto& group:model.groups){std::uint32_t color=group.tint>=0&&entry?entry->colors[unsigned(group.tint)]:0xffffffffu;if(group.tint==0&&colorOverride>=0)color=0xff000000u|(std::uint32_t(colorOverride)&0xffffffu);
        result.draws.push_back({model.reversed[kind]?group.reversedMesh:group.rawMesh,model.worldDisplay[kind],FirstPersonTexture::ItemAtlas,color,false});}
    return result;
}

void FirstPersonAssets::appendGlint(std::vector<FirstPersonDraw>& draws,std::int64_t time) {
    const auto count=draws.size();draws.reserve(count*3);
    for(int pass=0;pass<2;++pass){auto uv=identity();scale(uv,8,8,8);const auto cycle=pass?4873:3000;const float offset=float(time%cycle)/float(cycle)/8.f;translate(uv,pass?-offset:offset,0,0);rotate(uv,2,pass?10.f:-50.f);
        for(std::size_t i=0;i<count;++i)if(draws[i].texture==FirstPersonTexture::ItemAtlas&&!draws[i].glint){auto d=draws[i];d.glint=true;d.color=0xff8040ccu;d.textureMatrix=uv;draws.push_back(std::move(d));}}
}

FirstPersonFrame FirstPersonAssets::build(const FirstPersonPose& p)const {
    FirstPersonFrame result;
    auto addArm=[&](Matrix m,bool left,bool map) {
        result.draws.push_back({impl_->armMesh(p.slim,left,false,map),m,FirstPersonTexture::PlayerSkin});
        if(left?p.leftSleeve:p.rightSleeve)result.draws.push_back({impl_->armMesh(p.slim,left,true,map),m,FirstPersonTexture::PlayerSkin});
    };
    if(p.itemId<0){result.supported=true;if(!p.invisible)addArm(armTransform(p),false,false);return result;}
    if(impl_->models.empty())return result;
    auto base=itemTransform(p);
    if(p.itemId==358&&impl_->mapBackground) {
        auto m=mapBase(p);
        if(!p.invisible){auto right=m;rotate(right,1,54);rotate(right,0,64);rotate(right,2,-62);translate(right,.25f,-.85f,.75f);addArm(right,false,true);
            auto left=m;rotate(left,1,92);rotate(left,0,45);rotate(left,2,41);translate(left,-.3f,-1.1f,.45f);addArm(left,true,true);}
        base=mapSurface(m,p);result.draws.push_back({impl_->mapBackground,base,FirstPersonTexture::ItemAtlas});
        if(p.mapDataAvailable)result.draws.push_back({impl_->mapData,base,FirstPersonTexture::MapData});else result.approximate=true;
    }
    const auto& info=registry::item(p.itemId);int metadata=info.maxDamage>0?0:(p.metadata&65535);
    auto found=impl_->entries.find(key(p.itemId,metadata));const Impl::Entry* entry=found==impl_->entries.end()?nullptr:&found->second;unsigned selected=entry?entry->model:0;
    if(!entry){auto fallback=impl_->defaults.find(unsigned(p.itemId));if(fallback!=impl_->defaults.end()){selected=fallback->second;auto color=impl_->entries.find(key(p.itemId,0));if(color!=impl_->entries.end())entry=&color->second;}}
    int frame=p.textureFrame;
    if(p.itemId==261){int elapsed=p.maxUseTicks-p.useTicksRemaining;frame=p.usingItem?(elapsed>=18?3:elapsed>13?2:elapsed>0?1:0):0;}
    if(p.itemId==346)frame=p.fishingCast?1:0;
    if(frame>=0){auto variant=impl_->frames.find(key(p.itemId,frame));if(variant!=impl_->frames.end())selected=variant->second;}
    const auto& model=impl_->models[selected];result.supported=(model.flags&1)!=0;result.approximate=result.approximate||(model.flags&2)!=0;
    const auto matrix=multiply(base,model.display);const bool translucent=p.itemId==79||p.itemId==95||p.itemId==160||p.itemId==165;
    std::vector<FirstPersonDraw> itemDraws;
    for(const auto& group:model.groups){std::uint32_t color=group.tint>=0&&entry?entry->colors[unsigned(group.tint)]:0xffffffffu;if(group.tint==0&&p.colorOverride>=0)color=0xff000000u|(std::uint32_t(p.colorOverride)&0xffffffu);
        itemDraws.push_back({group.mesh,matrix,FirstPersonTexture::ItemAtlas,color,translucent});}
    if(p.enchanted&&!(model.flags&4))appendGlint(itemDraws,p.renderTimeMillis);
    result.draws.insert(result.draws.end(),itemDraws.begin(),itemDraws.end());
    return result;
}
} // namespace mc
