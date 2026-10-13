#include "mc/drops.hpp"
#include "mc/nbt.hpp"
#include "mc/registry.hpp"

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace mc;
namespace {
int checks{};
void require(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
ItemStack item(int id,int count=1,int damage=0,int silk=0,int fortune=0,int unbreaking=0,bool immutable=false){
    ItemStack value;value.id=static_cast<std::int16_t>(id);value.count=static_cast<std::int8_t>(count);value.damage=static_cast<std::int16_t>(damage);
    if(silk||fortune||unbreaking||immutable){nbt::Tag root;root.type=10;nbt::Tag list;list.type=9;list.subtype=10;
        for(const auto& pair:{std::pair<int,int>{33,silk},{35,fortune},{34,unbreaking}})if(pair.second){nbt::Tag tag;tag.type=10;tag.compound["id"]=nbt::integer(2,pair.first);tag.compound["lvl"]=nbt::integer(2,pair.second);list.list.push_back(tag);}
        root.compound["ench"]=std::move(list);if(immutable)root.compound["Unbreakable"]=nbt::integer(1,1);value.nbt=nbt::encode(root);
    }return value;
}
std::uint64_t bits(double value){std::uint64_t result;std::memcpy(&result,&value,8);return result;}
std::uint32_t bits(float value){std::uint32_t result;std::memcpy(&result,&value,4);return result;}
void printItem(const ItemStack& value){if(value.empty())std::cout<<" -1 0 0";else std::cout<<' '<<value.id<<' '<<int(value.count)<<' '<<value.damage;}
void tests(){
    drops::Random random(189),player(100);drops::Context context;context.state=blockState(1);
    auto stone=drops::harvest(context,item(278),random);require(stone.supported&&stone.items.size()==1&&stone.items[0].item.id==4&&stone.exhaustion==.025f,"stone yields cobblestone with exhaustion");
    context.state=blockState(56);random.setSeed(189);auto silk=drops::harvest(context,item(278,1,0,1),random);require(silk.items.size()==1&&silk.items[0].item.id==56&&silk.experience.empty(),"silk suppresses ore XP and normal drops");
    require(!drops::canHarvest(context.state,item(270))&&drops::canHarvest(context.state,item(257)),"harvest eligibility respects tool tier");
    auto tool=item(278,1,1561,1);const bool eligible=drops::canHarvest(context.state,tool);auto wear=drops::damageOnBreak(context.state,tool,false,random,player);auto last=drops::harvest(context,tool,random);
    require(eligible&&wear.broken&&tool.empty()&&last.items[0].item.id==264,"last durability loses silk after eligibility capture");
    context.state=blockState(18,3);auto leaves=drops::harvest(context,item(359),random);require(leaves.items.size()==1&&leaves.items[0].item.id==18&&leaves.items[0].item.damage==3&&leaves.exhaustion==0,"sheared leaves retain species and bypass exhaustion");
    context.state=blockState(31,2);auto fern=drops::harvest(context,item(359),random);require(fern.items.size()==1&&fern.items[0].item.damage==2&&fern.exhaustion==0,"sheared fern metadata");
    context.state=blockState(78,7);auto snow=drops::harvest(context,item(277,1,0,1),random);require(snow.items.size()==1&&snow.items[0].item.id==332&&snow.items[0].item.count==9&&snow.exhaustion==0,"original snow layer harvest uses LAYERS plus one, ignores silk");
    context.state=blockState(79);context.supportedBelow=true;auto ice=drops::harvest(context,item(278),random);require(ice.items.empty()&&ice.replacement==blockState(8),"ice becomes flowing water over supporting material");
    context.waterVaporizes=true;ice=drops::harvest(context,item(278),random);require(ice.replacement==0&&ice.items.empty(),"ice vaporizes in Nether");context.waterVaporizes=false;
    context.state=blockState(56);context.tileDrops=false;auto noItems=drops::harvest(context,item(278),random);require(noItems.items.empty()&&!noItems.experience.empty(),"doTileDrops suppresses items but not XP in 1.8.9");
    context.tileDrops=true;context.state=blockState(144);auto skull=drops::harvest(context,item(278),random);require(!skull.supported&&skull.items.empty(),"tile-NBT-dependent skull drop is explicitly unsupported");
    context.state=blockState(13);for(int i=0;i<16;++i){auto gravel=drops::dropWithChance(context,3,1,random);require(gravel.items.size()==1&&gravel.items[0].item.id==318,"fortune three gravel always flint");}
    tool=item(278,1,1,0,0,3,true);wear=drops::damageOnBreak(blockState(1),tool,false,random,player);require(tool.damage==1&&wear.amount==0,"Unbreakable tag suppresses durability draws");
    tool=item(359);wear=drops::damageOnBreak(blockState(1),tool,false,random,player);require(tool.damage==0,"shears do not wear against ordinary stone");
    require(drops::experienceSplit(2477)==2477&&drops::experienceSplit(8)==7&&drops::experienceSplit(2)==1,"XP orb split thresholds");
}
int trace(const char* path){std::ifstream file(path);if(!file)throw std::runtime_error("cannot open drop fixture");
    for(std::string line;std::getline(file,line);){if(line.empty()||line[0]=='#')continue;std::istringstream in(line);char mode;int encoded,id,count,damage,silk,fortune,unbreaking,immutable,tileDrops,nether,below,creative;std::int64_t worldSeed,playerSeed;float chance;
        in>>mode>>encoded>>id>>count>>damage>>silk>>fortune>>unbreaking>>immutable>>worldSeed>>playerSeed>>tileDrops>>nether>>below>>creative>>chance;
        if(!in)throw std::runtime_error("invalid drop fixture");
        drops::Random worldRandom(worldSeed),playerRandom(playerSeed);auto held=item(id,count,damage,silk,fortune,unbreaking,immutable!=0);drops::Context context;context.state=static_cast<BlockState>(encoded);context.tileDrops=tileDrops!=0;context.waterVaporizes=nether!=0;context.supportedBelow=below!=0;
        const bool eligible=drops::canHarvest(context.state,held);drops::Result result;result.supported=true;
        if(mode=='D')result=drops::dropWithChance(context,fortune,chance,worldRandom);
        else if(mode=='H')result=drops::harvest(context,held,worldRandom);
        else if(mode=='B'&&!creative){drops::damageOnBreak(context.state,held,false,worldRandom,playerRandom);if(eligible)result=drops::harvest(context,held,worldRandom);}
        else if(mode!='B')throw std::runtime_error("unknown fixture mode");
        if(!result.supported)throw std::runtime_error("unsupported fixture drop state "+std::to_string(encoded));
        if(creative)result.exhaustion=0;
        std::cout<<"R "<<eligible;printItem(held);std::cout<<' '<<bits(result.exhaustion)<<' '<<result.replacement<<' '<<result.items.size();
        for(const auto& drop:result.items){printItem(drop.item);std::cout<<' '<<bits(drop.x)<<' '<<bits(drop.y)<<' '<<bits(drop.z);}
        std::cout<<' '<<result.experience.size();for(int amount:result.experience)std::cout<<' '<<amount;
        const auto w1=static_cast<std::uint32_t>(worldRandom.nextInt()),w2=static_cast<std::uint32_t>(worldRandom.nextInt()),p1=static_cast<std::uint32_t>(playerRandom.nextInt()),p2=static_cast<std::uint32_t>(playerRandom.nextInt());
        std::cout<<' '<<w1<<' '<<w2<<' '<<p1<<' '<<p2<<'\n';
    }return 0;
}
}
int main(int argc,char** argv){try{if(argc==3&&std::string(argv[1])=="--trace")return trace(argv[2]);tests();std::cout<<"drop tests passed ("<<checks<<" checks)\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
