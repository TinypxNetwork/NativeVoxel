#include "mc/entity_models.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace mc {
namespace {
constexpr float pi = 3.14159265358979323846f;
struct Vec { float x=0, y=0, z=0; };
Vec operator+(Vec a,Vec b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vec operator-(Vec a,Vec b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vec operator*(Vec a,float s) { return {a.x*s,a.y*s,a.z*s}; }
Vec rotate(Vec p,Vec a) {
    // ModelRenderer.render: translate * Rz * Ry * Rx; local X applies first.
    float c=std::cos(a.x),s=std::sin(a.x); p={p.x,p.y*c-p.z*s,p.y*s+p.z*c};
    c=std::cos(a.y);s=std::sin(a.y);p={p.x*c+p.z*s,p.y,-p.x*s+p.z*c};
    c=std::cos(a.z);s=std::sin(a.z);return {p.x*c-p.y*s,p.x*s+p.y*c,p.z};
}
Vec normal(Vec a,Vec b,Vec c) {
    Vec u=b-a,v=c-a,n={u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
    float length=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
    return length>1e-9f?n*(1/length):Vec{};
}
struct Box {
    Vec pos,size; float u,v,inflate=0; bool mirror=false;
    bool exactBounds=false; Vec exactLo{},exactHi{};
};
struct Part {
    Vec pivot,angle;
    int parent=-1;
    float renderScale=1;
    Vec renderOffset;
    Vec offset,stretch{1,1,1};
    const char* name="";
    bool visible=true,rotateYXZ=false;
    std::vector<Box> boxes;
};
struct Geometry {
    std::vector<Part> parts;
    float textureWidth=64,textureHeight=32,scale=1,originY=24;
    Vec modelAngle,modelOffset,worldOffset;
    int part(Vec pivot={},Vec angle={},int parent=-1) {
        Part p;p.pivot=pivot;p.angle=angle;p.parent=parent;
        parts.push_back(std::move(p));return static_cast<int>(parts.size()-1);
    }
    void box(int p,float u,float v,Vec pos,Vec size,float inflate=0,bool mirror=false) {
        parts[p].boxes.push_back({pos,size,u,v,inflate,mirror});
    }
    void exactBox(int p,float u,float v,Vec pos,Vec size,Vec lo,Vec hi,bool mirror) {
        Box b{pos,size,u,v,0,mirror};b.exactBounds=true;b.exactLo=lo;b.exactHi=hi;parts[p].boxes.push_back(b);
    }
    Part& named(const char* name) {
        for(auto& p:parts)if(std::strcmp(p.name,name)==0)return p;
        throw std::runtime_error(std::string("Missing entity model part: ")+name);
    }
    int namedIndex(const char* name) const {
        for(std::size_t i=0;i<parts.size();++i)if(std::strcmp(parts[i].name,name)==0)return int(i);
        throw std::runtime_error(std::string("Missing entity model part: ")+name);
    }
    Vec transform(Vec point,int p) const {
        while(p>=0) {
            const auto& part=parts[p];
            if(part.rotateYXZ){point=rotate(point,{0,0,part.angle.z});point=rotate(point,{part.angle.x,0,0});point=rotate(point,{0,part.angle.y,0});}
            else point=rotate(point,part.angle);
            point=point+part.pivot+part.offset;
            if(part.parent<0){point=(point+part.renderOffset)*part.renderScale;point={point.x*part.stretch.x,point.y*part.stretch.y,point.z*part.stretch.z};}
            p=part.parent;
        }
        point=rotate(point,modelAngle)+modelOffset;
        return Vec{point.x*scale/16,(originY-point.y)*scale/16,-point.z*scale/16}+worldOffset;
    }
    void append(EntityMesh& result,float poseScale) const {
        std::size_t boxes=0;for(const auto& p:parts)boxes+=p.boxes.size();
        result.vertices.reserve(boxes*36);
        constexpr int faces[6][4]={{5,1,2,6},{0,4,7,3},{5,4,0,1},{2,3,7,6},{1,0,3,2},{4,5,6,7}};
        constexpr int triangles[]={0,1,2,0,2,3};
        for(std::size_t part=0;part<parts.size();++part)for(const auto& b:parts[part].boxes) {
            bool visible=true;for(int ancestor=int(part);ancestor>=0;ancestor=parts[ancestor].parent)visible=visible&&parts[ancestor].visible;
            if(!visible)continue;
            Vec lo=b.exactBounds?b.exactLo:b.pos-Vec{b.inflate,b.inflate,b.inflate};
            Vec hi=b.exactBounds?b.exactHi:b.pos+b.size+Vec{b.inflate,b.inflate,b.inflate};
            if(b.mirror)std::swap(lo.x,hi.x);
            const Vec corners[]={{lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,hi.y,lo.z},{lo.x,hi.y,lo.z},
                                 {lo.x,lo.y,hi.z},{hi.x,lo.y,hi.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z}};
            // Each box corner belongs to three faces. Transform it once using
            // the same ordered operations, then reuse the identical float result.
            std::array<Vec,8> transformed;
            for(std::size_t i=0;i<transformed.size();++i)transformed[i]=transform(corners[i],static_cast<int>(part))*poseScale;
            const float x=b.size.x,y=b.size.y,z=b.size.z,u=b.u,v=b.v;
            // ModelBox + TexturedQuad, including the reversed bottom-face V interval.
            const float rectangles[6][4]={{u+z+x,v+z,u+z+x+z,v+z+y},{u,v+z,u+z,v+z+y},
                {u+z,v,u+z+x,v+z},{u+z+x,v+z,u+z+x+x,v},
                {u+z,v+z,u+z+x,v+z+y},{u+z+x+z,v+z,u+z+x+z+x,v+z+y}};
            for(int face=0;face<6;++face) {
                std::array<EntityVertex,4> vertices{};
                const auto& uv=rectangles[face];
                const float pairs[4][2]={{uv[2],uv[1]},{uv[0],uv[1]},{uv[0],uv[3]},{uv[2],uv[3]}};
                for(int i=0;i<4;++i) {
                    const Vec p=transformed[faces[face][i]];
                    vertices[i]={p.x,p.y,p.z,pairs[i][0]/textureWidth,pairs[i][1]/textureHeight,0,0,0};
                }
                if(b.mirror)std::reverse(vertices.begin(),vertices.end());
                auto pos=[&](int i){return Vec{vertices[i].x,vertices[i].y,vertices[i].z};};
                Vec n=normal(pos(0),pos(1),pos(2));
                if(n.x==0&&n.y==0&&n.z==0)continue;
                for(auto& vertex:vertices){vertex.nx=n.x;vertex.ny=n.y;vertex.nz=n.z;}
                for(int i:triangles)result.vertices.push_back(vertices[i]);
            }
        }
    }
};

#include "entity_model_data.inc"

const std::array<float,65536>& animationSines() {
    static const auto values=[] {std::array<float,65536> result{};for(std::size_t i=0;i<result.size();++i){
        result[i]=static_cast<float>(std::sin(double(i)*3.14159265358979323846*2.0/65536.0));}return result;}();
    return values;
}
int animationIndex(float value) {
    if(value>=2147483648.f)return 2147483647;
    if(value<=-2147483648.f)return (-2147483647-1);
    return int(value);
}
float animSin(float value){return animationSines()[unsigned(animationIndex(value*10430.378f))&65535];}
float animCos(float value){return animationSines()[unsigned(animationIndex(value*10430.378f+16384.f))&65535];}
#include "entity_horse_animation.inc"

float walk(const EntityPose& p,bool opposite=false,float amount=1.4f) {
    return animCos(p.limbSwing*.6662f+(opposite?pi:0))*amount*p.limbSwingAmount;
}
void childBody(Part& part) { part.renderScale=.5f;part.renderOffset={0,24,0}; }

Geometry wolf(const EntityPose& p) {
    Geometry g=sourceModelWolf();
    auto& head=g.named("wolfHeadMain");auto& body=g.named("wolfBody");auto& mane=g.named("wolfMane");auto& tail=g.named("wolfTail");
    head.angle={p.headPitch,p.headYaw,p.interestedAngle+p.headShake};head.rotateYXZ=true;tail.rotateYXZ=true;
    tail.angle.y=p.flags&PoseAngry?0:animCos(p.limbSwing*.6662f)*1.4f*p.limbSwingAmount;
    tail.angle.x=p.flags&PoseAngry?1.5393804f:(p.flags&PoseTamed?(.55f-(20-p.health)*.02f)*pi:pi/5);
    if(p.flags&PoseSitting) {
        mane.pivot={-1,16,-3};mane.angle.x=pi*2/5;body.pivot={0,18,0};body.angle.x=pi/4;tail.pivot={-1,21,6};
        g.named("wolfLeg1").pivot={-2.5f,22,2};g.named("wolfLeg1").angle.x=pi*3/2;
        g.named("wolfLeg2").pivot={.5f,22,2};g.named("wolfLeg2").angle.x=pi*3/2;
        g.named("wolfLeg3").pivot={-2.49f,17,-4};g.named("wolfLeg3").angle.x=5.811947f;
        g.named("wolfLeg4").pivot={.51f,17,-4};g.named("wolfLeg4").angle.x=5.811947f;
    } else {
        body.angle.x=pi/2;mane.pivot={-1,14,-3};mane.angle.x=pi/2;
        g.named("wolfLeg1").angle.x=g.named("wolfLeg4").angle.x=animCos(p.limbSwing*.6662f)*1.4f*p.limbSwingAmount;
        g.named("wolfLeg2").angle.x=g.named("wolfLeg3").angle.x=animCos(p.limbSwing*.6662f+pi)*1.4f*p.limbSwingAmount;
    }
    mane.angle.z=p.maneShake;body.angle.z=p.bodyShake;tail.angle.z=p.tailShake;
    if(p.child){for(auto& part:g.parts)childBody(part);head.renderScale=1;head.renderOffset={0,5,2};}
    return g;
}

Geometry ocelot(const EntityPose& p) {
    Geometry g=sourceModelOcelot();auto& head=g.named("ocelotHead");auto& body=g.named("ocelotBody");
    auto& tail=g.named("ocelotTail");auto& tip=g.named("ocelotTail2");
    auto& bl=g.named("ocelotBackLeftLeg");auto& br=g.named("ocelotBackRightLeg");
    auto& fl=g.named("ocelotFrontLeftLeg");auto& fr=g.named("ocelotFrontRightLeg");
    head.angle={p.headPitch,p.headYaw,0};int state=1;
    if(p.sneaking){body.pivot.y+=1;head.pivot.y+=2;tail.pivot.y+=1;tip.pivot.y-=4;tip.pivot.z+=2;tail.angle.x=tip.angle.x=pi/2;state=0;}
    else if(p.flags&PoseSprinting){tip.pivot.y=tail.pivot.y;tip.pivot.z+=2;tail.angle.x=tip.angle.x=pi/2;state=2;}
    else if(p.flags&PoseSitting) {
        body.angle.x=pi/4;body.pivot.y-=4;body.pivot.z+=5;head.pivot.y-=3.3f;head.pivot.z+=1;
        tail.pivot.y+=8;tail.pivot.z-=2;tip.pivot.y+=2;tip.pivot.z-=.8f;tail.angle.x=1.7278761f;tip.angle.x=2.670354f;
        fl.angle.x=fr.angle.x=-.15707964f;fl.pivot.y=fr.pivot.y=15.8f;fl.pivot.z=fr.pivot.z=-7;
        bl.angle.x=br.angle.x=-pi/2;bl.pivot.y=br.pivot.y=21;bl.pivot.z=br.pivot.z=1;state=3;
    }
    if(state!=3) {
        body.angle.x=pi/2;bl.angle.x=animCos(p.limbSwing*.6662f)*p.limbSwingAmount;
        if(state==2){br.angle.x=animCos(p.limbSwing*.6662f+.3f)*p.limbSwingAmount;fl.angle.x=animCos(p.limbSwing*.6662f+pi+.3f)*p.limbSwingAmount;fr.angle.x=animCos(p.limbSwing*.6662f+pi)*p.limbSwingAmount;}
        else{br.angle.x=fl.angle.x=animCos(p.limbSwing*.6662f+pi)*p.limbSwingAmount;fr.angle.x=bl.angle.x;}
        tip.angle.x=1.7278761f+(state==2?pi/10:state==1?pi/4:.47123894f)*animCos(p.limbSwing)*p.limbSwingAmount;
    }
    if(p.child){for(auto& part:g.parts)childBody(part);head.renderScale=.75f;head.renderOffset={0,10,4};}
    return g;
}

Geometry rabbit(const EntityPose& p) {
    Geometry g=sourceModelRabbit();
    for(const char* name:{"rabbitHead","rabbitNose","rabbitLeftEar","rabbitRightEar"})g.named(name).angle.x=p.headPitch;
    g.named("rabbitNose").angle.y=g.named("rabbitHead").angle.y=p.headYaw;
    g.named("rabbitRightEar").angle.y=p.headYaw-.2617994f;g.named("rabbitLeftEar").angle.y=p.headYaw+.2617994f;
    const float jump=animSin(p.jumpProgress*pi);
    g.named("rabbitLeftThigh").angle.x=g.named("rabbitRightThigh").angle.x=(jump*50-21)*.017453292f;
    g.named("rabbitLeftFoot").angle.x=g.named("rabbitRightFoot").angle.x=jump*50*.017453292f;
    g.named("rabbitLeftArm").angle.x=g.named("rabbitRightArm").angle.x=(jump*-40-11)*.017453292f;
    if(p.child){for(auto& part:g.parts)childBody(part);for(const char* name:{"rabbitHead","rabbitNose","rabbitLeftEar","rabbitRightEar"}){g.named(name).renderScale=1;g.named(name).renderOffset={0,5,2};}}
    return g;
}

Geometry witch(const EntityPose& p) {
    Geometry g=sourceModelWitch();g.scale=.9375f;
    g.named("villagerHead").angle={p.headPitch,p.headYaw,0};
    g.named("villagerArms").pivot={0,3,-1};g.named("villagerArms").angle.x=-.75f;
    g.named("rightVillagerLeg").angle.x=animCos(p.limbSwing*.6662f)*1.4f*p.limbSwingAmount*.5f;
    g.named("leftVillagerLeg").angle.x=animCos(p.limbSwing*.6662f+pi)*1.4f*p.limbSwingAmount*.5f;
    const float frequency=.01f*float(p.entityId%10),age=std::floor(p.ageTicks);
    auto& nose=g.named("villagerNose");nose.angle={animSin(age*frequency)*4.5f*pi/180,0,animCos(age*frequency)*2.5f*pi/180};
    if(p.flags&PoseDrinking){nose.angle.x=-.9f;nose.offset={0,3,-1.5f};}
    return g;
}

Geometry bat(const EntityPose& p) {
    Geometry g=sourceModelBat();g.scale=.35f;
    auto& head=g.named("batHead");auto& body=g.named("batBody");auto& right=g.named("batRightWing");auto& left=g.named("batLeftWing");
    if(p.flags&PoseHanging) {
        head.angle={p.headPitch,pi-p.headYaw,pi};head.pivot={0,-2,0};right.pivot={-3,0,3};left.pivot={3,0,3};body.angle.x=pi;
        right.angle={-.15707964f,-pi*2/5,0};left.angle={right.angle.x,-right.angle.y,0};g.named("batOuterRightWing").angle.y=-1.7278761f;g.named("batOuterLeftWing").angle.y=1.7278761f;
        g.worldOffset.y=-.1f;
    } else {
        head.angle={p.headPitch,p.headYaw,0};body.angle.x=pi/4+animCos(p.ageTicks*.1f)*.15f;
        right.angle.y=animCos(p.ageTicks*1.3f)*pi*.25f;left.angle.y=-right.angle.y;
        g.named("batOuterRightWing").angle.y=right.angle.y*.5f;g.named("batOuterLeftWing").angle.y=-right.angle.y*.5f;
        g.worldOffset.y=animCos(p.ageTicks*.3f)*.1f;
    }
    return g;
}

Geometry wither(const EntityPose& p) {
    Geometry g=sourceModelWither();g.scale=2;
    if(p.invulnerabilityTicks>0)g.scale-=(float(p.invulnerabilityTicks)-p.partialTick)/220*.5f;
    const float f=animCos(p.ageTicks*.1f);
    auto& body=g.named("field_82905_a[1]");auto& tail=g.named("field_82905_a[2]");
    body.angle.x=(.065f+.05f*f)*pi;tail.pivot={-2,6.9f+animCos(body.angle.x)*10,-.5f+animSin(body.angle.x)*10};tail.angle.x=(.265f+.1f*f)*pi;
    g.named("field_82904_b[0]").angle={p.headPitch,p.headYaw,0};
    g.named("field_82904_b[1]").angle={p.sideHeadPitch[0],p.sideHeadYaw[0],0};g.named("field_82904_b[2]").angle={p.sideHeadPitch[1],p.sideHeadYaw[1],0};
    return g;
}

Geometry guardian(const EntityPose& p) {
    Geometry g=sourceModelGuardian();if(p.flags&PoseElder)g.scale=2.35f;
    g.named("guardianBody").angle={p.headPitch,p.headYaw,0};
    constexpr float ax[]={1.75f,.25f,0,0,.5f,.5f,.5f,.5f,1.25f,.75f,0,0};
    constexpr float ay[]={0,0,0,0,.25f,1.75f,1.25f,.75f,0,0,0,0};
    constexpr float az[]={0,0,.25f,1.75f,0,0,0,0,0,0,.75f,1.25f};
    constexpr float x[]={0,0,8,-8,-8,8,8,-8,0,0,8,-8},y[]={-8,-8,-8,-8,0,0,0,0,8,8,8,8},z[]={8,-8,0,0,-8,-8,8,8,8,-8,0,0};
    const float retract=(1-p.spikeExtension)*.55f;
    for(int i=0;i<12;++i){auto& spine=g.parts[1+i];const float distance=1+animCos(p.ageTicks*1.5f+i)*.01f-retract;spine.angle={pi*ax[i],pi*ay[i],pi*az[i]};spine.pivot={x[i]*distance,16+y[i]*distance,z[i]*distance};}
    g.named("guardianEye").pivot={p.eyeOffsetX,p.eyeOffsetY,-8.25f};
    const float phase=p.tailPhase>=0?p.tailPhase:p.ageTicks*.2f,sine=animSin(phase);
    g.named("guardianTail[0]").angle.y=sine*pi*.05f;
    g.named("guardianTail[1]").angle.y=sine*pi*.1f;g.named("guardianTail[1]").pivot={-1.5f,.5f,14};
    g.named("guardianTail[2]").angle.y=sine*pi*.15f;g.named("guardianTail[2]").pivot={.5f,.5f,6};
    return g;
}

Geometry armorStand(const EntityPose& p) {
    Geometry g=sourceModelArmorStand();g.originY=24.125f;g.named("bipedHeadwear").visible=false;
    const auto angle=[](const std::array<float,3>& r){return Vec{r[0]*.017453292f,r[1]*.017453292f,r[2]*.017453292f};};
    g.named("bipedHead").angle=angle(p.standHead);g.named("bipedHead").pivot={0,1,0};g.named("bipedBody").angle=angle(p.standBody);
    g.named("bipedLeftArm").angle=angle(p.standLeftArm);g.named("bipedRightArm").angle=angle(p.standRightArm);
    g.named("bipedLeftLeg").angle=angle(p.standLeftLeg);g.named("bipedRightLeg").angle=angle(p.standRightLeg);
    g.named("bipedLeftLeg").pivot={1.9f,11,0};g.named("bipedRightLeg").pivot={-1.9f,11,0};
    g.named("bipedLeftArm").visible=g.named("bipedRightArm").visible=(p.flags&PoseShowArms)!=0;
    for(const char* name:{"standRightSide","standLeftSide","standWaist"})g.named(name).angle=angle(p.standBody);
    g.named("standBase").angle={0,-p.bodyYaw,0};g.named("standBase").visible=!(p.flags&PoseNoBasePlate);
    if(p.child){for(auto& part:g.parts)childBody(part);g.named("bipedHead").renderScale=.75f;g.named("bipedHead").renderOffset={0,16,0};}
    else if(p.sneaking)for(auto& part:g.parts)part.renderOffset.y=3.2f;
    return g;
}

Geometry horse(const EntityPose& p) {
    Geometry g=sourceModelHorse();animateHorse(g,p);
    const bool mule=p.horseType==1||p.horseType==2,saddle=!p.child&&(p.flags&PoseSaddled),chest=!p.child&&(p.flags&PoseChested);
    g.named("muleLeftEar").visible=g.named("muleRightEar").visible=mule;
    g.named("horseLeftEar").visible=g.named("horseRightEar").visible=!mule;
    g.named("muleLeftChest").visible=g.named("muleRightChest").visible=chest;
    for(const char* name:{"horseFaceRopes","horseSaddleBottom","horseSaddleFront","horseSaddleBack","horseLeftSaddleRope","horseLeftSaddleMetal","horseRightSaddleRope","horseRightSaddleMetal","horseLeftFaceMetal","horseRightFaceMetal"})g.named(name).visible=saddle;
    g.named("horseLeftRein").visible=g.named("horseRightRein").visible=saddle&&(p.flags&PoseHasRider);
    if(p.horseType==1)g.scale=.87f;else if(p.horseType==2)g.scale=.92f;
    if(p.child) {
        const float size=std::clamp(p.horseSize,.1f,1.f);
        for(const char* name:{"backLeftLeg","backLeftShin","backLeftHoof","backRightLeg","backRightShin","backRightHoof","frontLeftLeg","frontLeftShin","frontLeftHoof","frontRightLeg","frontRightShin","frontRightHoof"}) {
            auto& part=g.named(name);part.stretch={size,.5f+size*.5f,size};part.renderOffset.y=.95f*(1-size)*16;
        }
        for(const char* name:{"body","tailBase","tailMiddle","tailTip","neck","mane"}){auto& part=g.named(name);part.renderScale=size;part.renderOffset.y=1.35f*(1-size)*16;}
        for(const char* name:{"head","horseLeftEar","horseRightEar","muleLeftEar","muleRightEar"}) {
            auto& part=g.named(name);part.renderScale=.5f+size*size*.5f;
            part.renderOffset={0,(.9f*(1-size)*p.grazing+1.35f*(1-size)*(1-p.grazing))*16,.15f*(1-size)*p.grazing*16};
        }
    }
    return g;
}

float wrapRotation(double value){while(value>=180)value-=360;while(value<-180)value+=360;return float(value);}
int cloneModelPart(Geometry& g,int index,bool reflected,int parent=-1) {
    const std::size_t originalCount=g.parts.size();
    Part copy=g.parts[index];copy.parent=parent;
    if(reflected){copy.pivot.x=-copy.pivot.x;copy.offset.x=-copy.offset.x;copy.angle.y=-copy.angle.y;copy.angle.z=-copy.angle.z;
        for(auto& box:copy.boxes){box.pos.x=-box.pos.x-box.size.x;const float x0=box.exactLo.x;box.exactLo.x=-box.exactHi.x;box.exactHi.x=-x0;box.mirror=!box.mirror;}}
    const int result=int(g.parts.size());g.parts.push_back(std::move(copy));
    std::vector<int> children;for(std::size_t i=0;i<originalCount;++i)if(g.parts[i].parent==index)children.push_back(int(i));
    for(int child:children)cloneModelPart(g,child,reflected,result);
    return result;
}
Geometry dragon(const EntityPose& p) {
    Geometry g=sourceModelDragon();g.parts.reserve(64);
    const float phase=p.animationPhase>=0?p.animationPhase:p.ageTicks*.02f;
    const float angle=phase*pi*2;
    g.named("jaw").angle.x=float(std::sin(double(angle))+1)*.2f;
    float bob=float(std::sin(double(angle-1))+1);bob=(bob*bob+2*bob)*.05f;
    g.modelOffset={0,(bob-2)*16,-48};g.modelAngle.x=bob*2*(pi/180);
    const auto offsets=[&](int i)->const std::array<double,2>&{return p.dragonOffsets[std::clamp(i,0,23)];};
    const float bend=wrapRotation(offsets(5)[0]-offsets(10)[0]),heading=wrapRotation(offsets(5)[0]+double(bend/2));
    const int spine=g.namedIndex("spine");Part section=g.parts[spine];g.parts[spine].visible=false;
    float y=20,z=-12,x=0;
    for(int i=0;i<5;++i){auto next=section;next.pivot={x,y,z};next.angle={float(std::cos(double(float(i)*.45f+angle)))*.15f+float(offsets(5-i)[1]-offsets(6)[1])*pi/180*1.5f*5,
        wrapRotation(offsets(5-i)[0]-offsets(6)[0])*pi/180*1.5f,-wrapRotation(offsets(5-i)[0]-double(heading))*pi/180*1.5f};
        y=float(double(y)+std::sin(double(next.angle.x))*10);z=float(double(z)-std::cos(double(next.angle.y))*std::cos(double(next.angle.x))*10);x=float(double(x)-std::sin(double(next.angle.y))*std::cos(double(next.angle.x))*10);g.parts.push_back(std::move(next));}
    auto& head=g.named("head");head.pivot={x,y,z};head.angle.y=wrapRotation(offsets(0)[0]-offsets(6)[0])*pi/180;head.angle.z=-wrapRotation(offsets(0)[0]-double(heading))*pi/180;
    g.named("wing").angle={.125f-float(std::cos(double(angle)))*.2f,.25f,float(std::sin(double(angle))+.125)*.8f};
    g.named("wingTip").angle.z=-float(std::sin(double(angle+2))+.5)*.75f;
    g.named("rearLeg").angle.x=1+bob*.1f;g.named("rearLegTip").angle.x=.5f+bob*.1f;g.named("rearFoot").angle.x=.75f+bob*.1f;
    g.named("frontLeg").angle.x=1.3f+bob*.1f;g.named("frontLegTip").angle.x=-.5f-bob*.1f;g.named("frontFoot").angle.x=.75f+bob*.1f;
    const int wing=g.namedIndex("wing"),front=g.namedIndex("frontLeg"),rear=g.namedIndex("rearLeg");
    const int wing2=cloneModelPart(g,wing,true),front2=cloneModelPart(g,front,true),rear2=cloneModelPart(g,rear,true);
    const int bank=g.part({0,16,0},{0,0,-bend*1.5f*pi/180});
    for(int index:{g.namedIndex("body"),wing,front,rear,wing2,front2,rear2}){g.parts[index].pivot.y-=16;g.parts[index].parent=bank;}
    float wave=0;y=10;z=60;x=0;
    for(int i=0;i<12;++i){auto next=section;wave=float(double(wave)+std::sin(double(float(i)*.45f+angle))*.05000000074505806);
        next.angle={wave+float(offsets(12+i)[1]-offsets(11)[1])*pi/180*1.5f*5,(wrapRotation(offsets(12+i)[0]-offsets(11)[0])*1.5f+180)*pi/180,wrapRotation(offsets(12+i)[0]-double(heading))*pi/180*1.5f};
        next.pivot={x,y,z};y=float(double(y)+std::sin(double(next.angle.x))*10);z=float(double(z)-std::cos(double(next.angle.y))*std::cos(double(next.angle.x))*10);x=float(double(x)-std::sin(double(next.angle.y))*std::cos(double(next.angle.x))*10);g.parts.push_back(std::move(next));}
    return g;
}

Geometry biped(const EntityPose& pose,int family) {
    // 0 Steve, 1 Alex, 2 zombie, 3 skeleton, 4 pig zombie, 5 enderman.
    Geometry g;bool player=family<=1,slim=family==1,skeleton=family==3,ender=family==5;
    g.originY=24.125f;
    if(player){g.scale=.9375f;if(pose.sneaking&&!pose.localPlayer)g.worldOffset.y=-.125f;}
    g.textureHeight=player||family==2||family==4?64:32;
    int head=g.part({0,0,0},{pose.headPitch,pose.headYaw,0});
    int body=g.part();
    int rightArm=g.part({-5,slim?2.5f:2.f,0});
    int leftArm=g.part({5,slim?2.5f:2.f,0});
    int rightLeg=g.part({skeleton?-2.f:-1.9f,12,.1f});
    int leftLeg=g.part({skeleton?2.f:1.9f,12,.1f});
    const char* joints[]={"bipedHead","bipedBody","bipedRightArm","bipedLeftArm","bipedRightLeg","bipedLeftLeg"};for(int i=0;i<6;++i)g.parts[i].name=joints[i];
    g.box(head,0,0,{-4,-8,-4},{8,8,8});
    g.box(body,16,16,{-4,0,-2},{8,12,4});
    if(skeleton) {
        g.box(rightArm,40,16,{-1,-2,-1},{2,12,2});g.box(leftArm,40,16,{-1,-2,-1},{2,12,2},0,true);
        g.box(rightLeg,0,16,{-1,0,-1},{2,12,2});g.box(leftLeg,0,16,{-1,0,-1},{2,12,2},0,true);
    } else {
        g.box(rightArm,40,16,{slim?-2.f:-3.f,-2,-2},{slim?3.f:4.f,12,4});
        g.box(leftArm,player?32.f:40.f,player?48.f:16.f,{-1,-2,-2},{slim?3.f:4.f,12,4},0,!player);
        g.box(rightLeg,0,16,{-2,0,-2},{4,12,4});g.box(leftLeg,player?16.f:0.f,player?48.f:16.f,{-2,0,-2},{4,12,4},0,!player);
    }
    auto& h=g.parts[head];auto& b=g.parts[body];auto& ra=g.parts[rightArm];auto& la=g.parts[leftArm];
    auto& rl=g.parts[rightLeg];auto& ll=g.parts[leftLeg];
    ra.angle.x=animCos(pose.limbSwing*.6662f+pi)*2.f*pose.limbSwingAmount*.5f;la.angle.x=animCos(pose.limbSwing*.6662f)*2.f*pose.limbSwingAmount*.5f;rl.angle.x=walk(pose);ll.angle.x=walk(pose,true);
    if(pose.riding){ra.angle.x-=pi/5;la.angle.x-=pi/5;rl.angle={-2*pi/5,pi/10,0};ll.angle={-2*pi/5,-pi/10,0};}
    if(pose.heldItemLeft)la.angle.x=la.angle.x*.5f-pi/10*pose.heldItemLeft;
    if(pose.heldItemRight==1||pose.heldItemRight==3)ra.angle.x=ra.angle.x*.5f-pi/10*pose.heldItemRight;
    if(pose.heldItemRight==3)ra.angle.y=-.5235988f;
    const float swing=std::clamp(pose.swingProgress,0.f,1.f);
    b.angle.y=animSin(std::sqrt(swing)*pi*2.f)*.2f;
    ra.pivot.z=animSin(b.angle.y)*5;ra.pivot.x=-animCos(b.angle.y)*5;
    la.pivot.z=-animSin(b.angle.y)*5;la.pivot.x=animCos(b.angle.y)*5;
    ra.angle.y+=b.angle.y;la.angle.y+=b.angle.y;la.angle.x+=b.angle.y;
    float f=1-swing;f=f*f;f=f*f;f=1-f;
    const float f1=animSin(f*pi),f2=animSin(swing*pi)*-(h.angle.x-.7f)*.75f;
    ra.angle.x=float(double(ra.angle.x)-(double(f1)*1.2+double(f2)));
    ra.angle.y+=b.angle.y*2;ra.angle.z+=animSin(swing*pi)*-.4f;
    if(pose.sneaking){b.angle.x=.5f;ra.angle.x+=.4f;la.angle.x+=.4f;rl.pivot.z=ll.pivot.z=4;rl.pivot.y=ll.pivot.y=9;h.pivot.y=1;}
    float idleZ=animCos(pose.ageTicks*.09f)*.05f+.05f,idleX=animSin(pose.ageTicks*.067f)*.05f;
    ra.angle.z+=idleZ;la.angle.z-=idleZ;ra.angle.x+=idleX;la.angle.x-=idleX;
    if(pose.aimedBow){ra.angle={-pi/2+h.angle.x+idleX,-.1f+h.angle.y,idleZ};la.angle={-pi/2+h.angle.x-idleX,.5f+h.angle.y,-idleZ};}
    if(family==2||family==3||family==4) {
        float a=animSin(swing*pi),c=animSin((1-(1-swing)*(1-swing))*pi),angle=-pi/2-(a*1.2f-c*.4f);
        ra.angle={angle+idleX,-(.1f-a*.6f),idleZ};
        la.angle={angle-idleX,.1f-a*.6f,-idleZ};
    }
    if(ender) {
        h.pivot={0,-13,0};b.pivot={0,-14,0};b.angle.x=0;b.boxes.clear();g.box(body,32,16,{-4,0,-2},{8,12,4});
        ra.pivot={-5,-12,0};la.pivot={5,-12,0};rl.pivot={-2,-5,0};ll.pivot={2,-5,0};
        for(int p:{rightArm,leftArm,rightLeg,leftLeg}) {
            g.parts[p].boxes.clear();g.parts[p].angle.x=std::clamp(g.parts[p].angle.x*.5f,-.4f,.4f);
            g.box(p,56,0,{-1,p==rightArm||p==leftArm?-2.f:0.f,-1},{2,30,2},0,p==leftArm||p==leftLeg);
        }
        if(pose.variant&1){ra.angle.x=la.angle.x=-.5f;ra.angle.z=.05f;la.angle.z=-.05f;}
        if(pose.variant&2)h.pivot.y-=5;
        // Actual ModelEnderman legs end at Java Y=25; use a foot-based origin.
        g.originY=25;
    }
    // All Part references above are consumed before vector growth below.
    const Part headPose=g.parts[head];
    int hat=g.part(headPose.pivot,headPose.angle);
    g.parts[hat].name="bipedHeadwear";
    g.box(hat,ender?0.f:32.f,ender?16.f:0.f,{-4,-8,-4},{8,8,8},ender?-.5f:.5f);
    if(player) {
        const int source[]={body,rightArm,leftArm,rightLeg,leftLeg};
        const float offsets[][2]={{16,32},{40,32},{48,48},{0,32},{0,48}};
        for(int i=0;i<5;++i) {
            Part copy=g.parts[source[i]];const auto original=copy.boxes.front();copy.boxes.clear();
            int p=static_cast<int>(g.parts.size());g.parts.push_back(std::move(copy));
            g.box(p,offsets[i][0],offsets[i][1],original.pos,original.size,.25f);
        }
        g.parts[hat].visible=(pose.skinParts&64)!=0;
        const unsigned bits[]={2,8,4,32,16};for(int i=0;i<5;++i)g.parts[7+i].visible=(pose.skinParts&bits[i])!=0;
        if(pose.spectator){for(auto& part:g.parts)part.visible=false;g.parts[head].visible=g.parts[hat].visible=true;}
    }
    if(pose.child) {
        for(auto& part:g.parts)childBody(part);
        g.parts[head].renderScale=.75f;g.parts[head].renderOffset={0,16,0};
    } else if(pose.sneaking) {
        for(auto& part:g.parts)part.renderOffset.y=3.2f;
    }
    if(skeleton&&pose.variant==1)g.scale=1.2f;
    return g;
}

Geometry creeper(const EntityPose& p) {
    Geometry g;int h=g.part({0,6,0},{p.headPitch,p.headYaw,0});g.box(h,0,0,{-4,-8,-4},{8,8,8});
    g.box(g.part({0,6,0}),16,16,{-4,0,-2},{8,12,4});
    for(int i=0;i<4;++i)g.box(g.part({i%2?2.f:-2.f,18,i<2?4.f:-4.f},{walk(p,i==1||i==2),0,0}),0,16,{-2,0,-2},{4,6,4});
    return g;
}

Geometry quadruped(const EntityPose& p,int family) {
    // ModelPig=0, ModelCow=1, ModelSheep2=2. Cow also supplies mooshroom base.
    Geometry g;float legLength=family==0?6.f:12.f;
    int head=g.part({0,18-legLength,-6},{p.headPitch,p.headYaw,0});
    int body=g.part({0,17-legLength,2},{pi/2,0,0});
    if(family==0) {
        g.box(head,0,0,{-4,-4,-8},{8,8,8});g.box(head,16,16,{-2,0,-9},{4,3,1});
        g.box(body,28,8,{-5,-10,-7},{10,16,8});
    } else if(family==1) {
        g.parts[head].pivot={0,4,-8};g.parts[body].pivot={0,5,2};
        g.box(head,0,0,{-4,-4,-6},{8,8,6});g.box(head,22,0,{-5,-5,-4},{1,3,1});g.box(head,22,0,{4,-5,-4},{1,3,1});
        g.box(body,18,4,{-6,-10,-7},{12,18,10});g.box(body,52,0,{-2,2,-8},{4,6,1});
    } else {
        g.parts[head].pivot={0,6,-8};g.parts[body].pivot={0,5,2};
        g.box(head,0,0,{-3,-4,-6},{6,6,8});g.box(body,28,8,{-4,-10,-7},{8,16,6});
    }
    for(int i=0;i<4;++i) {
        float x=(i%2?1.f:-1.f)*(family==1?4.f:3.f),z=i<2?7.f:(family==1?-6.f:-5.f);
        int part=g.part({x,24-legLength,z},{walk(p,i==1||i==2),0,0});g.box(part,0,16,{-2,0,-2},{4,legLength,4});
    }
    if(p.child){for(std::size_t i=1;i<g.parts.size();++i)childBody(g.parts[i]);g.parts[head].renderOffset={0,family==0?4.f:8.f,family==1?6.f:4.f};}
    return g;
}

Geometry spider(const EntityPose& p,bool cave) {
    Geometry g;if(cave)g.scale=.7f;
    g.box(g.part({0,15,-3},{p.headPitch,p.headYaw,0}),32,4,{-4,-4,-8},{8,8,8});
    g.box(g.part({0,15,0}),0,0,{-3,-3,-3},{6,6,6});g.box(g.part({0,15,9}),0,12,{-5,-4,-6},{10,8,12});
    const float y[]={pi/4,pi/8,-pi/8,-pi/4},z[]={-pi/4,-pi/4*.74f,-pi/4*.74f,-pi/4};
    const float phase[]={0,pi,pi/2,pi*1.5f};
    for(int i=0;i<8;++i) {
        int pair=i/2;float side=i%2?-1.f:1.f;
        float dy=-std::cos(p.limbSwing*.6662f*2+phase[pair])*.4f*p.limbSwingAmount;
        float dz=std::abs(std::sin(p.limbSwing*.6662f+phase[pair]))*.4f*p.limbSwingAmount;
        g.box(g.part({i%2?4.f:-4.f,15,2.f-pair},{0,(y[pair]+dy)*side,(z[pair]+dz)*side}),18,0,{i%2?-1.f:-15.f,-1,-1},{16,2,2});
    }
    return g;
}

Geometry chicken(const EntityPose& p) {
    Geometry g;
    int h=g.part({0,15,-4},{p.headPitch,p.headYaw,0});g.box(h,0,0,{-2,-6,-2},{4,6,3});
    g.box(h,14,0,{-2,-4,-4},{4,2,2});g.box(h,14,4,{-1,-2,-3},{2,2,2});
    g.box(g.part({0,16,0},{pi/2,0,0}),0,9,{-3,-4,-3},{6,8,6});
    g.box(g.part({-2,19,1},{walk(p),0,0}),26,0,{-1,0,-3},{3,5,3});
    g.box(g.part({1,19,1},{walk(p,true),0,0}),26,0,{-1,0,-3},{3,5,3});
    // Java passes an entity flap interpolant as ageInTicks; without that metadata,
    // use a documented periodic flap only while locomotion is active.
    float flap=(std::sin(p.ageTicks*1.5f)+1)*p.limbSwingAmount;
    g.box(g.part({-4,13,0},{0,0,flap}),24,13,{0,0,-3},{1,4,6});
    g.box(g.part({4,13,0},{0,0,-flap}),24,13,{-1,0,-3},{1,4,6});
    if(p.child){for(std::size_t i=1;i<g.parts.size();++i)childBody(g.parts[i]);g.parts[h].renderOffset={0,5,2};}
    return g;
}

Geometry villager(const EntityPose& p) {
    Geometry g;g.textureHeight=64;
    int h=g.part({}, {p.headPitch,p.headYaw,0});g.box(h,0,0,{-4,-10,-4},{8,10,8});
    g.box(g.part({0,-2,0},{},h),24,0,{-1,-1,-6},{2,4,2});
    int body=g.part();g.box(body,16,20,{-4,0,-3},{8,12,6});g.box(body,0,38,{-4,0,-3},{8,18,6},.5f);
    int arms=g.part({0,3,-1},{-.75f,0,0});
    g.box(arms,44,22,{-8,-2,-2},{4,8,4});g.box(arms,44,22,{4,-2,-2},{4,8,4});g.box(arms,40,38,{-4,2,-2},{8,4,4});
    g.box(g.part({-2,12,0},{walk(p,false,.7f),0,0}),0,22,{-2,0,-2},{4,12,4});
    g.box(g.part({2,12,0},{walk(p,true,.7f),0,0}),0,22,{-2,0,-2},{4,12,4},0,true);
    if(p.child){g.scale=.46875f;}else g.scale=.9375f;
    return g;
}

Geometry slime(const EntityPose& p,bool magma) {
    Geometry g;if(p.variant>0)g.scale=static_cast<float>(std::clamp(p.variant,1,127));
    int body=g.part();
    if(magma) {
        for(int i=0;i<8;++i) {
            int u=0,v=i;
            if(i==2){u=24;v=10;}else if(i==3){u=24;v=19;}
            g.box(body,static_cast<float>(u),static_cast<float>(v),{-4,16.f+i,-4},{8,1,8});
        }
        g.box(body,0,16,{-2,18,-2},{4,4,4});
    } else {
        g.box(body,0,16,{-3,17,-3},{6,6,6});
        g.box(body,32,0,{-3.25f,18,-3.5f},{2,2,2});g.box(body,32,4,{1.25f,18,-3.5f},{2,2,2});
        g.box(body,32,8,{0,21,-3.5f},{1,1,1});g.box(body,0,0,{-4,16,-4},{8,8,8});
    }
    return g;
}

class JavaRandom {
    std::uint64_t seed_;
public:
    explicit JavaRandom(std::uint64_t seed):seed_((seed^0x5deece66dull)&((1ull<<48)-1)){}
    int next(int bits){seed_=(seed_*0x5deece66dull+11)&((1ull<<48)-1);return static_cast<int>(seed_>>(48-bits));}
    int nextInt(int bound){int bits,value;do{bits=next(31);value=bits%bound;}while(std::uint32_t(bits)-std::uint32_t(value)+std::uint32_t(bound-1)>0x7fffffffu);return value;}
};
Geometry ghast(const EntityPose& p) {
    Geometry g;g.scale=4.5f;g.originY=31;
    g.box(g.part({0,8,0}),0,0,{-8,-8,-8},{16,16,16});JavaRandom random(1660);
    for(int i=0;i<9;++i) {
        float x=(float(i%3)-float(i/3%2)*.5f+.25f-1)*5,z=(float(i/3)-1)*5;
        g.box(g.part({x,15,z},{.2f*std::sin(p.ageTicks*.3f+i)+.4f,0,0}),0,0,{-1,0,-1},{2,float(random.nextInt(7)+8),2});
    }
    return g;
}
Geometry squid(const EntityPose& p) {
    Geometry g;g.originY=33;
    g.box(g.part({0,8,0}),0,0,{-6,-8,-6},{12,16,12});
    const float tentacle=(std::sin(p.ageTicks*.15f)+1)*.5f;
    for(int i=0;i<8;++i) {
        float angle=i*2*pi/8;
        g.box(g.part({std::cos(angle)*5,15,std::sin(angle)*5},{tentacle,-angle+pi/2,0}),48,0,{-1,0,-1},{2,18,2});
    }
    return g;
}

Geometry silverfish(const EntityPose& p,bool endermite) {
    Geometry g;
    constexpr int size[7][3]={{3,2,2},{4,3,2},{6,4,3},{3,3,3},{2,2,3},{2,1,2},{1,1,2}};
    constexpr int uv[7][2]={{0,0},{0,4},{0,9},{0,16},{0,22},{11,0},{13,4}};
    constexpr int miteSize[4][3]={{4,3,2},{6,4,5},{3,3,1},{1,2,1}};
    constexpr int miteUv[4][2]={{0,0},{0,5},{0,14},{0,18}};
    float z=-3.5f;
    int count=endermite?4:7;
    for(int i=0;i<count;++i) {
        const auto& s=endermite?miteSize[i]:size[i];const auto& tex=endermite?miteUv[i]:uv[i];
        float a=p.ageTicks*.9f+i*.15f*pi;
        int part=g.part({std::sin(a)*pi*(endermite?.1f:.2f)*std::abs(i-2),24.f-s[1],z},
            {0,std::cos(a)*pi*(endermite?.01f:.05f)*(1+std::abs(i-2)),0});
        g.box(part,float(tex[0]),float(tex[1]),{-s[0]*.5f,0,-s[2]*.5f},{float(s[0]),float(s[1]),float(s[2])});
        if(i<count-1)z+=(s[2]+(endermite?miteSize[i+1][2]:size[i+1][2]))*.5f;
    }
    if(!endermite) {
        const int target[]={2,4,1};const float texV[]={0,11,18},height[]={8,4,5},width[]={10,6,6};
        for(int i=0;i<3;++i) {
            Part original=g.parts[target[i]];int part=g.part({i?original.pivot.x:0,24-height[i],original.pivot.z},{0,original.angle.y,0});
            g.box(part,20,texV[i],{-width[i]*.5f,0,-1.5f},{width[i],height[i],i==2?2.f:3.f});
        }
    }
    return g;
}

Geometry blaze(const EntityPose& p) {
    Geometry g;g.box(g.part({}, {p.headPitch,p.headYaw,0}),0,0,{-4,-4,-4},{8,8,8});
    float angle=p.ageTicks*pi*-.1f;
    for(int i=0;i<12;++i) {
        if(i==4)angle=pi/4+p.ageTicks*pi*.03f;
        if(i==8)angle=.47123894f+p.ageTicks*pi*-.05f;
        float radius=i<4?9.f:i<8?7.f:5.f;
        float y=i<4?-2+std::cos((i*2+p.ageTicks)*.25f):i<8?2+std::cos((i*2+p.ageTicks)*.25f):11+std::cos((i*1.5f+p.ageTicks)*.5f);
        g.box(g.part({std::cos(angle)*radius,y,std::sin(angle)*radius}),0,16,{0,0,0},{2,8,2});angle+=1;
    }
    return g;
}

float triangleWave(float x,float period){return (std::abs(std::fmod(x,period)-period*.5f)-period*.25f)/(period*.25f);}
Geometry ironGolem(const EntityPose& p) {
    Geometry g;g.textureWidth=g.textureHeight=128;g.originY=31;
    int head=g.part({0,0,-2},{p.headPitch,p.headYaw,0});g.box(head,0,0,{-4,-12,-5.5f},{8,10,8});g.box(head,24,0,{-1,-5,-7.5f},{2,4,2});
    int body=g.part();g.box(body,0,40,{-9,-2,-6},{18,12,11});g.box(body,0,70,{-4.5f,10,-3},{9,5,6},.5f);
    float phase=triangleWave(p.limbSwing,13)*p.limbSwingAmount;
    g.box(g.part({0,-7,0},{-.2f*p.limbSwingAmount+1.5f*phase,0,0}),60,21,{-13,-2.5f,-3},{4,30,6});
    g.box(g.part({0,-7,0},{-.2f*p.limbSwingAmount-1.5f*phase,0,0}),60,58,{9,-2.5f,-3},{4,30,6});
    g.box(g.part({-4,18,0},{-1.5f*phase,0,0}),37,0,{-3.5f,-3,-3},{6,16,5});
    g.box(g.part({5,18,0},{1.5f*phase,0,0}),60,0,{-3.5f,-3,-3},{6,16,5});
    return g;
}

Geometry snowman(const EntityPose& p) {
    Geometry g;g.textureHeight=64;g.originY=23.5f;
    g.box(g.part({0,4,0},{p.headPitch,p.headYaw,0}),0,0,{-4,-8,-4},{8,8,8},-.5f);
    float turn=p.headYaw*.25f;
    g.box(g.part({0,13,0},{0,turn,0}),0,16,{-5,-10,-5},{10,10,10},-.5f);
    g.box(g.part({0,24,0}),0,36,{-6,-12,-6},{12,12,12},-.5f);
    g.box(g.part({std::cos(turn)*5,6,-std::sin(turn)*5},{0,turn,1}),32,0,{-1,0,-1},{12,2,2},-.5f);
    g.box(g.part({-std::cos(turn)*5,6,std::sin(turn)*5},{0,pi+turn,-1}),32,0,{-1,0,-1},{12,2,2},-.5f);
    return g;
}

std::uint32_t read32(std::ifstream& input) {
    unsigned char b[4];if(!input.read(reinterpret_cast<char*>(b),4))throw std::runtime_error("Truncated entity texture header");
    return std::uint32_t(b[0])|(std::uint32_t(b[1])<<8)|(std::uint32_t(b[2])<<16)|(std::uint32_t(b[3])<<24);
}
bool safePath(const std::string& path) {
    if(path.empty()||path.front()=='/'||path.find(':')!=std::string::npos||path.find('\\')!=std::string::npos)return false;
    std::size_t start=0;
    while(start<path.size()) {
        auto end=path.find('/',start);auto part=path.substr(start,end==std::string::npos?end:end-start);
        if(part.empty()||part=="."||part=="..")return false;
        if(end==std::string::npos)break;
        start=end+1;
    }
    return true;
}
} // namespace

bool EntityModels::load(const std::string& assetsDirectory,std::string& error) {
    try {
        EntityModels next;std::ifstream index(assetsDirectory+"/runtime/entities/index.txt");
        if(!index)throw std::runtime_error("Missing native entity texture index; rerun prepare_assets.py");
        std::string line;std::uint64_t totalBytes=0;
        while(std::getline(index,line)) {
            if(!line.empty()&&line.back()=='\r')line.pop_back();
            if(line.empty())continue;
            auto tab=line.find('\t');if(tab==std::string::npos||line.size()>2048)throw std::runtime_error("Invalid entity texture index entry");
            std::string name=line.substr(0,tab),path=line.substr(tab+1);
            if(!safePath(path)||next.textures_.size()>=8192)throw std::runtime_error("Invalid entity texture path/count");
            std::ifstream input(assetsDirectory+"/runtime/entities/"+path,std::ios::binary);
            char magic[4];if(!input.read(magic,4)||std::memcmp(magic,"NVTX",4))throw std::runtime_error("Invalid entity texture: "+name);
            EntityTexture texture;texture.name=name;texture.width=read32(input);texture.height=read32(input);
            if(!texture.width||!texture.height||texture.width>8192||texture.height>8192)throw std::runtime_error("Invalid entity texture dimensions");
            auto bytes=std::uint64_t(texture.width)*texture.height*4;totalBytes+=bytes;
            if(totalBytes>512u*1024u*1024u)throw std::runtime_error("Entity textures exceed 512 MiB");
            texture.pixels.resize(static_cast<std::size_t>(bytes));
            if(!input.read(reinterpret_cast<char*>(texture.pixels.data()),static_cast<std::streamsize>(bytes))||input.peek()!=std::ifstream::traits_type::eof())
                throw std::runtime_error("Invalid entity texture pixel count: "+name);
            if(!next.lookup_.emplace(name,static_cast<int>(next.textures_.size())).second)throw std::runtime_error("Duplicate entity texture: "+name);
            next.textures_.push_back(std::move(texture));
        }
        if(next.textureIndex("minecraft:entity/steve")<0)throw std::runtime_error("Default Steve texture is missing");
        std::ifstream equipment(assetsDirectory+"/runtime/equipment/index.txt");
        while(std::getline(equipment,line)) {
            if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
            auto tab=line.find('\t');if(tab==std::string::npos)throw std::runtime_error("Invalid equipment texture index");
            const auto name=line.substr(0,tab),path=line.substr(tab+1);if(!safePath(path))throw std::runtime_error("Invalid equipment texture path");
            std::ifstream input(assetsDirectory+"/runtime/equipment/"+path,std::ios::binary);char magic[4];if(!input.read(magic,4)||std::memcmp(magic,"NVTX",4))throw std::runtime_error("Missing equipment texture "+name);
            EntityTexture texture;texture.name=name;texture.width=read32(input);texture.height=read32(input);auto bytes=std::uint64_t(texture.width)*texture.height*4;
            if(!texture.width||!texture.height||texture.width>8192||texture.height>8192||(totalBytes+=bytes)>512u*1024u*1024u)throw std::runtime_error("Invalid equipment image size");texture.pixels.resize(std::size_t(bytes));
            if(!input.read(reinterpret_cast<char*>(texture.pixels.data()),std::streamsize(bytes))||input.peek()!=std::ifstream::traits_type::eof())throw std::runtime_error("Invalid equipment pixels");
            if(!next.lookup_.emplace(name,int(next.textures_.size())).second)throw std::runtime_error("Duplicate equipment texture");next.textures_.push_back(std::move(texture));
        }
        const auto composite=[&](const std::string& resource,const std::string& base,const std::vector<std::string>& overlays,const float* tint=nullptr) {
            const int baseIndex=next.textureIndex(base);if(baseIndex<0)return;
            EntityTexture result=next.textures_[baseIndex];result.name=resource;
            for(const auto& layer:overlays){const int index=next.textureIndex(layer);if(index<0)continue;const auto& overlay=next.textures_[index];
                if(overlay.width!=result.width||overlay.height!=result.height)continue;
                for(std::size_t pixel=0;pixel<result.pixels.size();pixel+=4){const unsigned a=overlay.pixels[pixel+3];
                    for(unsigned c=0;c<3;++c){unsigned value=overlay.pixels[pixel+c];if(tint)value=unsigned(float(value)*tint[c]);
                        result.pixels[pixel+c]=std::uint8_t((value*a+unsigned(result.pixels[pixel+c])*(255-a)+127)/255);}
                    result.pixels[pixel+3]=std::uint8_t(a+(unsigned(result.pixels[pixel+3])*(255-a)+127)/255);
                }
            }
            next.lookup_.emplace(resource,int(next.textures_.size()));next.textures_.push_back(std::move(result));
        };
        const char* coats[]={"horse_white","horse_creamy","horse_chestnut","horse_brown","horse_black","horse_gray","horse_darkbrown"};
        const char* markings[]={"","horse_markings_white","horse_markings_whitefield","horse_markings_whitedots","horse_markings_blackdots"};
        const char* armor[]={"","armor/horse_armor_iron","armor/horse_armor_gold","armor/horse_armor_diamond"};
        for(int coat=0;coat<7;++coat)for(int mark=0;mark<5;++mark)for(int equipment=0;equipment<4;++equipment){
            std::vector<std::string> layers;if(mark)layers.push_back(std::string("minecraft:entity/horse/")+markings[mark]);
            if(equipment)layers.push_back(std::string("minecraft:entity/horse/")+armor[equipment]);
            composite("native:horse/"+std::to_string(coat)+"/"+std::to_string(mark)+"/"+std::to_string(equipment),std::string("minecraft:entity/horse/")+coats[coat],layers);
        }
        for(int color=0;color<16;++color)composite("native:wolf/"+std::to_string(color),"minecraft:entity/wolf/wolf_tame",{"minecraft:entity/wolf/wolf_collar"},sourceDyeColors[color]);
        *this=std::move(next);error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}

int EntityModels::textureIndex(const std::string& resource) const noexcept {
    const auto found=lookup_.find(resource);return found==lookup_.end()?-1:found->second;
}

namespace {
EntityMatrix emIdentity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
EntityMatrix emMultiply(const EntityMatrix& a,const EntityMatrix& b){EntityMatrix r{};for(int c=0;c<4;++c)for(int y=0;y<4;++y)for(int k=0;k<4;++k)r[c*4+y]+=a[k*4+y]*b[c*4+k];return r;}
void emTranslate(EntityMatrix& m,float x,float y,float z){auto t=emIdentity();t[12]=x;t[13]=y;t[14]=z;m=emMultiply(m,t);}
void emScale(EntityMatrix& m,float x,float y,float z){auto t=emIdentity();t[0]=x;t[5]=y;t[10]=z;m=emMultiply(m,t);}
void emRotate(EntityMatrix& m,int axis,float degrees){double a=double(degrees)*3.14159265358979323846/180.;float c=float(std::cos(a)),s=float(std::sin(a));auto r=emIdentity();if(axis==0){r[5]=c;r[9]=-s;r[6]=s;r[10]=c;}else if(axis==1){r[0]=c;r[8]=s;r[2]=-s;r[10]=c;}else{r[0]=c;r[4]=-s;r[1]=s;r[5]=c;}m=emMultiply(m,r);}
bool standKind(int kind,int type){return (kind==1&&type==30)||(kind==2&&type==78);}
bool humanoidKind(int kind,int type){return kind==0||standKind(kind,type)||(kind==1&&(type==51||type==53||type==54||type==57));}
Geometry humanoid(int kind,int type,const EntityPose& pose){if(standKind(kind,type))return armorStand(pose);auto g=biped(pose,kind==0?(pose.variant==1?1:0):type==51?3:type==57?4:2);if(type==53&&kind==1)g.scale=6;else if(kind==1&&type==51&&pose.variant==1)g.scale=1.2f;return g;}
EntityMatrix projection(const Geometry& g,const EntityPose& p){auto m=emIdentity();emTranslate(m,g.worldOffset.x*p.scale,g.worldOffset.y*p.scale,g.worldOffset.z*p.scale);emScale(m,g.scale*p.scale,g.scale*p.scale,g.scale*p.scale);emTranslate(m,0,g.originY/16,0);emScale(m,1,-1,-1);return m;}
void postPart(EntityMatrix& m,const Part& part){emTranslate(m,part.pivot.x/16,part.pivot.y/16,part.pivot.z/16);emRotate(m,2,part.angle.z*(180.f/pi));emRotate(m,1,part.angle.y*(180.f/pi));emRotate(m,0,part.angle.x*(180.f/pi));}
}
EntityMatrix EntityModels::humanoidHand(int kind,int type,const EntityPose& pose)const {
    if(!humanoidKind(kind,type))return emIdentity();auto g=humanoid(kind,type,pose);auto m=projection(g,pose);
    if(pose.child){emTranslate(m,0,.625f,0);emRotate(m,0,20);emScale(m,.5f,.5f,.5f);}
    auto part=standKind(kind,type)?g.named("bipedRightArm"):g.parts[2];if(kind==0&&pose.variant==1)part.pivot.x+=1;
    postPart(m,part);emTranslate(m,-.0625f,.4375f,.0625f);return m;
}
EntityMatrix EntityModels::humanoidHead(int kind,int type,const EntityPose& pose)const {
    if(!humanoidKind(kind,type))return emIdentity();auto g=humanoid(kind,type,pose);auto m=projection(g,pose);
    if(pose.sneaking)emTranslate(m,0,.2f,0);if(pose.child){emScale(m,.7f,.7f,.7f);emTranslate(m,0,1,0);}
    postPart(m,standKind(kind,type)?g.named("bipedHead"):g.parts[0]);return m;
}
EntityMatrix EntityModels::playerHand(const EntityPose& pose)const{return humanoidHand(0,0,pose);}
EntityMatrix EntityModels::playerHead(const EntityPose& pose)const{return humanoidHead(0,0,pose);}
std::vector<EntityLayer> EntityModels::humanoidArmor(int kind,int type,int itemId,const EntityPose& pose,int rgb,bool enchanted,int slotOverride)const {
    std::vector<EntityLayer> layers;if(!humanoidKind(kind,type)||itemId<298||itemId>317)return layers;
    const int slot=slotOverride>=0?slotOverride:(itemId-298)%4,material=(itemId-298)/4;if(slot>3)return layers;const char* names[]={"leather","chainmail","iron","diamond","gold"};
    const std::string resource=std::string("minecraft:models/armor/")+names[material]+"_layer_"+(slot==2?"2":"1");int tex=textureIndex(resource);if(tex<0)return layers;
    auto g=biped(pose,kind==1&&!standKind(kind,type)?2:6);g.textureHeight=32;
    if(kind==0){g.scale=.9375f;if(pose.sneaking&&!pose.localPlayer)g.worldOffset.y=-.125f;}
    else if(kind==1&&type==53)g.scale=6;else if(kind==1&&type==51&&pose.variant==1)g.scale=1.2f;
    if(standKind(kind,type)){
        const auto angle=[](const std::array<float,3>& v){return Vec{v[0]*.017453292f,v[1]*.017453292f,v[2]*.017453292f};};
        g.parts[0].angle=angle(pose.standHead);g.parts[0].pivot={0,1,0};g.parts[1].angle=angle(pose.standBody);g.parts[2].angle=angle(pose.standRightArm);g.parts[3].angle=angle(pose.standLeftArm);g.parts[4].angle=angle(pose.standRightLeg);g.parts[5].angle=angle(pose.standLeftLeg);g.parts[4].pivot={-1.9f,11,0};g.parts[5].pivot={1.9f,11,0};g.parts[6].angle=g.parts[0].angle;g.parts[6].pivot=g.parts[0].pivot;
    }
    for(std::size_t i=0;i<g.parts.size();++i){g.parts[i].visible=slot==0?(i==0||i==6):slot==1?(i==1||i==2||i==3):slot==2?(i==1||i==4||i==5):(i==4||i==5);for(auto& box:g.parts[i].boxes)box.inflate+=slot==2?.5f:1.f;}
    EntityLayer layer;layer.mesh.supported=true;layer.mesh.texture=tex;layer.mesh.modelName=standKind(kind,type)?"ModelArmorStandArmor":kind==1?"ModelZombieArmor":"ModelBipedArmor";g.append(layer.mesh,pose.scale);
    if(material==0){unsigned color=rgb>=0?unsigned(rgb):10511680u;layer.color={{float((color>>16)&255)/255,float((color>>8)&255)/255,float(color&255)/255,1}};}
    layers.push_back(layer);
    if(material==0){int overlay=textureIndex(resource+"_overlay");if(overlay>=0){auto over=layer;over.mesh.texture=overlay;over.color={{1,1,1,1}};layers.push_back(std::move(over));}}
    const int glint=textureIndex("minecraft:misc/enchanted_item_glint");if(enchanted&&glint>=0)for(int pass=0;pass<2;++pass){auto shine=layer;shine.mesh.texture=glint;shine.color={{.5f*.76f,.25f*.76f,.8f*.76f,1}};shine.glint=true;auto uv=emIdentity();emScale(uv,.33333334f,.33333334f,.33333334f);emRotate(uv,2,30.f-float(pass)*60.f);emTranslate(uv,0,pose.ageTicks*(.001f+float(pass)*.003f)*20.f,0);shine.textureMatrix=uv;layers.push_back(std::move(shine));}
    return layers;
}
std::vector<EntityLayer> EntityModels::playerArmor(int itemId,const EntityPose& pose,int rgb,bool enchanted)const{return humanoidArmor(0,0,itemId,pose,rgb,enchanted);}

EntityMesh EntityModels::build(int kind,int type,const EntityPose& pose) const {
    EntityMesh result;
    if(!std::isfinite(pose.headYaw)||!std::isfinite(pose.headPitch)||!std::isfinite(pose.limbSwing)||
       !std::isfinite(pose.limbSwingAmount)||!std::isfinite(pose.ageTicks)||!std::isfinite(pose.swingProgress)||
       !std::isfinite(pose.scale)||pose.scale<=0||pose.scale>256)return result;
    for(float value:{pose.health,pose.partialTick,pose.grazing,pose.rearing,pose.mouthOpen,pose.horseSize,pose.interestedAngle,pose.headShake,pose.maneShake,pose.bodyShake,pose.tailShake,
        pose.jumpProgress,pose.spikeExtension,pose.tailPhase,pose.animationPhase,pose.eyeOffsetX,pose.eyeOffsetY,pose.bodyYaw})if(!std::isfinite(value))return result;
    for(const auto& rotation:{pose.standHead,pose.standBody,pose.standLeftArm,pose.standRightArm,pose.standLeftLeg,pose.standRightLeg})for(float value:rotation)if(!std::isfinite(value))return result;
    for(const auto& offset:pose.dragonOffsets)for(double value:offset)if(!std::isfinite(value)||std::abs(value)>10000000)return result;
    Geometry geometry;std::string texture;
    if(kind==0) {
        geometry=biped(pose,pose.variant==1?1:0);texture=pose.variant==1?"alex":"steve";result.modelName="ModelPlayer";
    } else if(kind==1) {
        switch(type) {
        case 30:geometry=armorStand(pose);texture="armorstand/wood";result.modelName="ModelArmorStand";break;
        case 50:geometry=creeper(pose);texture="creeper/creeper";result.modelName="ModelCreeper";break;
        case 51:geometry=biped(pose,3);if(pose.variant==1)geometry.scale=1.2f;texture=pose.variant==1?"skeleton/wither_skeleton":"skeleton/skeleton";result.modelName="ModelSkeleton";break;
        case 52:case 59:geometry=spider(pose,type==59);texture=type==59?"spider/cave_spider":"spider/spider";result.modelName="ModelSpider";break;
        case 53:case 54:geometry=biped(pose,2);if(type==53)geometry.scale=6;texture="zombie/zombie";result.modelName="ModelZombie";break;
        case 55:geometry=slime(pose,false);texture="slime/slime";result.modelName="ModelSlime";result.approximate=true;break;
        case 56:geometry=ghast(pose);texture=pose.variant?"ghast/ghast_shooting":"ghast/ghast";result.modelName="ModelGhast";result.approximate=true;break;
        case 57:geometry=biped(pose,4);texture="zombie_pigman";result.modelName="ModelZombie";break;
        case 58:geometry=biped(pose,5);texture="enderman/enderman";result.modelName="ModelEnderman";result.approximate=true;break;
        case 60:case 67:geometry=silverfish(pose,type==67);texture=type==67?"endermite":"silverfish";result.modelName=type==67?"ModelEnderMite":"ModelSilverfish";break;
        case 61:geometry=blaze(pose);texture="blaze";result.modelName="ModelBlaze";break;
        case 62:geometry=slime(pose,true);texture="slime/magmacube";result.modelName="ModelMagmaCube";result.approximate=true;break;
        case 63:geometry=dragon(pose);texture="enderdragon/dragon";result.modelName="ModelDragon";result.approximate=true;break;
        case 64:geometry=wither(pose);texture=pose.invulnerabilityTicks>0&&(pose.invulnerabilityTicks>80||pose.invulnerabilityTicks/5%2!=1)?"wither/wither_invulnerable":"wither/wither";result.modelName="ModelWither";result.approximate=true;break;
        case 65:geometry=bat(pose);texture="bat";result.modelName="ModelBat";break;
        case 66:geometry=witch(pose);texture="witch";result.modelName="ModelWitch";break;
        case 68:geometry=guardian(pose);texture=pose.flags&PoseElder?"guardian_elder":"guardian";result.modelName="ModelGuardian";result.approximate=true;break;
        case 90:geometry=quadruped(pose,0);texture="pig/pig";result.modelName="ModelPig";break;
        case 91:geometry=quadruped(pose,2);texture="sheep/sheep";result.modelName="ModelSheep2";result.approximate=true;break;
        case 92:case 96:geometry=quadruped(pose,1);texture=type==96?"cow/mooshroom":"cow/cow";result.modelName="ModelCow";result.approximate=type==96;break;
        case 93:geometry=chicken(pose);texture="chicken";result.modelName="ModelChicken";result.approximate=true;break;
        case 94:geometry=squid(pose);texture="squid";result.modelName="ModelSquid";result.approximate=true;break;
        case 95:geometry=wolf(pose);texture=pose.flags&PoseTamed?"native:wolf/"+std::to_string(std::clamp(pose.collarColor,0,15)):pose.flags&PoseAngry?"wolf/wolf_angry":"wolf/wolf";result.modelName="ModelWolf";break;
        case 97:geometry=snowman(pose);texture="snowman";result.modelName="ModelSnowMan";result.approximate=true;break;
        case 98:{geometry=ocelot(pose);const char* cats[]={"ocelot","black","red","siamese"};texture=std::string("cat/")+cats[std::clamp(pose.variant,0,3)];result.modelName="ModelOcelot";break;}
        case 99:geometry=ironGolem(pose);texture="iron_golem";result.modelName="ModelIronGolem";result.approximate=true;break;
        case 100:{geometry=horse(pose);result.modelName="ModelHorse";
            const char* kinds[]={"","donkey","mule","horse_zombie","horse_skeleton"};
            if(pose.horseType>0&&pose.horseType<=4)texture=std::string("horse/")+kinds[pose.horseType];
            else texture="native:horse/"+std::to_string(std::clamp(pose.variant&255,0,6))+"/"+std::to_string(std::clamp((pose.variant>>8)&255,0,4))+"/"+std::to_string(std::clamp(pose.horseArmor,0,3));
            break;}
        case 101:{geometry=rabbit(pose);const char* rabbits[]={"brown","white","black","white_splotched","gold","salt"};texture=std::string("rabbit/")+((pose.flags&PoseToast)?"toast":pose.variant==99?"caerbannog":rabbits[std::clamp(pose.variant,0,5)]);result.modelName="ModelRabbit";break;}
        case 120:{
            geometry=villager(pose);const char* professions[]={"farmer","librarian","priest","smith","butcher"};
            texture=std::string("villager/")+professions[std::clamp(pose.variant,0,4)];result.modelName="ModelVillager";break;
        }
        default:return result;
        }
    } else if(kind==2) {
        if(type==1){geometry=sourceModelBoat();geometry.originY=4;texture="boat";result.modelName="ModelBoat";result.approximate=true;}
        else if(type==10){geometry=sourceModelMinecart();geometry.originY=6;geometry.named("sideModels[5]").pivot.y=4.1f;texture="minecart";result.modelName="ModelMinecart";result.approximate=true;}
        else if(type==78){geometry=armorStand(pose);texture="armorstand/wood";result.modelName="ModelArmorStand";}
        else return result;
    } else return result;
    result.texture=textureIndex(texture.find(':')!=std::string::npos?texture:"minecraft:entity/"+texture);
    if(result.texture<0)return result;
    result.supported=true;geometry.append(result,pose.scale);return result;
}

} // namespace mc
