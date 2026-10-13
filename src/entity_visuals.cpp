#include "mc/entity_visuals.hpp"
#include "mc/registry.hpp"
#include "mc/nbt.hpp"
#include "mc/world.hpp"
#include "mc/brewing_data.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace mc {
namespace {
using Matrix=FirstPersonMatrix;
constexpr float pi=3.14159265358979323846f;
Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
Matrix multiply(const Matrix& a,const Matrix& b){Matrix r{};for(int c=0;c<4;++c)for(int y=0;y<4;++y)for(int k=0;k<4;++k)r[c*4+y]+=a[k*4+y]*b[c*4+k];return r;}
void translate(Matrix& m,float x,float y,float z){auto t=identity();t[12]=x;t[13]=y;t[14]=z;m=multiply(m,t);}
void scale(Matrix& m,float x,float y,float z){auto t=identity();t[0]=x;t[5]=y;t[10]=z;m=multiply(m,t);}
void rotate(Matrix& m,int axis,float degrees){double angle=double(degrees)*3.14159265358979323846/180.;float c=float(std::cos(angle)),s=float(std::sin(angle));auto r=identity();if(axis==0){r[5]=c;r[9]=-s;r[6]=s;r[10]=c;}else if(axis==1){r[0]=c;r[8]=s;r[2]=-s;r[10]=c;}else{r[0]=c;r[4]=-s;r[1]=s;r[5]=c;}m=multiply(m,r);}
float wrap(float value){value=std::fmod(value,360.f);if(value>=180)value-=360;if(value< -180)value+=360;return value;}
void alignPrevious(float& previous,float current){while(current-previous< -180)previous-=360;while(current-previous>=180)previous+=360;}
float boundedAngle(float target,float angle,float limit){float delta=wrap(target-angle);if(delta< -limit)delta=-limit;if(delta>=limit)delta=limit;return target-delta;}
double javaAtan2(double y,double x) {
    const double length=x*x+y*y;if(std::isnan(length))return length;
    const bool negativeY=y<0,negativeX=x<0;if(negativeY)y=-y;if(negativeX)x=-x;const bool swapped=y>x;if(swapped)std::swap(x,y);
    double value=length;std::uint64_t bits;std::memcpy(&bits,&value,8);bits=6910469410427058090ull-(bits>>1);std::memcpy(&value,&bits,8);value=value*(1.5-.5*length*value*value);x*=value;y*=value;
    std::uint64_t biasBits=4805340802404319232ull;double bias;std::memcpy(&bias,&biasBits,8);double rounded=bias+y;std::memcpy(&bits,&rounded,8);unsigned index=unsigned(bits);
    static const auto tables=[] {std::array<std::array<double,257>,2> t{};for(int i=0;i<257;++i){t[0][i]=std::asin(double(i)/256.);t[1][i]=std::cos(t[0][i]);}return t;}();
    if(index>256)return std::atan2(negativeY?-y:y,negativeX?-x:x);
    double residual=y*tables[1][index]-x*(rounded-bias);double angle=tables[0][index]+(6+residual*residual)*residual*.16666666666666666;
    if(swapped)angle=3.14159265358979323846/2-angle;if(negativeX)angle=3.14159265358979323846-angle;if(negativeY)angle=-angle;return angle;
}
bool isStand(int kind,int type){return (kind==1&&type==30)||(kind==2&&type==78);}
bool isHumanoid(int kind,int type){return kind==0||isStand(kind,type)||(kind==1&&(type==51||type==53||type==54||type==57));}
nbt::Tag tags(const VisualItem& item){return item.nbt?nbt::parse(*item.nbt):nbt::Tag{};}
int number(const nbt::Tag* tag){return tag&&tag->type>=1&&tag->type<=4?int(tag->integer):0;}
int signedByte(const nbt::Tag* tag){int n=number(tag)&255;return n>=128?n-256:n;}
#include "entity_visual_data.inc"
}

struct EntityAnimationTracker::Impl {
    struct State {EntityAnimationTick input;double x=0,z=0;float yaw=0,previousYaw=0,pitch=0,previousPitch=0,head=0,previousHead=0,body=0,previousBody=0;
        float amount=0,previousAmount=0,limb=0,swing=0,previousSwing=0,helperHead=0;int helperTicks=0,swingTicks=0,useRemaining=0;bool swinging=false,usingItem=false;};
    std::unordered_map<int,State> entities;
};
EntityAnimationTracker::EntityAnimationTracker():impl_(new Impl){}
EntityAnimationTracker::~EntityAnimationTracker()=default;
void EntityAnimationTracker::erase(int id){impl_->entities.erase(id);}
void EntityAnimationTracker::prune(std::uint64_t tick,std::uint64_t maximumIdleTicks){for(auto i=impl_->entities.begin();i!=impl_->entities.end();){const auto updated=i->second.input.tick;if(tick<updated||tick-updated>maximumIdleTicks)i=impl_->entities.erase(i);else ++i;}}
std::size_t EntityAnimationTracker::size()const noexcept{return impl_->entities.size();}
void EntityAnimationTracker::reset(){impl_->entities.clear();}
void EntityAnimationTracker::tick(const EntityAnimationTick& input) {
    if(!std::isfinite(input.x)||!std::isfinite(input.z)||!std::isfinite(input.yaw)||!std::isfinite(input.pitch)||!std::isfinite(input.headYaw))return;
    auto found=impl_->entities.find(input.id);
    if(found==impl_->entities.end()){if(impl_->entities.size()>=65536)return;Impl::State initial;initial.input=input;initial.x=input.x;initial.z=input.z;initial.body=initial.previousBody=initial.yaw=initial.previousYaw=input.yaw;initial.pitch=initial.previousPitch=input.pitch;initial.head=initial.previousHead=input.headYaw;found=impl_->entities.emplace(input.id,std::move(initial)).first;}
    else if(input.tick<=found->second.input.tick)return;
    auto& s=found->second;const double dx=input.x-s.x,dz=input.z-s.z;const float oldRawHead=s.input.headYaw;
    s.previousYaw=s.yaw;s.previousPitch=s.pitch;s.previousHead=s.head;s.previousBody=s.body;s.previousAmount=s.amount;s.previousSwing=s.swing;
    s.yaw=input.yaw;s.pitch=input.pitch;if(input.headYaw!=oldRawHead||input.player||input.armorStand)s.head=input.headYaw;
    const int duration=std::max(1,input.swingDuration);
    if(input.swingProgress>=0){s.swing=input.swingProgress;if(input.previousSwingProgress>=0)s.previousSwing=input.previousSwingProgress;s.swinging=false;}
    else {if(input.swingTriggered&&(!s.swinging||s.swingTicks>=duration/2||s.swingTicks<0)){s.swinging=true;s.swingTicks=-1;}if(s.swinging){if(++s.swingTicks>=duration){s.swingTicks=0;s.swinging=false;}}else s.swingTicks=0;s.swing=float(s.swingTicks)/float(duration);}
    float distance=float(std::sqrt(dx*dx+dz*dz))*4.f;if(distance>1)distance=1;s.amount+=(distance-s.amount)*.4f;s.limb+=s.amount;
    if(input.armorStand){s.previousBody=s.previousYaw;s.body=s.yaw;}
    else if(input.player){float target=s.body;float movement=float(dx*dx+dz*dz);if(movement>.0025000002f)target=float(javaAtan2(dz,dx))*180.f/pi-90.f;if(s.swing>0)target=s.yaw;
        s.body+=wrap(target-s.body)*.3f;float gap=wrap(s.yaw-s.body);if(gap< -75)gap=-75;if(gap>=75)gap=75;s.body=s.yaw-gap;if(gap*gap>2500)s.body+=gap*.2f;
    } else {
        if(dx*dx+dz*dz>2.500000277905201E-7){s.body=s.yaw;s.head=boundedAngle(s.body,s.head,75);s.helperHead=s.head;s.helperTicks=0;}
        else{float limit=75;if(std::abs(s.head-s.helperHead)>15){s.helperTicks=0;s.helperHead=s.head;}else{++s.helperTicks;if(s.helperTicks>10)limit=std::max(1.f-float(s.helperTicks-10)/10.f,0.f)*75.f;}s.body=boundedAngle(s.head,s.body,limit);}
    }
    alignPrevious(s.previousYaw,s.yaw);alignPrevious(s.previousPitch,s.pitch);alignPrevious(s.previousHead,s.head);alignPrevious(s.previousBody,s.body);
    if(input.usingItem){if(!s.usingItem)s.useRemaining=input.useDuration;else if(s.useRemaining>0)--s.useRemaining;}else s.useRemaining=0;
    s.usingItem=input.usingItem;s.x=input.x;s.z=input.z;s.input=input;
}
EntityAnimationSample EntityAnimationTracker::sample(int id,float partial)const {
    EntityAnimationSample result;auto found=impl_->entities.find(id);if(found==impl_->entities.end())return result;const auto& s=found->second;partial=std::isfinite(partial)?std::clamp(partial,0.f,1.f):0.f;
    result.valid=true;result.bodyYaw=s.previousBody+wrap(s.body-s.previousBody)*partial;result.headYaw=s.previousHead+wrap(s.head-s.previousHead)*partial;result.pitch=s.previousPitch+(s.pitch-s.previousPitch)*partial;
    result.previousYaw=s.previousYaw;result.yaw=s.yaw;result.previousPitch=s.previousPitch;result.pose=s.input.pose;result.pose.headYaw=(result.headYaw-result.bodyYaw)/(180.f/pi);result.pose.headPitch=result.pitch/(180.f/pi);result.pose.bodyYaw=result.bodyYaw/(180.f/pi);
    result.pose.limbSwing=s.limb-s.amount*(1.f-partial);if(result.pose.child)result.pose.limbSwing*=3;result.pose.limbSwingAmount=std::min(1.f,s.previousAmount+(s.amount-s.previousAmount)*partial);result.pose.ageTicks=s.input.pose.ageTicks+partial;result.pose.partialTick=partial;
    float swingDelta=s.swing-s.previousSwing;if(swingDelta<0)swingDelta+=1;result.pose.swingProgress=s.previousSwing+swingDelta*partial;result.usingItem=s.usingItem;result.useTicksRemaining=s.useRemaining;return result;
}

int EntityVisuals::primaryItemColor(const VisualItem& item) {
    if(!item.present()||!item.nbt)return -1;
    try {auto root=tags(item);
        if(item.id>=298&&item.id<=301){const auto* display=root.get("display");const auto* color=display?display->get("color"):nullptr;if(color&&color->type==3)return int(std::uint32_t(color->integer)&0xffffffu);}
        if(item.id==373){const auto* effects=root.get("CustomPotionEffects");if(!effects||effects->type!=9)return -1;float rgb[3]{},count=0;bool any=false;
            if(effects->subtype==10)for(const auto& effect:effects->list){int id=signedByte(effect.get("Id"));if(id<0||id>=32||potionLiquidColors[id]<0)continue;any=true;const auto* show=effect.get("ShowParticles");if(show&&show->type==1&&!show->integer)continue;int amplifier=signedByte(effect.get("Amplifier"));unsigned color=unsigned(potionLiquidColors[id]);for(int i=0;i<=amplifier;++i){rgb[0]+=float((color>>16)&255)/255.f;rgb[1]+=float((color>>8)&255)/255.f;rgb[2]+=float(color&255)/255.f;++count;}}
            if(!any)return 3694022;if(count==0)return 0;return (int(rgb[0]/count*255.f)<<16)|(int(rgb[1]/count*255.f)<<8)|int(rgb[2]/count*255.f);
        }
    }catch(const std::exception&){}return -1;
}
bool EntityVisuals::enchanted(const VisualItem& item){if(!item.present()||!item.nbt)return false;try{auto root=tags(item);auto value=root.get("ench");return value&&value->type==9;}catch(const std::exception&){return false;}}
bool EntityVisuals::hasEffect(const VisualItem& item) {
    if(!item.present())return false;
    if(item.id==403)return true; // ItemEnchantedBook always has a glint.
    if(item.id!=373)return enchanted(item);
    try {auto root=tags(item);const auto* effects=root.get("CustomPotionEffects");
        if(effects&&effects->type==9){if(effects->subtype==10)for(const auto& effect:effects->list){int id=signedByte(effect.get("Id"));if(id>0&&id<32&&potionLiquidColors[id]>=0)return true;}return false;}
    }catch(const std::exception&){}
    return brewing::data::effects[unsigned(item.metadata)&32767u]!=0;
}

ItemModelFrame EntityVisuals::item(const FirstPersonAssets& assets,const VisualItem& item,ItemModelTransform transform,TileEntityVisuals* tiles,SkinCache* skins,int textureFrame,std::int64_t time) {
    auto result=assets.itemModel(item.id,item.metadata,transform,primaryItemColor(item),textureFrame);
    if(result.builtIn&&tiles&&!result.draws.empty()){static const std::vector<std::uint8_t> empty{0};auto builtin=tiles->builtinItem(item.id,item.metadata,item.nbt?*item.nbt:empty,skins);
        if(builtin.status==TileRenderStatus::Drawn){auto outer=result.draws[0].matrix;result.draws.clear();for(const auto& d:builtin.draws){FirstPersonDraw draw;draw.mesh=d.mesh;draw.matrix=multiply(outer,d.matrix);draw.color=d.color;draw.textureMatrix=d.textureMatrix;draw.dynamicTexture=d.texture;result.draws.push_back(std::move(draw));}result.approximate=builtin.approximate;}}
    if(hasEffect(item)&&!result.builtIn)FirstPersonAssets::appendGlint(result.draws,time);return result;
}
FirstPersonFrame EntityVisuals::firstPerson(const FirstPersonAssets& assets,const FirstPersonPose& pose,const VisualItem& item,TileEntityVisuals* tiles,SkinCache* skins) {
    auto p=pose;p.enchanted=p.enchanted||hasEffect(item);if(p.colorOverride<0)p.colorOverride=primaryItemColor(item);auto result=assets.build(p);
    if(tiles&&result.supported&&!result.draws.empty()&&(p.itemId==397||p.itemId==425||p.itemId==54||p.itemId==146||p.itemId==130)){static const std::vector<std::uint8_t> empty{0};auto builtin=tiles->builtinItem(p.itemId,p.metadata,item.nbt?*item.nbt:empty,skins);
        if(builtin.status==TileRenderStatus::Drawn){auto outer=result.draws[0].matrix;result.draws.clear();for(const auto& d:builtin.draws){FirstPersonDraw draw;draw.mesh=d.mesh;draw.matrix=multiply(outer,d.matrix);draw.color=d.color;draw.textureMatrix=d.textureMatrix;draw.dynamicTexture=d.texture;result.draws.push_back(std::move(draw));}result.approximate=builtin.approximate;}}
    return result;
}

PlayerVisualFrame EntityVisuals::player(const EntityModels& models,const FirstPersonAssets& items,const PlayerVisualInput& input,TileEntityVisuals* tiles,SkinCache* skins) {
    PlayerVisualFrame result;if(!isHumanoid(input.kind,input.type))return result;auto pose=input.pose;const auto& held=input.equipment[0];pose.heldItemLeft=0;pose.heldItemRight=held.present()?(input.usingItem&&input.useAction==3?3:1):0;pose.aimedBow=input.usingItem&&input.useAction==4;
    if(!input.invisible||input.showInvisibleSkin){EntityLayer body;body.mesh=models.build(input.kind,input.type,pose);body.playerSkin=input.kind==0;if(input.invisible)body.color[3]=.15f;result.supported=body.mesh.supported;result.approximate=body.mesh.approximate;if(body.mesh.supported)result.layers.push_back(std::move(body));}else result.supported=true;
    if(pose.spectator)return result;
    for(int slot=4;slot>=1;--slot){const auto& item=input.equipment[std::size_t(slot)];if(!item.present())continue;auto layers=models.humanoidArmor(input.kind,input.type,item.id,pose,primaryItemColor(item),enchanted(item),4-slot);result.layers.insert(result.layers.end(),std::make_move_iterator(layers.begin()),std::make_move_iterator(layers.end()));}
    if(held.present()){
        int id=input.fishing&&input.kind==0?346:held.id,frame=-1;
        if(id==261&&input.usingItem){int elapsed=input.maxUseTicks-input.useTicksRemaining;frame=elapsed>=18?3:elapsed>13?2:elapsed>0?1:0;}
        auto visible=held;visible.id=id;auto model=EntityVisuals::item(items,visible,ItemModelTransform::ThirdPerson,tiles,skins,frame,input.renderTimeMillis);auto matrix=models.humanoidHand(input.kind,input.type,pose);
        if(id==54||id==130||id==146){translate(matrix,0,.1875f,-.3125f);rotate(matrix,0,20);rotate(matrix,1,45);scale(matrix,-.375f,-.375f,.375f);}
        if(pose.sneaking)translate(matrix,0,.203125f,0);scale(matrix,2,2,2);
        for(auto draw:model.draws){draw.matrix=multiply(matrix,draw.matrix);result.held.push_back(std::move(draw));}result.approximate=result.approximate||model.approximate||!model.supported;
    }
    const auto& head=input.equipment[4];if(head.present()&&head.id==397&&tiles){static const std::vector<std::uint8_t> empty{0};auto skull=tiles->skullItem(head.metadata,head.nbt?*head.nbt:empty,skins,180);auto m=models.humanoidHead(input.kind,input.type,pose);scale(m,1.1875f,-1.1875f,-1.1875f);scale(m,.5f,.5f,.5f);translate(m,-.5f,0,-.5f);for(auto draw:skull.draws){draw.matrix=multiply(m,draw.matrix);result.customHead.push_back(std::move(draw));}result.approximate=result.approximate||skull.approximate;}
    else if(head.present()&&registry::item(head.id).blockId>=0){auto model=EntityVisuals::item(items,head,ItemModelTransform::Head,tiles,skins,-1,input.renderTimeMillis);auto m=models.humanoidHead(input.kind,input.type,pose);translate(m,0,-.25f,0);rotate(m,1,180);scale(m,.625f,-.625f,-.625f);scale(m,2,2,2);for(auto draw:model.draws){draw.matrix=multiply(m,draw.matrix);result.held.push_back(std::move(draw));}result.approximate=result.approximate||model.approximate;}
    return result;
}

ArrowFrame EntityVisuals::arrow(const ArrowPose& p) {
    static const auto meshes=[] {std::array<std::shared_ptr<const FirstPersonMesh>,3> result;const float f=.05625f;const FirstPersonVertex vertices[3][4]={
        {{-7,-2,-2,0,.15625f,f,0,0},{-7,-2,2,.15625f,.15625f,f,0,0},{-7,2,2,.15625f,.3125f,f,0,0},{-7,2,-2,0,.3125f,f,0,0}},
        {{-7,2,-2,0,.15625f,-f,0,0},{-7,2,2,.15625f,.15625f,-f,0,0},{-7,-2,2,.15625f,.3125f,-f,0,0},{-7,-2,-2,0,.3125f,-f,0,0}},
        {{-8,-2,0,0,0,0,0,f},{8,-2,0,.5f,0,0,0,f},{8,2,0,.5f,.15625f,0,0,f},{-8,2,0,0,.15625f,0,0,f}}};
        for(int part=0;part<3;++part){auto mesh=std::make_shared<FirstPersonMesh>();for(int i:{0,1,2,0,2,3})mesh->vertices.push_back(vertices[part][i]);result[part]=std::move(mesh);}return result;}();
    auto m=identity();translate(m,float(p.x),float(p.y),float(p.z));rotate(m,1,p.previousYaw+(p.yaw-p.previousYaw)*p.partialTicks-90);rotate(m,2,p.previousPitch+(p.pitch-p.previousPitch)*p.partialTicks);float shake=float(p.shakeTicks)-p.partialTicks;if(shake>0)rotate(m,2,-javaSin(shake*3.f)*shake);rotate(m,0,45);scale(m,.05625f,.05625f,.05625f);translate(m,-4,0,0);
    ArrowFrame result;result.draws.push_back({meshes[0],m});result.draws.push_back({meshes[1],m});for(int i=0;i<4;++i){rotate(m,0,90);result.draws.push_back({meshes[2],m});}return result;
}
FirstPersonMatrix EntityVisuals::inventoryTransform(float x,float y,float size,bool gui3d){auto m=identity();translate(m,x,y,100);scale(m,size/16.f,size/16.f,size/16.f);translate(m,8,8,0);scale(m,1,1,-1);scale(m,.5f,.5f,.5f);if(gui3d){scale(m,40,40,40);rotate(m,0,210);rotate(m,1,-135);}else{scale(m,64,64,64);rotate(m,0,180);}return m;}
std::array<std::array<double,3>,17> EntityVisuals::fishingLine(const FishingLinePose& p) {
    // RenderFish.doRender and Vec3.rotatePitch/rotateYaw, including float
    // subtraction before the sixteen-segment line is assembled.
    const float swing=javaSin(float(std::sqrt(std::clamp(p.swing,0.f,1.f)))*pi);
    std::array<double,3> v{{-.36,.03,.35}};
    auto pitch=[&](float a){const double c=javaCos(a),s=javaSin(a);v={{v[0],v[1]*c+v[2]*s,v[2]*c-v[1]*s}};};
    auto yaw=[&](float a){const double c=javaCos(a),s=javaSin(a);v={{v[0]*c+v[2]*s,v[1],v[2]*c-v[0]*s}};};
    pitch(-p.pitch*pi/180.f);yaw(-p.yaw*pi/180.f);yaw(swing*.5f);pitch(-swing*.7f);
    double x=p.anglerX+v[0],y=p.anglerY+v[1],z=p.anglerZ+v[2],eye=p.eyeHeight;
    if(p.thirdPerson||!p.localAngler){
        const float angle=p.bodyYaw*pi/180.f;const double s=javaSin(angle),c=javaCos(angle);
        x=p.anglerX-c*.35-s*.8;y=p.anglerY+eye-.45;z=p.anglerZ-s*.35+c*.8;eye=p.sneaking?-.1875:0;
    }
    const double dx=double(float(x-p.hookX)),dy=double(float(y-(p.hookY+.25)))+eye,dz=double(float(z-p.hookZ));
    std::array<std::array<double,3>,17> points{};
    for(int i=0;i<=16;++i){const float f=float(i)/16.f;points[i]={{p.hookX+dx*double(f),p.hookY+dy*double(f*f+f)*.5+.25,p.hookZ+dz*double(f)}};}
    return points;
}
} // namespace mc
