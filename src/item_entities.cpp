#include "mc/item_entities.hpp"
#include "mc/world.hpp"
#include "mc/entity_visuals.hpp"

#include <algorithm>
#include <cmath>

namespace mc {
namespace {
using Matrix=FirstPersonMatrix;
constexpr float pi=3.14159265358979323846f;
Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
Matrix multiply(const Matrix& a,const Matrix& b){Matrix result{};for(int c=0;c<4;++c)for(int r=0;r<4;++r)for(int k=0;k<4;++k)result[c*4+r]+=a[k*4+r]*b[c*4+k];return result;}
void translate(Matrix& m,float x,float y,float z){auto t=identity();t[12]=x;t[13]=y;t[14]=z;m=multiply(m,t);}
void scale(Matrix& m,float x,float y,float z){auto t=identity();t[0]=x;t[5]=y;t[10]=z;m=multiply(m,t);}
void rotate(Matrix& m,int axis,float degrees){double angle=double(degrees)*3.14159265358979323846/180.0;float c=float(std::cos(angle)),s=float(std::sin(angle));auto r=identity();if(axis==0){r[5]=c;r[9]=-s;r[6]=s;r[10]=c;}else{r[0]=c;r[8]=s;r[2]=-s;r[10]=c;}m=multiply(m,r);}
struct CopyRandom {
    std::uint64_t state=(187ull^0x5deece66dull)&((1ull<<48)-1);
    float next(){state=(state*0x5deece66dull+11)&((1ull<<48)-1);return float(state>>24)/16777216.f;}
};
const std::array<std::shared_ptr<const FirstPersonMesh>,11>& orbMeshes() {
    static const auto meshes=[] {std::array<std::shared_ptr<const FirstPersonMesh>,11> result;
        for(unsigned index=0;index<11;++index){auto mesh=std::make_shared<FirstPersonMesh>();float u0=float(index%4)*.25f,u1=u0+.25f,v0=float(index/4)*.25f,v1=v0+.25f;
            const FirstPersonVertex corners[]={{-.5f,-.25f,0,u0,v1,0,1,0},{.5f,-.25f,0,u1,v1,0,1,0},{.5f,.75f,0,u1,v0,0,1,0},{-.5f,.75f,0,u0,v0,0,1,0}};
            for(int vertex:{0,1,2,0,2,3})mesh->vertices.push_back(corners[vertex]);
            result[index]=std::move(mesh);}
        return result;}();return meshes;
}
} // namespace

int ItemEntityVisuals::stackCopies(int count)noexcept{return count>48?5:count>32?4:count>16?3:count>1?2:1;}
int ItemEntityVisuals::orbTexture(int amount)noexcept{return amount>=2477?10:amount>=1237?9:amount>=617?8:amount>=307?7:amount>=149?6:amount>=73?5:amount>=37?4:amount>=17?3:amount>=7?2:amount>=3?1:0;}
std::uint32_t ItemEntityVisuals::orbColor(int colorTicks,float partialTicks){const float phase=(float(colorTicks)+partialTicks)/2.f;const auto red=std::uint32_t((javaSin(phase)+1.f)*.5f*255.f),blue=std::uint32_t((javaSin(phase+4.1887903f)+1.f)*.1f*255.f);return 0x8000ff00u|(red<<16)|blue;}
std::uint32_t ItemEntityVisuals::orbLight(std::uint32_t light)noexcept{return std::min(240u,(light&255u)+120u)|(light&0xff0000u);}

std::vector<FirstPersonMatrix> ItemEntityVisuals::droppedTransforms(const DroppedItemPose& p,bool gui3d,const std::array<float,3>& groundScale) {
    std::vector<FirstPersonMatrix> result;if(p.itemId<0)return result;const int copies=stackCopies(p.count);result.reserve(std::size_t(copies));
    const float age=float(p.ageTicks)+p.partialTicks,bob=javaSin(age/10.f+p.hoverStart)*.1f+.1f;auto base=identity();
    translate(base,float(p.x),float(p.y)+bob+.25f*groundScale[1],float(p.z));
    if(gui3d||p.cameraOptionsPresent)rotate(base,1,(age/20.f+p.hoverStart)*(180.f/pi));
    if(!gui3d)translate(base,-0.f*float(copies-1)*.5f,-0.f*float(copies-1)*.5f,-.046875f*float(copies-1)*.5f);
    CopyRandom random;
    for(int copy=0;copy<copies;++copy){auto m=base;if(gui3d){if(copy){const float x=(random.next()*2.f-1.f)*.15f,y=(random.next()*2.f-1.f)*.15f,z=(random.next()*2.f-1.f)*.15f;translate(m,x,y,z);}scale(m,.5f,.5f,.5f);}
        result.push_back(m);if(!gui3d)translate(base,0.f*groundScale[0],0.f*groundScale[1],.046875f*groundScale[2]);}
    return result;
}
FirstPersonMatrix ItemEntityVisuals::orbTransform(const XPOrbPose& p){auto m=identity();translate(m,float(p.x),float(p.y),float(p.z));rotate(m,1,180.f-p.viewYaw);rotate(m,0,-p.viewPitch);scale(m,.3f,.3f,.3f);return m;}

ItemEntityFrame ItemEntityVisuals::dropped(const FirstPersonAssets& assets,const DroppedItemPose& p,TileEntityVisuals* tiles,SkinCache* skins) {
    ItemEntityFrame result;result.packedLight=p.packedLight;if(p.itemId<0)return result;
    VisualItem item{p.itemId,p.metadata,p.count,p.nbt};auto model=EntityVisuals::item(assets,item,ItemModelTransform::Ground,tiles,skins,p.textureFrame,p.renderTimeMillis);
    if(p.colorOverride>=0&&!model.builtIn){model=assets.itemModel(p.itemId,p.metadata,ItemModelTransform::Ground,p.colorOverride,p.textureFrame);if(EntityVisuals::hasEffect(item))FirstPersonAssets::appendGlint(model.draws,p.renderTimeMillis);}
    result.supported=model.supported;result.approximate=model.approximate;
    if(model.draws.empty())return result;
    auto transforms=droppedTransforms(p,model.gui3d,model.groundScale);result.copies=int(transforms.size());result.draws.reserve(transforms.size()*model.draws.size());
    for(const auto& transform:transforms)for(const auto& group:model.draws)result.draws.push_back({group.mesh,multiply(transform,group.matrix),ItemEntityTexture::ItemAtlas,group.color,group.dynamicTexture,group.glint,group.textureMatrix});
    return result;
}
ItemEntityFrame ItemEntityVisuals::orb(const XPOrbPose& p) {
    ItemEntityFrame result;result.supported=true;result.copies=1;result.packedLight=orbLight(p.packedLight);
    result.draws.push_back({orbMeshes()[std::size_t(orbTexture(p.amount))],orbTransform(p),ItemEntityTexture::ExperienceOrb,orbColor(p.xpColor,p.partialTicks)});return result;
}
FirstPersonMatrix ItemEntityVisuals::thrownTransform(const ThrownItemPose& p){auto m=identity();translate(m,float(p.x),float(p.y),float(p.z));scale(m,.5f,.5f,.5f);rotate(m,1,p.fishingHook?180.f-p.viewYaw:-p.viewYaw);rotate(m,0,p.fishingHook?-p.viewPitch:p.viewPitch);return m;}
ItemEntityFrame ItemEntityVisuals::thrown(const FirstPersonAssets& assets,const ThrownItemPose& p){
    ItemEntityFrame result;result.packedLight=p.packedLight;result.copies=1;const auto transform=thrownTransform(p);
    if(p.fishingHook){
        static const auto mesh=[] {auto value=std::make_shared<FirstPersonMesh>();const FirstPersonVertex corners[]={{-.5f,-.5f,0,.0625f,.1875f,0,1,0},{.5f,-.5f,0,.125f,.1875f,0,1,0},{.5f,.5f,0,.125f,.125f,0,1,0},{-.5f,.5f,0,.0625f,.125f,0,1,0}};for(int index:{0,1,2,0,2,3})value->vertices.push_back(corners[index]);return value;}();
        result.supported=true;result.draws.push_back({mesh,transform,ItemEntityTexture::ParticleAtlas});return result;
    }
    const auto model=EntityVisuals::item(assets,{p.itemId,p.metadata,1,p.nbt},ItemModelTransform::Ground,nullptr,nullptr,-1,p.renderTimeMillis);
    result.supported=model.supported;result.approximate=model.approximate;
    for(const auto& draw:model.draws)result.draws.push_back({draw.mesh,multiply(transform,draw.matrix),ItemEntityTexture::ItemAtlas,draw.color,draw.dynamicTexture,draw.glint,draw.textureMatrix});
    return result;
}
} // namespace mc
