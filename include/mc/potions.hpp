#pragma once
#include "mc/game.hpp"
#include "mc/nbt.hpp"
#include <algorithm>
#include <cmath>

namespace mc::potions {
struct PotionEffect {int id{},amplifier{},duration{};bool showParticles{true};};
inline constexpr int type(int metadata){constexpr int ids[]={0,10,1,12,19,6,16,0,18,5,2,8,7,13,14,0};return ids[unsigned(metadata)&15u];}
inline constexpr bool instant(int id){return id==6||id==7||id==23;}
inline const char* key(int id){static constexpr const char* keys[]={"","potion.moveSpeed","potion.moveSlowdown","potion.digSpeed","potion.digSlowDown","potion.damageBoost","potion.heal","potion.harm","potion.jump","potion.confusion","potion.regeneration","potion.resistance","potion.fireResistance","potion.waterBreathing","potion.invisibility","potion.blindness","potion.nightVision","potion.hunger","potion.weakness","potion.poison","potion.wither","potion.healthBoost","potion.absorption","potion.saturation"};return id>0&&id<24?keys[id]:"";}
inline constexpr int effectColor(int id){constexpr int colors[]={3694022,8171462,5926017,14270531,4866583,9643043,16262179,4393481,2293580,5578058,13458603,10044730,14981690,3035801,8356754,2039587,2039713,5797459,4738376,5149489,3484199,16284963,2445989,16262179};return id>=0&&id<24?colors[id]:3694022;}
inline constexpr int color(int metadata){return effectColor(type(metadata));}
inline const char* prefix(int metadata){static constexpr const char* prefixes[]={"mundane","uninteresting","bland","clear","milky","diffuse","artless","thin","awkward","flat","bulky","bungling","buttered","smooth","suave","debonair","thick","elegant","fancy","charming","dashing","refined","cordial","sparkling","potent","foul","odorless","rank","harsh","acrid","gross","stinky"};return prefixes[(unsigned(metadata)>>1)&31u];}
inline std::vector<PotionEffect> effects(const ItemStack& stack){
    if(!stack.nbt.empty()&&stack.nbt[0])try{
        const auto root=nbt::parse(stack.nbt);const auto* custom=root.get("CustomPotionEffects");
        if(custom&&custom->type==9){std::vector<PotionEffect> result;
            if(custom->subtype==10)for(const auto& value:custom->list){
                auto integer=[&](const char* name,int fallback=0){const auto* tag=value.get(name);return tag&&tag->type>=1&&tag->type<=4?int(tag->integer):fallback;};
                const int id=static_cast<std::int8_t>(integer("Id"));if(id<=0||id>=24)continue;
                result.push_back({id,static_cast<std::int8_t>(integer("Amplifier")),integer("Duration"),integer("ShowParticles",1)!=0});
            }return result;
        }
    }catch(const net::ProtocolError&){}
    const int id=type(stack.damage);if(!id)return {};
    const bool scales=id==1||id==5||id==6||id==7||id==8||id==10||id==19;
    const int amplifier=scales?((unsigned(stack.damage)>>5)&1u):0;
    int duration=1;
    if(!instant(id)){
        duration=((stack.damage&64)?9600:3600)>>amplifier;
        const double effectiveness=id==10||id==19?.25:id==2||id==18?.5:1.;
        duration=int(std::floor(duration*effectiveness+.5));
        if(stack.damage&16384)duration=int(std::floor(duration*.75+1.));
    }
    return {{id,amplifier,duration,true}};
}
} // namespace mc::potions
