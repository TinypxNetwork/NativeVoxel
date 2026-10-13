#include "mc/crafting.hpp"
#include "mc/registry.hpp"
#include "mc/nbt.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

#include "mc/crafting_data.hpp"

namespace mc::crafting {
namespace {
ItemStack makeStack(const data::Stack& input) {
    ItemStack result;
    if (input.id < 0 || input.count <= 0) return result;
    result.id = static_cast<std::int16_t>(input.id); result.count = static_cast<std::int8_t>(input.count); result.damage = static_cast<std::int16_t>(input.damage);
    const auto hex = [](char digit) -> unsigned { return digit >= '0' && digit <= '9' ? static_cast<unsigned>(digit-'0') : static_cast<unsigned>(digit-'a'+10); };
    for (std::size_t i=0; input.nbt[i]; i+=2) result.nbt.push_back(static_cast<std::uint8_t>((hex(input.nbt[i])<<4)|hex(input.nbt[i+1])));
    return result;
}
bool ingredientMatches(const data::Ingredient& ingredient,const ItemStack& stack) {
    if (ingredient.id < 0) return stack.empty();
    return !stack.empty() && ingredient.id == stack.id && (ingredient.damage == 32767 || ingredient.damage == stack.damage);
}
bool shapedMatches(const data::Recipe& recipe,const std::vector<ItemStack>& grid,int width,int height) {
    const ItemStack empty;
    // The original ShapedRecipes scans a 3x3 envelope even for a 2x2 matrix.
    for (int ox=0; ox<=3-recipe.width; ++ox) for (int oy=0; oy<=3-recipe.height; ++oy) for (int mirror=1; mirror>=0; --mirror) {
        bool valid=true;
        for (int x=0; x<3 && valid; ++x) for (int y=0; y<3 && valid; ++y) {
            const int rx=x-ox,ry=y-oy;
            data::Ingredient expected{-1,0};
            if (rx>=0 && ry>=0 && rx<recipe.width && ry<recipe.height)
                expected=recipe.ingredients[static_cast<std::size_t>((mirror ? recipe.width-rx-1 : rx)+ry*recipe.width)];
            const auto& actual=x<width&&y<height?grid[static_cast<std::size_t>(x+y*width)]:empty;
            valid=ingredientMatches(expected,actual);
        }
        if (valid) return true;
    }
    return false;
}
bool shapelessMatches(const data::Recipe& recipe,const std::vector<ItemStack>& grid) {
    std::array<bool,9> used{};
    int count=0;
    for (const auto& input:grid) {
        if (input.empty()) continue;
        bool found=false;
        for (int i=0; i<recipe.count; ++i) if (!used[static_cast<std::size_t>(i)] && ingredientMatches(recipe.ingredients[static_cast<std::size_t>(i)],input)) {
            used[static_cast<std::size_t>(i)]=true; ++count; found=true; break;
        }
        if (!found) return false;
    }
    return count==recipe.count;
}
ItemStack repair(const std::vector<ItemStack>& grid) {
    const ItemStack* first=nullptr;
    const ItemStack* second=nullptr;
    for (const auto& input:grid) {
        if (input.empty()) continue;
        if (!first) first=&input;
        else if (!second) second=&input;
        else return {};
    }
    if (!first || !second || first->id!=second->id || first->count!=1 || second->count!=1) return {};
    const int maximum=registry::item(first->id).maxDamage;
    if (maximum<=0) return {};
    const int remaining=(maximum-first->damage)+(maximum-second->damage)+maximum*5/100;
    ItemStack result; result.id=first->id; result.count=1; result.damage=static_cast<std::int16_t>(std::max(0,maximum-remaining));
    return result;
}
const data::ItemRule& rule(int id) {
    static constexpr data::ItemRule missing{};
    return id>=0&&static_cast<std::size_t>(id)<data::items.size()?data::items[static_cast<std::size_t>(id)]:missing;
}
using Enchantments=std::vector<std::pair<int,int>>;
Enchantments enchantments(const ItemStack& stack) {
    Enchantments result;const auto root=nbt::parse(stack.nbt);const auto* list=root.get(stack.id==403?"StoredEnchantments":"ench");
    if(!list||list->type!=9||list->subtype!=10)return result;
    for(const auto& tag:list->list){const auto* id=tag.get("id");const auto* level=tag.get("lvl");if(!id||!level)continue;
        const int key=static_cast<std::int16_t>(id->integer),value=static_cast<std::int16_t>(level->integer);
        auto found=std::find_if(result.begin(),result.end(),[&](const auto& pair){return pair.first==key;});
        if(found==result.end())result.push_back({key,value});else found->second=value;}
    return result;
}
void setEnchantments(nbt::Tag& root,int itemId,const Enchantments& values) {
    nbt::Tag result;result.type=9;result.subtype=10;
    for(const auto& value:values) {
        if(value.first<0||value.first>=256||!data::enchantments[static_cast<std::size_t>(value.first)].valid)continue;
        nbt::Tag enchant;enchant.type=10;enchant.compound["id"]=nbt::integer(2,value.first);enchant.compound["lvl"]=nbt::integer(2,value.second);result.list.push_back(enchant);
        if(itemId==403) {
            auto& stored=root.compound["StoredEnchantments"];if(stored.type!=9||stored.subtype!=10){stored=nbt::Tag{};stored.type=9;stored.subtype=10;}
            bool found=false;
            for(auto& existing:stored.list){const auto* id=existing.get("id");const auto* level=existing.get("lvl");if(id&&level&&id->integer==value.first){if(level->integer<value.second)existing.compound["lvl"]=nbt::integer(2,value.second);found=true;break;}}
            if(!found)stored.list.push_back(std::move(enchant));
        }
    }
    if(!result.list.empty()){if(itemId!=403)root.compound["ench"]=std::move(result);}else root.compound.erase("ench");
}
int repairCost(const nbt::Tag& root) {const auto* tag=root.get("RepairCost");return tag?static_cast<std::int32_t>(tag->integer):0;}
bool damageable(const ItemStack& stack,const nbt::Tag& root) {
    const auto* unbreakable=root.get("Unbreakable");return registry::item(stack.id).maxDamage>0&&(!unbreakable||unbreakable->integer==0);
}
bool blank(const std::string& name) {
    if(name.empty())return true;
    for(std::size_t i=0;i<name.size();) {
        unsigned cp=static_cast<unsigned char>(name[i++]);int extra=0;
        if(cp>=0xf0){cp&=7;extra=3;}else if(cp>=0xe0){cp&=15;extra=2;}else if(cp>=0xc0){cp&=31;extra=1;}
        for(int j=0;j<extra&&i<name.size();++j)cp=(cp<<6)|(static_cast<unsigned char>(name[i++])&63);
        if(!((cp>=9&&cp<=13)||(cp>=28&&cp<=32)||cp==0x1680||(cp>=0x2000&&cp<=0x2006)||(cp>=0x2008&&cp<=0x200a)||cp==0x2028||cp==0x2029||cp==0x205f||cp==0x3000))return false;
    }
    return true;
}
int wrapped(std::int64_t value){const auto bits=static_cast<std::uint32_t>(value);std::int32_t out;std::memcpy(&out,&bits,sizeof out);return out;}
} // namespace

Result match(const std::vector<ItemStack>& grid,int width,int height) {
    if (width<=0 || width>3 || height<=0 || height>3 || grid.size()!=static_cast<std::size_t>(width*height))
        throw net::ProtocolError("Invalid crafting matrix dimensions");
    Result result; result.remaining.resize(grid.size());
    for (std::size_t index=0; index<data::recipes.size(); ++index) {
        const auto& recipe=data::recipes[index];
        if (recipe.kind==0) {
            if (!shapedMatches(recipe,grid,width,height)) continue;
            result.output=makeStack(recipe.output);
        } else if (recipe.kind==1) {
            if (!shapelessMatches(recipe,grid)) continue;
            result.output=makeStack(recipe.output);
        } else if (recipe.kind==2) {
            result.output=repair(grid); if (result.output.empty()) continue;
        } else continue;
        if (recipe.copyNbt) for (const auto& stack:grid) if (!stack.empty() && !stack.nbt.empty() && stack.nbt[0]!=0) result.output.nbt=stack.nbt;
        result.matched=true; result.recipeIndex=static_cast<int>(index);
        for (std::size_t i=0; i<grid.size(); ++i) result.remaining[i]=containerItem(grid[i]);
        return result;
    }
    // CraftingManager.func_180303_b returns the original inputs if no recipe
    // matches. A normal crafting result is empty in this case.
    result.remaining=grid;
    return result;
}
void refresh(Game& game) {
    std::vector<ItemStack> playerGrid(game.inventory.begin()+1,game.inventory.begin()+5);
    game.inventory[0]=match(playerGrid,2,2).output;
    if (game.window.open && game.window.type=="minecraft:crafting_table" && game.window.slots.size()>=10) {
        std::vector<ItemStack> grid(game.window.slots.begin()+1,game.window.slots.begin()+10);
        game.window.slots[0]=match(grid,3,3).output;
    }
}
ItemStack smeltingResult(const ItemStack& input) {
    if (input.empty()) return {};
    for (const auto& recipe:data::smelting) if (ingredientMatches(recipe.input,input)) return makeStack(recipe.output);
    return {};
}
ItemStack containerItem(const ItemStack& input) {
    if (input.empty()) return {};
    const int id=rule(input.id).container;
    if (id<0) return {};
    ItemStack result; result.id=static_cast<std::int16_t>(id); result.count=1; return result;
}
int fuelBurnTime(const ItemStack& input) {
    if (input.empty()) return 0;
    const int meta=input.damage>=0&&input.damage<16?input.damage:0;
    return rule(input.id).fuel[static_cast<std::size_t>(meta)];
}
bool potionIngredient(const ItemStack& input) {
    if (input.empty()) return false;
    const int meta=input.damage>=0&&input.damage<16?input.damage:0;
    return (rule(input.id).potionMask&(1u<<meta))!=0;
}
bool repairIngredient(int itemId,int ingredientId) {
    const auto& info=rule(itemId);
    return std::find(info.repairMaterials.begin(),info.repairMaterials.begin()+info.repairCount,ingredientId)!=info.repairMaterials.begin()+info.repairCount;
}
int merchantResult(const ContainerWindow& window,const std::vector<ItemStack>& slots,ItemStack& result) {
    result=slots.size()>2?slots[2]:ItemStack{};
    if(slots.size()<3)return -1;
    const ItemStack* first=&slots[0];const ItemStack* second=&slots[1];
    const ItemStack empty;
    if(first->empty()){first=second;second=&empty;}
    if(first->empty()){result=ItemStack{};return -1;}
    if(!window.tradesLoaded)return -1;
    auto matches=[](const ItemStack& given,const ItemStack& price) {
        if(given.empty()||price.empty()||given.id!=price.id||given.damage!=price.damage||given.count<price.count)return false;
        if(price.nbt.empty()||price.nbt[0]==0)return true;
        return !given.nbt.empty()&&given.nbt[0]!=0&&nbt::subset(nbt::parse(price.nbt),nbt::parse(given.nbt));
    };
    auto find=[&](const ItemStack& a,const ItemStack& b) {
        auto usable=[&](const TradeOffer& offer){return matches(a,offer.first)&&(offer.second.empty()?b.empty():matches(b,offer.second));};
        if(window.selectedTrade>0&&static_cast<std::size_t>(window.selectedTrade)<window.trades.size())
            return usable(window.trades[static_cast<std::size_t>(window.selectedTrade)])?window.selectedTrade:-1;
        for(std::size_t i=0;i<window.trades.size();++i)if(usable(window.trades[i]))return static_cast<int>(i);
        return -1;
    };
    int selected=find(*first,*second);
    auto enabled=[&](int index){return index>=0&&!window.trades[static_cast<std::size_t>(index)].disabled&&
        window.trades[static_cast<std::size_t>(index)].uses<window.trades[static_cast<std::size_t>(index)].maxUses;};
    if(!enabled(selected)&&!second->empty())selected=find(*second,*first);
    if(enabled(selected)){result=window.trades[static_cast<std::size_t>(selected)].result;return selected;}
    result=ItemStack{};return -1;
}
void refreshMerchant(Game& game) {
    if(!game.window.open||game.window.type!="minecraft:villager"||game.window.slots.size()<3)return;
    ItemStack result;game.window.activeTrade=merchantResult(game.window,game.window.slots,result);game.window.slots[2]=std::move(result);
}
RepairResult anvilResult(const ItemStack& first,const ItemStack& second,const std::string& name,bool creative) {
    RepairResult result;if(first.empty())return result;
    ItemStack output=first;auto root=nbt::parse(output.nbt);if(root.type==0)root.type=10;
    const auto otherRoot=nbt::parse(second.nbt);auto enchantMap=enchantments(output);
    const int base=wrapped(static_cast<std::int64_t>(repairCost(root))+(second.empty()?0:repairCost(otherRoot)));
    int work=0,rename=0;
    if(!second.empty()) {
        const auto* stored=otherRoot.get("StoredEnchantments");const bool book=second.id==403&&stored&&stored->type==9&&!stored->list.empty();
        const int maximum=registry::item(first.id).maxDamage;
        if(damageable(output,root)&&repairIngredient(first.id,second.id)) {
            int amount=std::min<int>(output.damage,maximum/4);if(amount<=0)return result;
            for(int i=0;amount>0&&i<second.count;++i){output.damage=static_cast<std::int16_t>(output.damage-amount);++work;++result.materialCost;amount=std::min<int>(output.damage,maximum/4);}
        } else {
            if(!book&&(first.id!=second.id||!damageable(output,root)))return result;
            if(damageable(output,root)&&!book) {
                const int combined=(maximum-first.damage)+(maximum-second.damage)+maximum*12/100;
                const int damage=std::max(0,maximum-combined);
                if(damage<output.damage){output.damage=static_cast<std::int16_t>(damage);work+=2;}
            }
            for(const auto& entry:enchantments(second)) {
                const int id=entry.first;if(id<0||id>=256)continue;const auto& rule=data::enchantments[static_cast<std::size_t>(id)];if(!rule.valid)continue;
                auto old=std::find_if(enchantMap.begin(),enchantMap.end(),[&](const auto& pair){return pair.first==id;});
                const int previous=old==enchantMap.end()?0:old->second;
                int level=previous==entry.second?entry.second+1:std::max(previous,entry.second);
                bool canApply=first.id>=0&&first.id<2304&&(rule.items[static_cast<std::size_t>(first.id/64)]&(std::uint64_t{1}<<(first.id%64)))!=0;
                if(creative||first.id==403)canApply=true;
                for(const auto& existing:enchantMap)if(existing.first!=id&&existing.first>=0&&existing.first<256&&
                    (rule.compatible[static_cast<std::size_t>(existing.first/64)]&(std::uint64_t{1}<<(existing.first%64)))==0){canApply=false;work=wrapped(static_cast<std::int64_t>(work)+1);}
                if(canApply){level=std::min(level,rule.maxLevel);if(old==enchantMap.end())enchantMap.push_back({id,level});else old->second=level;
                    int cost=rule.weight==1?8:(rule.weight==2?4:(rule.weight==5?2:(rule.weight==10?1:0)));if(book)cost=std::max(1,cost/2);work=wrapped(static_cast<std::int64_t>(work)+static_cast<std::int64_t>(cost)*level);}
            }
        }
    }
    const auto* display=root.get("display");const auto* oldName=display&&display->type==10?display->get("Name"):nullptr;
    if(blank(name)) {
        if(oldName&&oldName->type==8){rename=1;work=wrapped(static_cast<std::int64_t>(work)+1);root.compound["display"].compound.erase("Name");if(root.compound["display"].compound.empty())root.compound.erase("display");}
    } else {
        const auto encoded=nbt::modifiedUtf8(name);
        if(!oldName||oldName->type!=8||oldName->string!=encoded){rename=1;work=wrapped(static_cast<std::int64_t>(work)+1);auto& changed=root.compound["display"];if(changed.type!=10){changed=nbt::Tag{};changed.type=10;}changed.compound["Name"]=nbt::text(encoded);}
    }
    result.cost=wrapped(static_cast<std::int64_t>(base)+work);
    if(rename==work&&rename>0&&result.cost>=40)result.cost=39;
    if(work<=0||(result.cost>=40&&!creative))return result;
    const int previousCost=std::max(repairCost(root),second.empty()?0:repairCost(otherRoot));
    root.compound["RepairCost"]=nbt::integer(3,wrapped(static_cast<std::int64_t>(previousCost)*2+1));setEnchantments(root,output.id,enchantMap);
    output.nbt=nbt::encode(root);result.output=std::move(output);return result;
}
void refreshAnvil(Game& game) {
    if(!game.window.open||game.window.type!="minecraft:anvil"||game.window.slots.size()<3)return;
    const auto result=anvilResult(game.window.slots[0],game.window.slots[1],game.window.repairName,game.creativeMode);
    game.window.slots[2]=result.output;game.window.properties[0]=result.cost;
    // ContainerRepair only resets materialCost inside its nonempty-input path.
    if(!game.window.slots[0].empty())game.window.repairMaterialCost=result.materialCost;
}
bool enchantable(const ItemStack& item) {
    if(item.empty())return false;
    const auto& info=registry::item(item.id);
    if(!(item.id==340?item.count==1:(info.maxStack==1&&info.maxDamage>0)))return false;
    const auto root=nbt::parse(item.nbt);const auto* ench=root.get("ench");return !ench||ench->type!=9;
}
void refreshEnchantment(Game& game) {
    if(!game.window.open||game.window.type!="minecraft:enchanting_table"||game.window.slots.empty()||enchantable(game.window.slots[0]))return;
    for(int i=0;i<3;++i){game.window.properties[i]=0;game.window.properties[i+4]=-1;}
}
std::size_t ordinaryRecipeCount() { return data::ordinaryCount; }
std::size_t specialRecipeCount() { return data::specialCount; }

} // namespace mc::crafting
