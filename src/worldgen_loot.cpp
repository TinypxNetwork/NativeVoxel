#include "mc/worldgen_loot.hpp"
#include "mc/worldgen_loot_data.hpp"
#include "mc/registry.hpp"
#include "mc/crafting_data.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace mc::worldgen {
namespace {
nbt::Tag compound(){nbt::Tag result;result.type=10;return result;}
nbt::Tag item(int id,int count=1){auto tag=compound();tag.compound["id"]=nbt::text(registry::item(id).name);tag.compound["Count"]=nbt::integer(1,count);tag.compound["Damage"]=nbt::integer(2,0);return tag;}
struct Candidate {int id,level,weight;};
Candidate weighted(JavaRandom& random,const std::vector<Candidate>& candidates){
    int total=0;for(const auto& candidate:candidates)total+=candidate.weight;int choice=random.nextInt(total);
    for(const auto& candidate:candidates){choice-=candidate.weight;if(choice<0)return candidate;}
    throw std::logic_error("invalid enchantment weight total");
}
nbt::Tag tile(const char* id,int x,int y,int z){auto result=compound();result.compound["id"]=nbt::text(id);result.compound["x"]=nbt::integer(3,x);result.compound["y"]=nbt::integer(3,y);result.compound["z"]=nbt::integer(3,z);return result;}
}
nbt::Tag makeEnchantedBook(JavaRandom& random,int power){
    if(power<0||power>32767)throw std::invalid_argument("unsupported book enchantment power");
    const int enchantability=lootdata::bookEnchantability/2,bound=(enchantability>>1)+1;
    const int first=random.nextInt(bound),second=random.nextInt(bound),base=1+first+second+power;
    const float f0=random.nextFloat(),f1=random.nextFloat(),spread=(f0+f1-1.0f)*.15f;
    const int effective=std::max(1,int(float(base)*(1.0f+spread)+.5f));std::vector<Candidate> candidates;
    for(const auto& rule:lootdata::bookRules){int selected=0;for(const auto& level:rule.levels)if(level.level&&effective>=level.minimum&&effective<=level.maximum)selected=level.level;
        if(selected)candidates.push_back({rule.id,selected,crafting::data::enchantments[rule.id].weight});
    }
    // Java 8+ HashMap<Integer,...>: capacity starts at 16, grows at 0.75,
    // and buckets preserve insertion order. IDs here are below 256, so the
    // hash spread leaves them unchanged; no bucket reaches treeification size.
    int capacity=16;while(int(candidates.size())>capacity*3/4)capacity*=2;
    std::stable_sort(candidates.begin(),candidates.end(),[&](const Candidate& a,const Candidate& b){return (a.id&(capacity-1))<(b.id&(capacity-1));});
    std::vector<Candidate> chosen;
    if(!candidates.empty()){
        chosen.push_back(weighted(random,candidates));
        for(int remaining=effective;random.nextInt(50)<=remaining;remaining>>=1){
            candidates.erase(std::remove_if(candidates.begin(),candidates.end(),[&](const Candidate& candidate){
                for(const auto& previous:chosen){const auto& rule=crafting::data::enchantments[previous.id];if((rule.compatible[candidate.id>>6]&(1ULL<<(candidate.id&63)))==0)return true;}return false;
            }),candidates.end());
            if(!candidates.empty())chosen.push_back(weighted(random,candidates));
        }
    }
    auto result=item(403);
    if(!chosen.empty()){
        auto extra=compound();nbt::Tag list;list.type=9;list.subtype=10;
        for(const auto& selected:chosen){auto entry=compound();entry.compound["id"]=nbt::integer(2,selected.id);entry.compound["lvl"]=nbt::integer(2,selected.level);list.list.push_back(std::move(entry));}
        extra.compound["StoredEnchantments"]=std::move(list);result.compound["tag"]=std::move(extra);
    }
    return result;
}
nbt::Tag makeDungeonChest(JavaRandom& random,int x,int y,int z){
    const auto book=makeEnchantedBook(random);
    struct Loot {int id,maximum,weight;};static constexpr Loot content[]={{329,1,10},{265,4,10},{297,1,10},{296,4,10},{289,4,10},{287,4,10},{325,1,10},{322,1,1},{331,4,10},{2256,1,4},{2257,1,4},{421,1,10},{418,1,2},{417,1,5},{419,1,1},{403,1,1}};
    std::array<nbt::Tag,27> slots{};
    for(int roll=0;roll<8;++roll){int value=random.nextInt(108);const Loot* choice=nullptr;for(const auto& entry:content){value-=entry.weight;if(value<0){choice=&entry;break;}}
        const int count=1+random.nextInt(choice->maximum);auto stack=choice->id==403?book:item(choice->id);stack.compound["Count"]=nbt::integer(1,count);
        const int slot=random.nextInt(27);stack.compound["Slot"]=nbt::integer(1,slot);slots[slot]=std::move(stack);
    }
    auto result=tile("Chest",x,y,z);result.compound["Lock"]=nbt::text("");nbt::Tag contents;contents.type=9;
    for(auto& stack:slots)if(stack.type){contents.subtype=10;contents.list.push_back(std::move(stack));}
    result.compound["Items"]=std::move(contents);return result;
}
nbt::Tag makeDungeonSpawner(JavaRandom& random,int x,int y,int z){
    static constexpr const char* types[]={"Skeleton","Zombie","Zombie","Spider"};const char* type=types[random.nextInt(4)];auto result=tile("MobSpawner",x,y,z);
    result.compound["EntityId"]=nbt::text(type);result.compound["Delay"]=nbt::integer(2,20);result.compound["MinSpawnDelay"]=nbt::integer(2,200);result.compound["MaxSpawnDelay"]=nbt::integer(2,800);
    result.compound["SpawnCount"]=nbt::integer(2,4);result.compound["MaxNearbyEntities"]=nbt::integer(2,6);result.compound["RequiredPlayerRange"]=nbt::integer(2,16);result.compound["SpawnRange"]=nbt::integer(2,4);return result;
}
}
