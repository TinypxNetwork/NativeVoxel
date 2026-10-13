#include "mc/drop_entities.hpp"
#include "mc/nbt.hpp"

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace mc;
using drop_entities::State;
namespace {
int checks{};
void require(bool test,const char* why){++checks;if(!test)throw std::runtime_error(why);}
ItemStack stack(int id,int count=1,int damage=0){ItemStack item;item.id=static_cast<std::int16_t>(id);item.count=static_cast<std::int8_t>(count);item.damage=static_cast<std::int16_t>(damage);return item;}
void tag(ItemStack& item,int kind){if(!kind)return;nbt::Tag root;root.type=10;if(kind>1)root.compound["marker"]=nbt::integer(kind==3?1:3,kind==4?2:1);item.nbt=nbt::encode(root);}
std::uint64_t bits(double value){std::uint64_t result;std::memcpy(&result,&value,8);return result;}
std::uint32_t bits(float value){std::uint32_t result;std::memcpy(&result,&value,4);return result;}
void printStack(const ItemStack& item){if(item.empty())std::cout<<" -1 0 0";else std::cout<<' '<<item.id<<' '<<int(item.count)<<' '<<item.damage;}
void tests(){
    World world;for(int x=-16;x<=16;x+=16)for(int z=-16;z<=16;z+=16){world.setBlock(x,0,z,blockState(1));world.setBlock(x,0,z,0);}
    State entity;entity.item=stack(4);entity.setPosition(.5,64,.5);entity.pickupDelay=10;drop_entities::tick(entity,world);
    require(entity.y==64-.03999999910593033&&entity.motionY==-.03999999910593033*.9800000190734863,"item gravity and drag follow Java doubles");
    require(entity.pickupDelay==9&&entity.age==1&&entity.ticks==1,"item countdown and age advance once");
    entity.pickupDelay=32767;entity.age=-32768;drop_entities::tick(entity,world);require(entity.pickupDelay==32767&&entity.age==-32768,"item infinite pickup and age sentinels");
    entity.age=5999;drop_entities::tick(entity,world);require(entity.dead,"item expires at six thousand ticks");
    State first,second;first.item=stack(4,3);second.item=stack(4,7);first.age=99;second.age=100;first.pickupDelay=8;second.pickupDelay=2;
    require(drop_entities::combine(first,second)&&first.dead&&second.item.count==10&&second.age==99&&second.pickupDelay==8,"merge keeps larger stack with earliest age and latest pickup");
    first=State{};first.item=stack(35,1,1);second=State{};second.item=stack(35,1,2);require(!drop_entities::combine(first,second),"colored item metadata prevents merge");
    second.item=stack(35,64,1);require(!drop_entities::combine(first,second),"merge cannot exceed maximum stack");
    Game game;auto incoming=stack(4,65);require(drop_entities::insertInventory(game.inventory,incoming,false)&&game.inventory[36].count==64&&game.inventory[37].count==1&&incoming.count==0,"pickup fills hotbar first and splits full stacks");
    for(int i=9;i<45;++i)game.inventory[static_cast<std::size_t>(i)]=stack(1,64);
    game.inventory[36]=stack(4,63);incoming=stack(4,3);
    require(!drop_entities::insertInventory(game.inventory,incoming,false)&&game.inventory[36].count==64&&incoming.count==2,"vanilla partial insertion may return false after mutating inventory");
    incoming=stack(4,3);require(drop_entities::insertInventory(game.inventory,incoming,true)&&incoming.count==0,"creative full inventory consumes picked stack");
    game=Game{};entity=State{};entity.item=stack(4);entity.owner="Alice";int cooldown=0;
    require(!drop_entities::collect(entity,game,cooldown,"Bob"),"owner protects fresh dropped item");entity.age=5800;
    require(drop_entities::collect(entity,game,cooldown,"Bob")&&entity.dead&&game.inventory[36].id==4,"owner protection expires with two hundred ticks left");
    entity=State{};entity.orb=true;entity.value=7;require(drop_entities::collect(entity,game,cooldown)&&game.experienceLevel==1&&game.totalExperience==7&&cooldown==2,"XP pickup uses vanilla level capacity and player cooldown");
    State next;next.orb=true;next.value=3;require(!drop_entities::collect(next,game,cooldown)&&!next.dead,"second XP orb waits for pickup cooldown");
    game.online=true;bool rejected=false;try{drop_entities::collect(next,game,cooldown);}catch(const std::logic_error&){rejected=true;}require(rejected,"offline pickup rejects online state");
}
void printState(const State& value){std::cout<<"E "<<value.dead<<' '<<value.onGround<<' '<<value.noClip<<' '<<value.inWeb<<' '<<value.inWater<<' '<<value.age<<' '<<value.pickupDelay<<' '<<value.health<<' '<<value.fire<<' '<<value.ticks<<' '<<value.color<<' '<<value.targetColor<<' '<<value.hasTarget;
    for(double v:{value.x,value.y,value.z,value.motionX,value.motionY,value.motionZ,value.bounds.minX,value.bounds.minY,value.bounds.minZ,value.bounds.maxX,value.bounds.maxY,value.bounds.maxZ})std::cout<<' '<<bits(v);
    std::cout<<' '<<bits(value.fallDistance)<<' '<<value.random.internalSeed()<<'\n';
}
int trace(const char* path){std::ifstream file(path);if(!file)throw std::runtime_error("cannot open item entity fixture");World world;State entity;Player player;bool attract=false,rain=false;Game game;
    for(std::string line;std::getline(file,line);){if(line.empty()||line[0]=='#')continue;std::istringstream in(line);char mode;in>>mode;
        if(mode=='W'){world=World{};attract=rain=false;for(int x=-32;x<=32;x+=16)for(int z=-32;z<=32;z+=16){world.setBlock(x,0,z,blockState(1));world.setBlock(x,0,z,0);}}
        else if(mode=='B'){int x,y,z,state;in>>x>>y>>z>>state;world.setBlock(x,y,z,static_cast<BlockState>(state));}
        else if(mode=='S'){entity=State{};int orb,first,wet,web,ground;std::int64_t seed;double x,y,z;in>>orb>>entity.id>>x>>y>>z>>entity.motionX>>entity.motionY>>entity.motionZ>>seed>>entity.age>>entity.pickupDelay>>entity.health>>entity.fire>>first>>wet>>web>>ground;entity.orb=orb!=0;entity.item=stack(4);entity.value=3;entity.width=entity.height=entity.orb?.5f:.25f;entity.setPosition(x,y,z);entity.random.setSeed(seed);entity.firstUpdate=first!=0;entity.inWater=wet!=0;entity.inWeb=web!=0;entity.onGround=ground!=0;}
        else if(mode=='P'){int enabled,spectator;double x,y,z;in>>enabled>>x>>y>>z>>spectator;attract=enabled!=0;player.teleport(x,y,z);player.spectator=spectator!=0;}
        else if(mode=='R'){in>>rain;}
        else if(mode=='T'){int count;in>>count;for(int i=0;i<count;++i){drop_entities::tick(entity,world,attract?&player:nullptr,rain);printState(entity);}}
        else if(mode=='I'){int fill,id,count,damage,creative;in>>fill>>id>>count>>damage>>creative;game=Game{};if(fill>=0)for(int i=9;i<45;++i)game.inventory[static_cast<std::size_t>(i)]=stack(fill,64);auto incoming=stack(id,count,damage);int slots;in>>slots;for(int i=0;i<slots;++i){int slot,item,amount,meta;in>>slot>>item>>amount>>meta;game.inventory[static_cast<std::size_t>(slot<9?36+slot:slot)]=stack(item,amount,meta);}const bool accepted=drop_entities::insertInventory(game.inventory,incoming,creative!=0);std::cout<<"I "<<accepted;printStack(incoming);for(int i=0;i<36;++i)printStack(game.inventory[static_cast<std::size_t>(i<9?36+i:i)]);std::cout<<'\n';}
        else if(mode=='X'){int level,total,amount;float progress;in>>level>>total>>progress>>amount;game=Game{};game.experienceLevel=level;game.totalExperience=total;game.experienceProgress=progress;drop_entities::addExperience(game,amount);std::cout<<"X "<<game.experienceLevel<<' '<<game.totalExperience<<' '<<bits(game.experienceProgress)<<'\n';}
        else if(mode=='M'){State first,second;for(State* value:{&first,&second}){int id,count,damage,nbt;in>>id>>count>>damage>>value->age>>value->pickupDelay>>nbt;value->item=stack(id,count,damage);tag(value->item,nbt);}const bool merged=drop_entities::combine(first,second);std::cout<<"M "<<merged;for(const State* value:{&first,&second}){std::cout<<' '<<value->dead<<' '<<value->age<<' '<<value->pickupDelay;printStack(value->item);}std::cout<<'\n';}
        else if(mode=='V'){int around,traced,sneaking;float yaw,pitch;std::int64_t seed;in>>around>>traced>>sneaking>>yaw>>pitch>>seed;State thrown;Player source;source.teleport(1.25,64,-2.5);source.sneaking=sneaking!=0;source.yaw=yaw;source.pitch=pitch;worldgen::JavaRandom random(seed);drop_entities::throwFromPlayer(thrown,source,around!=0,traced!=0,"NativeDrops",random);std::cout<<"V "<<thrown.pickupDelay<<' '<<!thrown.thrower.empty();for(double v:{thrown.x,thrown.y,thrown.z,thrown.motionX,thrown.motionY,thrown.motionZ})std::cout<<' '<<bits(v);std::cout<<' '<<random.internalSeed()<<'\n';}
    }return 0;
}
}
int main(int argc,char** argv){try{if(argc==3&&std::string(argv[1])=="--trace")return trace(argv[2]);tests();std::cout<<"drop entity tests passed ("<<checks<<" checks)\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
