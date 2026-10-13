// Including the implementation lets this development test compare private
// ModelRenderer-like joint state; the application still compiles its own TU.
#include "../src/entity_models.cpp"
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>

namespace {
mc::EntityPose pose(const std::string& parameters) {
    mc::EntityPose p;
    const std::map<std::string,float mc::EntityPose::*> floats={
        {"headYaw",&mc::EntityPose::headYaw},{"headPitch",&mc::EntityPose::headPitch},{"limbSwing",&mc::EntityPose::limbSwing},
        {"limbSwingAmount",&mc::EntityPose::limbSwingAmount},{"ageTicks",&mc::EntityPose::ageTicks},{"health",&mc::EntityPose::health},
        {"partialTick",&mc::EntityPose::partialTick},{"grazing",&mc::EntityPose::grazing},{"rearing",&mc::EntityPose::rearing},
        {"mouthOpen",&mc::EntityPose::mouthOpen},{"horseSize",&mc::EntityPose::horseSize},{"interestedAngle",&mc::EntityPose::interestedAngle},
        {"headShake",&mc::EntityPose::headShake},{"maneShake",&mc::EntityPose::maneShake},{"bodyShake",&mc::EntityPose::bodyShake},
        {"tailShake",&mc::EntityPose::tailShake},{"jumpProgress",&mc::EntityPose::jumpProgress},{"spikeExtension",&mc::EntityPose::spikeExtension},
        {"tailPhase",&mc::EntityPose::tailPhase},{"bodyYaw",&mc::EntityPose::bodyYaw}};
    const std::map<std::string,std::array<float,3> mc::EntityPose::*> triples={
        {"standHead",&mc::EntityPose::standHead},{"standBody",&mc::EntityPose::standBody},{"standLeftArm",&mc::EntityPose::standLeftArm},
        {"standRightArm",&mc::EntityPose::standRightArm},{"standLeftLeg",&mc::EntityPose::standLeftLeg},{"standRightLeg",&mc::EntityPose::standRightLeg}};
    std::istringstream in(parameters);std::string field;
    while(in>>field){const auto split=field.find('=');if(split==std::string::npos)throw std::runtime_error("bad pose parameter");
        const auto key=field.substr(0,split),value=field.substr(split+1);auto f=floats.find(key);
        if(f!=floats.end())p.*(f->second)=std::stof(value);
        else if(key=="flags")p.flags=std::stoul(value);
        else if(key=="entityId")p.entityId=std::stoi(value);
        else if(key=="horseType")p.horseType=std::stoi(value);
        else if(key=="invulnerabilityTicks")p.invulnerabilityTicks=std::stoi(value);
        else if(key=="child")p.child=std::stoi(value)!=0;
        else if(key=="sneaking")p.sneaking=std::stoi(value)!=0;
        else if(key.rfind("sideHeadYaw",0)==0)p.sideHeadYaw.at(unsigned(key.back()-'0'))=std::stof(value);
        else if(key.rfind("sideHeadPitch",0)==0)p.sideHeadPitch.at(unsigned(key.back()-'0'))=std::stof(value);
        else{auto triple=triples.find(key.substr(0,key.size()-1));if(triple==triples.end())throw std::runtime_error("unknown pose parameter: "+key);(p.*(triple->second)).at(unsigned(key.back()-'0'))=std::stof(value);}
    }
    return p;
}
mc::Geometry geometry(const std::string& name,const mc::EntityPose& p) {
    if(name=="wolf")return mc::wolf(p);if(name=="ocelot")return mc::ocelot(p);
    if(name=="horse")return mc::horse(p);if(name=="rabbit")return mc::rabbit(p);
    if(name=="witch")return mc::witch(p);if(name=="guardian")return mc::guardian(p);
    if(name=="wither")return mc::wither(p);if(name=="bat")return mc::bat(p);
    if(name=="armorstand")return mc::armorStand(p);
    throw std::runtime_error("unknown model family");
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("entity_models_tests requires a pose fixture file");
        std::ifstream in(argv[1]);if(!in)throw std::runtime_error("cannot open model fixture");
        std::cout<<std::setprecision(17);std::string line;int sample=0;
        while(std::getline(in,line)){if(line.empty()||line[0]=='#')continue;const auto space=line.find(' ');const auto family=line.substr(0,space);
            const auto p=pose(space==std::string::npos?"":line.substr(space+1));const auto g=geometry(family,p);
            for(const auto& part:g.parts)std::cout<<sample<<' '<<part.name<<' '<<part.pivot.x<<' '<<part.pivot.y<<' '<<part.pivot.z<<' '
                <<part.angle.x<<' '<<part.angle.y<<' '<<part.angle.z<<' '<<part.offset.x<<' '<<part.offset.y<<' '<<part.offset.z<<'\n';
            ++sample;
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
