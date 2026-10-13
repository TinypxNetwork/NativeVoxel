#pragma once

#include "mc/gui_nbt.hpp"
#include "mc/item_nbt.hpp"
#include "mc/i18n.hpp"
#include "mc/potions.hpp"
#include <cmath>
#include <functional>
#include <iomanip>
#include <sstream>

namespace mc::item_tooltip {
inline const char* enchantmentKey(int id){switch(id){
    case 0:return "enchantment.protect.all";case 1:return "enchantment.protect.fire";case 2:return "enchantment.protect.fall";case 3:return "enchantment.protect.explosion";case 4:return "enchantment.protect.projectile";case 5:return "enchantment.oxygen";case 6:return "enchantment.waterWorker";case 7:return "enchantment.thorns";case 8:return "enchantment.waterWalker";
    case 16:return "enchantment.damage.all";case 17:return "enchantment.damage.undead";case 18:return "enchantment.damage.arthropods";case 19:return "enchantment.knockback";case 20:return "enchantment.fire";case 21:return "enchantment.lootBonus";case 32:return "enchantment.digging";case 33:return "enchantment.untouching";case 34:return "enchantment.durability";case 35:return "enchantment.lootBonusDigger";
    case 48:return "enchantment.arrowDamage";case 49:return "enchantment.arrowKnockback";case 50:return "enchantment.arrowFire";case 51:return "enchantment.arrowInfinite";case 61:return "enchantment.lootBonusFishing";case 62:return "enchantment.fishingSpeed";default:return nullptr;}}
inline std::string enchantmentName(int id,int level,const I18n& tr){
    const auto* key=enchantmentKey(id);if(!key)return {};auto value=tr(key);
    if(level!=1||(id!=6&&id!=33&&id!=50&&id!=51))value+=' '+tr("enchantment.level."+std::to_string(level));return value;
}
inline std::string format(const std::string& value,const std::vector<std::string>& arguments){
    std::string out;std::size_t sequential=0;
    for(std::size_t i=0;i<value.size();){
        if(value[i]!='%'||i+1==value.size()){out+=value[i++];continue;}
        if(value[i+1]=='%'){out+='%';i+=2;continue;}
        std::size_t end=i+1,index=0;bool numbered=false;
        while(end<value.size()&&value[end]>='0'&&value[end]<='9'){numbered=true;index=index*10+unsigned(value[end++]-'0');}
        if(numbered&&end<value.size()&&value[end]=='$'){++end;index=index?index-1:arguments.size();}
        else {end=i+1;index=sequential++;}
        if(end<value.size()&&(value[end]=='s'||value[end]=='d'||value[end]=='f')&&index<arguments.size()){out+=arguments[index];i=end+1;}
        else out+=value[i++];
    }return out;
}
inline std::string decimal(double value){
    std::ostringstream out;out<<std::fixed<<std::setprecision(3)<<value;auto text=out.str();
    while(text.size()>1&&text.back()=='0')text.pop_back();if(!text.empty()&&text.back()=='.')text.pop_back();return text;
}
struct Attribute {std::string name;double amount{};int operation{};};
inline void attributeLine(std::vector<std::string>& rows,const Attribute& value,const I18n& tr){
    if(value.amount==0||!std::isfinite(value.amount))return;
    const auto key="attribute.modifier."+std::string(value.amount>0?"plus.":"take.")+std::to_string(value.operation);
    rows.push_back(std::string(u8"\u00a7")+(value.amount>0?'9':'c')+format(tr(key),{decimal(std::abs(value.amount*(value.operation?100:1))),tr("attribute.name."+value.name)}));
}
inline bool badEffect(int id){return id==2||id==4||id==7||id==9||id==15||id==17||id==18||id==19||id==20;}
inline void potionInformation(const ItemStack& item,std::vector<std::string>& rows,const I18n& tr){
    if(item.damage==0)return;const auto effects=potions::effects(item);std::vector<Attribute> attributes;
    if(effects.empty())rows.push_back(std::string(u8"\u00a77")+tr("potion.empty"));
    for(const auto& effect:effects){
        auto line=tr(potions::key(effect.id));
        if(effect.amplifier>0)line+=' '+tr("potion.potency."+std::to_string(effect.amplifier));
        if(effect.duration>20){const int seconds=effect.duration/20;line+=" ("+std::to_string(seconds/60)+":"+(seconds%60<10?"0":"")+std::to_string(seconds%60)+")";}
        rows.push_back(std::string(u8"\u00a7")+(badEffect(effect.id)?'c':'7')+line);
        switch(effect.id){
        case 1:attributes.push_back({"generic.movementSpeed",.20000000298023224*(effect.amplifier+1),2});break;
        case 2:attributes.push_back({"generic.movementSpeed",-.15000000596046448*(effect.amplifier+1),2});break;
        case 5:attributes.push_back({"generic.attackDamage",1.3*(effect.amplifier+1),2});break;
        case 18:attributes.push_back({"generic.attackDamage",-.5*(effect.amplifier+1),0});break;
        case 21:attributes.push_back({"generic.maxHealth",4.*(effect.amplifier+1),0});break;
        default:break;
        }
    }
    if(!attributes.empty()){rows.push_back("");rows.push_back(std::string(u8"\u00a75")+tr("potion.effects.whenDrank"));for(const auto& attribute:attributes)attributeLine(rows,attribute,tr);}
}

// ItemStack.getTooltip, shared by containers and show_item chat components.
// A name resolver keeps resource-pack item translations identical in both paths.
inline std::vector<std::string> item(const ItemStack& item,const I18n& tr,
                                    const std::function<std::string(const ItemStack&)>& name,bool advanced=false){
    if(item.empty())return {};const std::string section=u8"\u00a7";const auto& info=registry::item(item.id);
    nbt::Tag parsed;const nbt::Tag* tag=nullptr;
    if(!item.nbt.empty()&&item.nbt.front()!=0)try{parsed=nbt::parse(item.nbt);if(parsed.type==10)tag=&parsed;}catch(const net::ProtocolError&){}
    const auto* display=tag?tag->get("display"):nullptr;const auto custom=display?gui_nbt::string(*display,"Name"):std::string{};
    const int flags=tag?int(item_nbt::integer(*tag,"HideFlags")):0;
    const auto* enchanted=tag?tag->get("ench"):nullptr;const auto* stored=tag?tag->get("StoredEnchantments"):nullptr;
    char rarity=enchanted&&enchanted->type==9?'b':'f';
    if(item.id==403&&stored&&stored->type==9&&!stored->list.empty())rarity='e';
    if(item.id==322)rarity=item.damage==0?'b':'d';
    if(item.id>=2256&&item.id<=2267)rarity='b';
    std::string displayName=custom.empty()?name(item):custom;
    if(item.id==387&&custom.empty()&&tag){const auto title=gui_nbt::string(*tag,"title");if(!title.empty())displayName=title;}
    auto first=section+rarity+(custom.empty()?std::string{}:section+'o')+displayName+section+'r';
    if(advanced){std::ostringstream suffix;suffix<<" (#"<<std::setw(4)<<std::setfill('0')<<item.id;if(info.hasSubtypes)suffix<<'/'<<item.damage;suffix<<')';first+=suffix.str();}
    else if(custom.empty()&&item.id==358)first+=" #"+std::to_string(item.damage);
    std::vector<std::string> rows{first};
    auto appendEnchantments=[&](const nbt::Tag* values){
        if(values&&values->type==9&&values->subtype==10)for(const auto& value:values->list){
            const auto title=enchantmentName(std::int16_t(item_nbt::integer(value,"id")),std::int16_t(item_nbt::integer(value,"lvl")),tr);
            if(!title.empty())rows.push_back(section+'7'+title);
        }
    };
    if(!(flags&32)) {
        if(item.id==403)appendEnchantments(stored);
        if(item.id==373)potionInformation(item,rows,tr);
        if(item.id==387&&tag){const auto author=gui_nbt::string(*tag,"author");if(!author.empty())rows.push_back(section+'7'+format(tr("book.byAuthor"),{author}));rows.push_back(section+'7'+tr("book.generation."+std::to_string(item_nbt::integer(*tag,"generation"))));}
        if(item.id>=2256&&item.id<=2267){static const char* records[]={"13","cat","blocks","chirp","far","mall","mellohi","stal","strad","ward","11","wait"};rows.push_back(section+'7'+tr(std::string("item.record.")+records[item.id-2256]+".desc"));}
    }
    if(!(flags&1))appendEnchantments(enchanted);
    if(display){
        if(const auto* color=display->get("color");color&&color->type==3){
            if(advanced){std::ostringstream value;value<<std::uppercase<<std::hex<<std::uint32_t(color->integer);rows.push_back(section+'7'+"Color: #"+value.str());}
            else rows.push_back(section+'7'+section+'o'+tr("item.dyed"));
        }
        if(const auto* lore=display->get("Lore");lore&&lore->type==9&&lore->subtype==8)for(const auto& line:lore->list)rows.push_back(section+'5'+section+'o'+gui_nbt::utf8(line.string));
    }
    if(!(flags&2)) {
        std::vector<Attribute> attributes;const auto* customAttributes=tag?tag->get("AttributeModifiers"):nullptr;
        if(customAttributes&&customAttributes->type==9){
            if(customAttributes->subtype==10)for(const auto& value:customAttributes->list){const auto* amount=value.get("Amount");const auto operation=item_nbt::integer(value,"Operation");
                if(!amount||amount->type<1||amount->type>6||operation<0||operation>2||!item_nbt::integer(value,"UUIDMost")||!item_nbt::integer(value,"UUIDLeast"))continue;
                attributes.push_back({gui_nbt::string(value,"AttributeName"),amount->type<=4?double(amount->integer):amount->number,int(operation)});
            }
        } else if(info.attackDamage>0){
            double amount=info.attackDamage;
            if(enchanted&&enchanted->type==9&&enchanted->subtype==10)for(const auto& value:enchanted->list)if(item_nbt::integer(value,"id")==16){amount+=item_nbt::integer(value,"lvl")*1.25;break;}
            attributes.push_back({"generic.attackDamage",amount,0});
        }
        if(!attributes.empty()){rows.push_back("");for(const auto& attribute:attributes)attributeLine(rows,attribute,tr);}
    }
    const bool unbreakable=tag&&item_nbt::integer(*tag,"Unbreakable")!=0;
    if(unbreakable&&!(flags&4))rows.push_back(section+'9'+tr("item.unbreakable"));
    if(tag)for(const auto& field:{std::pair<const char*,int>{"CanDestroy",8},{"CanPlaceOn",16}}){
        const auto* blocks=tag->get(field.first);if((flags&field.second)||!blocks||blocks->type!=9||blocks->subtype!=8||blocks->list.empty())continue;
        rows.push_back("");rows.push_back(section+'7'+tr(field.second==8?"item.canBreak":"item.canPlace"));
        for(const auto& block:blocks->list){auto key=gui_nbt::utf8(block.string);if(key.find(':')==std::string::npos)key="minecraft:"+key;
            int id=-1;for(unsigned i=0;i<198;++i)if(registry::block(i).valid&&key==registry::block(i).name){id=int(i);break;}
            if(id<0){rows.push_back(section+'8'+"missingno");continue;}
            ItemStack stack;stack.id=std::int16_t(id);stack.count=1;rows.push_back(section+'8'+name(stack));
        }
    }
    if(advanced){
        if(info.maxDamage>0&&item.damage>0&&!unbreakable)rows.push_back(section+'7'+"Durability: "+std::to_string(info.maxDamage-item.damage)+" / "+std::to_string(info.maxDamage));
        rows.push_back(section+'8'+info.name);if(tag)rows.push_back(section+'8'+"NBT: "+std::to_string(tag->compound.size())+" tag(s)");
    }
    return rows;
}
} // namespace mc::item_tooltip
