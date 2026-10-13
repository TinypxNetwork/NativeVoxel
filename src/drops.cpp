#include "mc/drops.hpp"
#include "mc/drops_data.hpp"
#include "mc/controller.hpp"
#include "mc/nbt.hpp"
#include "mc/registry.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace mc::drops {
namespace {
const data::Rule& rule(BlockState state) {
    static constexpr data::Rule unknown{};
    return state<data::rules.size()?data::rules[state]:unknown;
}
ItemStack stack(int id,int count=1,int damage=0) {
    ItemStack item;item.id=static_cast<std::int16_t>(id);item.count=static_cast<std::int8_t>(count);item.damage=static_cast<std::int16_t>(damage);return item;
}
void spawn(Result& result,const Context& context,Random& random,ItemStack item) {
    if(!context.tileDrops||item.empty())return;
    const double x=static_cast<double>(random.nextFloat()*.5f)+.25;
    const double y=static_cast<double>(random.nextFloat()*.5f)+.25;
    const double z=static_cast<double>(random.nextFloat()*.5f)+.25;
    result.items.push_back({std::move(item),x,y,z});
}
void experience(Result& result,int amount) {
    while(amount>0){const int value=experienceSplit(amount);result.experience.push_back(value);amount-=value;}
}
int shiftedChance(int initial,unsigned value,int fortune,int minimum) {
    if(fortune<=0)return initial;
    // Java masks shift distances and wraps the signed subtraction at 32 bits.
    const std::uint32_t wrapped=static_cast<std::uint32_t>(initial)-(value<<(static_cast<unsigned>(fortune)&31));
    std::int32_t result;std::memcpy(&result,&wrapped,sizeof result);return std::max<int>(minimum,result);
}
int quantity(const data::Rule& info,unsigned id,int fortune,Random& random) {
    switch(info.quantity){
    case data::Quantity::Fixed:return info.count;
    case data::Quantity::Ore:{
        int multiplier=1;
        if(fortune>0&&info.item!=static_cast<int>(id))multiplier=std::max(0,random.nextInt(fortune+2)-1)+1;
        const int base=id==21?4+random.nextInt(5):1;
        return base*multiplier;
    }
    case data::Quantity::Redstone:{const int base=4+random.nextInt(2);return base+random.nextInt(fortune+1);}
    case data::Quantity::Glowstone:{const int base=2+random.nextInt(3);return std::clamp(base+random.nextInt(fortune+1),1,4);}
    case data::Quantity::Melon:{const int base=3+random.nextInt(5);return std::min(base+random.nextInt(fortune+1),9);}
    case data::Quantity::SeaLantern:{const int base=2+random.nextInt(2);return std::clamp(base+random.nextInt(fortune+1),1,5);}
    case data::Quantity::Mushroom:return std::max(0,random.nextInt(10)-7);
    case data::Quantity::TallGrass:return 1+random.nextInt(fortune*2+1);
    case data::Quantity::Leaves:return random.nextInt(20)==0?1:0;
    }
    return 0;
}
int itemDropped(const data::Rule& info,int fortune,Random& random) {
    if(info.itemRule==data::ItemRule::Gravel)return random.nextInt(10-std::min(fortune,3)*3)==0?318:13;
    if(info.itemRule==data::ItemRule::Seeds)return random.nextInt(8)==0?295:-1;
    return info.item;
}
void ordinary(Result& result,const Context& context,const data::Rule& info,int fortune,float chance,Random& random) {
    const int count=quantity(info,blockId(context.state),fortune,random);
    for(int i=0;i<count;++i){
        if(random.nextFloat()<=chance){const int id=itemDropped(info,fortune,random);if(id>=0)spawn(result,context,random,stack(id,1,info.damage));}
    }
}
bool kind(const registry::ItemInfo& info,const char* expected){return std::strcmp(info.kind,expected)==0;}
bool unbreakable(const ItemStack& held) {
    const auto tag=nbt::parse(held.nbt);const auto* value=tag.get("Unbreakable");
    return value&&value->type>=1&&value->type<=4&&static_cast<std::uint8_t>(value->integer)!=0;
}
}

bool knownState(BlockState state){return rule(state).valid;}
bool hasDropRule(BlockState state){return rule(state).valid&&rule(state).behavior!=data::Behavior::Unsupported;}
bool canHarvest(BlockState state,const ItemStack& held){const auto& block=registry::block(blockId(state));return block.valid&&(!block.toolRequired||(!held.empty()&&registry::canHarvest(held.id,blockId(state))));}
int experienceSplit(int amount){for(int value:{2477,1237,617,307,149,73,37,17,7,3})if(amount>=value)return value;return 1;}

ToolDamage damageOnBreak(BlockState state,ItemStack& held,bool creative,Random& worldRandom,Random& playerRandom) {
    ToolDamage result;
    if(creative||held.empty())return result;
    const auto& item=registry::item(held.id);const auto& block=registry::block(blockId(state));
    if(item.maxDamage<=0||unbreakable(held))return result;
    int amount=0;
    if(kind(item,"ItemSword")&&block.hardness!=0)amount=2;
    else if((kind(item,"ItemTool")||kind(item,"ItemPickaxe")||kind(item,"ItemSpade")||kind(item,"ItemAxe"))&&block.hardness!=0)amount=1;
    else if(kind(item,"ItemShears")){
        const auto id=blockId(state);
        if(std::strcmp(block.material,"leaves")==0||id==30||id==31||id==106||id==132||id==35)amount=1;
    }
    if(!amount)return result;
    const int unbreaking=Controller::enchantment(held,34);
    for(int i=0;i<amount;++i)if(unbreaking<=0||playerRandom.nextInt(unbreaking+1)==0)++result.amount;
    if(result.amount==0)return result;
    const int damage=static_cast<int>(held.damage)+result.amount;
    if(damage>item.maxDamage){
        result.broken=true;
        // EntityLivingBase.renderBrokenItemStack evaluates one world draw for
        // pitch and three player draws per particle even on a headless server.
        worldRandom.nextFloat();for(int i=0;i<15;++i)playerRandom.nextFloat();
        --held.count;held.damage=0;if(held.count<=0)held=ItemStack{};
    }else held.damage=static_cast<std::int16_t>(damage);
    return result;
}

Result dropWithChance(const Context& context,int fortune,float chance,Random& random) {
    if(fortune<0||fortune>32767)throw std::invalid_argument("Fortune must fit a nonnegative vanilla enchantment level");
    Result result;const auto& info=rule(context.state);
    if(!info.valid||info.behavior==data::Behavior::Unsupported)return result;
    result.supported=true;const auto id=blockId(context.state);
    switch(info.behavior){
    case data::Behavior::None:return result;
    case data::Behavior::Leaves:{
        const int saplings=shiftedChance(id==18&&info.damage==3?40:20,2,fortune,10);
        if(random.nextInt(saplings)==0)spawn(result,context,random,stack(info.item,1,info.damage));
        const int apples=shiftedChance(200,10,fortune,40);
        if((id==18&&info.damage==0)||(id==161&&info.damage==5))if(random.nextInt(apples)==0)spawn(result,context,random,stack(260));
        return result;
    }
    case data::Behavior::Crops:{
        ordinary(result,context,info,0,chance,random);
        if(info.age>=7){const int seed=id==59?295:(id==141?391:392);
            for(int i=0;i<3+fortune;++i)if(random.nextInt(15)<=info.age)spawn(result,context,random,stack(seed));
            if(id==142&&random.nextInt(50)==0)spawn(result,context,random,stack(394));
        }
        return result;
    }
    case data::Behavior::Cocoa:{const int count=info.age>=2?3:1;for(int i=0;i<count;++i)spawn(result,context,random,stack(351,1,3));return result;}
    case data::Behavior::Wart:{int count=1;if(info.age>=3){count=2+random.nextInt(3);if(fortune>0)count+=random.nextInt(fortune+1);}for(int i=0;i<count;++i)spawn(result,context,random,stack(372));return result;}
    case data::Behavior::Stem:{ordinary(result,context,info,fortune,chance,random);for(int i=0;i<3;++i)if(random.nextInt(15)<=info.age)spawn(result,context,random,stack(id==104?361:362));return result;}
    case data::Behavior::Bed:if(info.canonical&8)return result;ordinary(result,context,info,0,chance,random);return result;
    case data::Behavior::Default:ordinary(result,context,info,fortune,chance,random);break;
    case data::Behavior::Unsupported:return result;
    }
    if(id==16)experience(result,random.nextInt(3));
    else if(id==56||id==129)experience(result,3+random.nextInt(5));
    else if(id==21||id==153)experience(result,2+random.nextInt(4));
    else if(id==73||id==74)experience(result,1+random.nextInt(5));
    else if(id==52){const int first=random.nextInt(15);experience(result,15+first+random.nextInt(15));}
    return result;
}

Result harvest(const Context& context,const ItemStack& held,Random& random) {
    Result result;const auto& info=rule(context.state);if(!info.valid)return result;
    const auto id=blockId(context.state);result.supported=true;
    if(!held.empty()&&held.id==359){
        if(id==18||id==161){spawn(result,context,random,stack(static_cast<int>(id),1,info.canonical&3));return result;}
        if(id==31){spawn(result,context,random,stack(31,1,info.canonical));return result;}
        if(id==32||id==106){spawn(result,context,random,stack(static_cast<int>(id)));return result;}
        if(id==175&&!(info.canonical&8)&&((info.canonical&7)==2||(info.canonical&7)==3)){spawn(result,context,random,stack(31,2,(info.canonical&7)-1));return result;}
    }
    if(id==78){spawn(result,context,random,stack(332,info.age+1));return result;}
    result.exhaustion=.025f;
    if(info.silk&&!held.empty()&&Controller::enchantment(held,33)>0){
        if(info.silkItem>=0)spawn(result,context,random,stack(info.silkItem,info.silkCount,info.silkDamage));
        return result;
    }
    if(id==79&&context.waterVaporizes)return result;
    result=dropWithChance(context,held.empty()?0:Controller::enchantment(held,35),1.0f,random);
    if(result.supported){result.exhaustion=.025f;if(id==79&&context.supportedBelow)result.replacement=blockState(8);}
    return result;
}

} // namespace mc::drops
