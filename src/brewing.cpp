#include "mc/brewing.hpp"
#include "mc/brewing_data.hpp"
#include "mc/crafting.hpp"

namespace mc::brewing {
int result(int metadata,const ItemStack& ingredient){if(ingredient.empty())return metadata;const int variant=ingredient.damage>=0&&ingredient.damage<16?ingredient.damage:0;const char* formula=nullptr;
    for(const auto& rule:data::ingredients)if(rule.item==ingredient.id&&(rule.mask&(1u<<variant))!=0){formula=rule.formula;break;}
    if(!formula)return metadata;
    std::uint32_t value=static_cast<std::uint32_t>(metadata);int bit=0;bool digits=false,clear=false,toggle=false,require=false;
    auto apply=[&]{const std::uint32_t mask=1u<<(bit&31);if(require){if((value&mask)==0)value=0;}else if(clear)value&=~mask;else if(toggle)value^=mask;else value|=mask;};
    for(const char* at=formula;*at;++at){const char c=*at;if(c>='0'&&c<='9'){bit=bit*10+(c-'0');digits=true;}
        else if(c=='!'||c=='-'||c=='+'||c=='&'){if(digits){apply();digits=clear=toggle=require=false;bit=0;}if(c=='!')toggle=true;else if(c=='-')clear=true;else if(c=='&')require=true;}}
    if(digits)apply();
    return static_cast<int>(value&32767u);}
bool changes(int before,int after){if((before&16384)==0&&(after&16384)!=0)return true;if(before==after)return false;
    const auto oldEffects=data::effects[static_cast<unsigned>(before)&32767u],newEffects=data::effects[static_cast<unsigned>(after)&32767u];
    if(oldEffects==0)return before<=0||newEffects!=0;
    return newEffects!=0&&oldEffects!=newEffects;}
bool canBrew(const BrewingState& state){if(state.slots[3].empty()||!crafting::potionIngredient(state.slots[3]))return false;
    for(int i=0;i<3;++i)if(!state.slots[static_cast<std::size_t>(i)].empty()&&state.slots[static_cast<std::size_t>(i)].id==373){const int before=state.slots[static_cast<std::size_t>(i)].damage;if(changes(before,result(before,state.slots[3])))return true;}
    return false;}
void tick(BrewingState& state){if(state.brewTime>0){--state.brewTime;if(state.brewTime==0){if(canBrew(state)){
                for(int i=0;i<3;++i){auto& potion=state.slots[static_cast<std::size_t>(i)];if(!potion.empty()&&potion.id==373){const int next=result(potion.damage,state.slots[3]);if(changes(potion.damage,next))potion.damage=static_cast<std::int16_t>(next);}}
                auto& ingredient=state.slots[3];const auto remaining=crafting::containerItem(ingredient);if(!remaining.empty())ingredient=remaining;else{--ingredient.count;if(ingredient.count<=0)ingredient=ItemStack{};}}
            state.dirty=true;
        }else if(!canBrew(state)||state.ingredientId!=state.slots[3].id){state.brewTime=0;state.dirty=true;}}
    else if(canBrew(state)){state.brewTime=400;state.ingredientId=state.slots[3].id;}
    int filled=0;for(int i=0;i<3;++i)if(!state.slots[static_cast<std::size_t>(i)].empty())filled|=1<<i;state.filledMask=filled;}
}
