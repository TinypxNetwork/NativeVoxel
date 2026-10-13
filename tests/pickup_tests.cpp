#include "mc/pickup.hpp"
#include "mc/nbt.hpp"
#include "mc/simulation.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace mc;
namespace {
std::size_t checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
void near(double actual,double expected,const char* message){require(std::abs(actual-expected)<1e-10,message);}
ItemStack item(int id,int count=1,int damage=0){return {static_cast<std::int16_t>(id),static_cast<std::int8_t>(count),static_cast<std::int16_t>(damage),{0}};}
Entity dropped(int id=5){
    Entity value;value.id=id;value.type=2;value.x=10;value.y=64;value.z=-4;
    value.previousX=-100;value.previousY=12;value.previousZ=-30;
    value.ageTicks=83;value.hoverStart=.625f;value.yaw=97;value.uuid="original-skin";
    auto stack=item(397,32,3);nbt::Tag tag;tag.type=10;tag.compound["SkullOwner"]=nbt::text("Alex");stack.nbt=nbt::encode(tag);
    value.equipment[0]=stack;
    net::Writer bytes;Game::writeItem(bytes,stack);value.metadata[10].type=5;value.metadata[10].raw=bytes.data;
    return value;
}
ItemStack entityItem(const Entity& entity){net::Reader bytes(entity.metadata.at(10).raw);return Game::readItem(bytes);}
void collectPacket(Game& game,int id,int collector){game.consumePacket({0x0d,net::Writer().varInt(id).varInt(collector).data});}

void packetTests(){
    Game game;game.entityId=42;game.player.teleport(0,64,0);game.entities[5]=dropped();
    Entity collector;collector.id=7;collector.kind=EntityKind::Player;collector.x=2;collector.y=66;collector.z=4;collector.previousY=64;
    game.entities[7]=collector;
    collectPacket(game,5,7);
    require(game.entities.count(5)==0&&game.entities.count(7)==1,"S0D removes only the collected entity immediately");
    require(game.pickupEvents.size()==1&&game.pickupEvents.front().collectorId==7,"S0D keeps collector ID and source snapshot");
    const auto original=entityItem(game.pickupEvents.front().entity);
    require(original.id==397&&original.count==32&&original.damage==3&&nbt::parse(original.nbt).get("SkullOwner")->string=="Alex","custom skull NBT, metadata and stack count survive collection");
    require(game.pickupEvents.front().entity.uuid=="original-skin"&&game.pickupEvents.front().entity.equipment[0].nbt==original.nbt,"snapshot preserves skins and equipment NBT");
    require(game.soundEvents.size()==1&&game.soundEvents.front().name=="random.pop"&&game.soundEvents.front().volume==.2f,"S0D item plays original pop sound");
    require(game.soundEvents.front().pitch>=.6f&&game.soundEvents.front().pitch<=3.4f,"pickup pitch stays in vanilla random range");
    collectPacket(game,5,7);require(game.pickupEvents.size()==1&&game.soundEvents.size()==1,"duplicate or unknown S0D has no extra effect or sound");
    Entity orb;orb.id=9;orb.kind=EntityKind::ExperienceOrb;orb.type=orb.xpValue=73;orb.ageTicks=127;game.entities[9]=orb;
    collectPacket(game,9,999);
    require(game.pickupEvents.back().collectorId==42&&game.pickupEvents.back().entity.xpValue==73,"missing collector falls back to local player and retains XP value");
    require(game.soundEvents.back().name=="random.orb","S0D experience uses orb sound");
    game.entities[10]=dropped(10);bool rejected=false;
    try{game.consumePacket({0x0d,net::Writer().varInt(10).varInt(7).u8(0).data});}catch(const net::ProtocolError&){rejected=true;}
    require(rejected&&game.entities.count(10)==1&&game.pickupEvents.size()==2,"malformed S0D is rejected before any pickup side effect");
    game.disconnect();require(game.pickupEvents.empty(),"disconnect clears unconsumed pickup events");
}

void animationTests(){
    Game game;game.entityId=42;game.player.teleport(0,64,0);game.entities[5]=dropped();
    collectPacket(game,5,42);PickupAnimationTracker tracker;tracker.update(game);
    auto frame=tracker.frame(0);require(frame.size()==1&&game.pickupEvents.empty(),"tracker consumes the event without restoring a world entity");
    near(frame[0].x,10,"animation begins at source current position, not previous tick position");
    near(frame[0].y,64,"initial pickup Y");near(frame[0].z,-4,"initial pickup Z");
    game.player.x=2;game.player.y=66;game.player.z=4;++game.ticks;tracker.update(game);tracker.update(game);
    frame=tracker.frame(.5f);
    near(frame[0].x,7.75,"age 1.5 uses squared progress and interpolated moving target X");
    near(frame[0].y,64.375,"pickup aims at interpolated feet plus 0.5 blocks");
    near(frame[0].z,-2.5,"moving target Z and duplicate update do not change age");
    require(frame[0].entity->ageTicks==83&&frame[0].entity->hoverStart==.625f&&entityItem(*frame[0].entity).count==32,"snapshot age, item bobbing and stack copies stay frozen");
    game.entities[5]=dropped();game.entities[5].x=1000;game.entities[5].metadata.clear();tracker.update(game);
    require(tracker.frame(.5f)[0].entity->x==10&&!tracker.frame(.5f)[0].entity->metadata.empty(),"reused world ID cannot mutate the collected snapshot");
    ++game.ticks;tracker.update(game);frame=tracker.frame(1);
    near(frame[0].x,2,"third tick end reaches collector X");near(frame[0].y,66.5,"third tick end reaches collector feet offset");near(frame[0].z,4,"third tick end reaches collector Z");
    ++game.ticks;tracker.update(game);require(tracker.size()==0&&tracker.frame(0).empty(),"effect expires on the third onUpdate");
    game.queuePickup(5,42);game.entities.erase(5);game.ticks+=10;tracker.update(game);require(tracker.size()==0,"old queued events expire during catch-up instead of replaying late");

    game.entities[5]=dropped();Entity collector;collector.id=7;collector.kind=EntityKind::Mob;collector.x=collector.previousX=1;collector.y=collector.previousY=64;
    game.entities[7]=collector;collectPacket(game,5,7);tracker.update(game);
    game.entities[7].previousX=1;game.entities[7].x=3;++game.ticks;tracker.update(game);
    near(tracker.frame(.5f)[0].x,8,"remote living collector is followed as it moves");
    game.entities.erase(7);tracker.update(game);game.entities[7]=collector;game.entities[7].x=1000;tracker.update(game);
    near(tracker.frame(.5f)[0].x,8,"removed collector retains its last pose even if its ID is reused");
    ++game.pickupGeneration;tracker.update(game);require(tracker.size()==0,"new world generation clears active effects");
    game.entities[5]=dropped();collectPacket(game,5,42);tracker.update(game);require(tracker.size()==1,"new pickup after reset");
    game.dimension=-1;tracker.update(game);require(tracker.size()==0,"dimension change clears active effects");
    game.entities[5]=dropped();collectPacket(game,5,42);tracker.update(game);game.ticks=0;tracker.update(game);require(tracker.size()==0,"rewinding game tick clears previous session effects");
}

void capacityTests(){
    Game game;game.entityId=42;
    for(int i=0;i<4005;++i){Entity source;source.id=100+i;game.entities[source.id]=source;game.queuePickup(source.id,42);game.entities.erase(source.id);}
    require(game.pickupEvents.size()==4000&&game.pickupEvents.front().entity.id==105,"pending effects retain the newest 4000 snapshots");
    PickupAnimationTracker tracker;tracker.update(game);require(tracker.size()==4000,"tracker accepts the full original particle-layer limit");
    for(int i=0;i<5;++i){Entity source;source.id=5000+i;game.entities[source.id]=source;game.queuePickup(source.id,42);}
    tracker.update(game);require(tracker.size()==4000&&tracker.frame(0).front().entity->id==110,"active layer evicts the oldest effects");
}

void floor(Game& game){for(int x=-3;x<=3;++x)for(int z=-3;z<=3;++z)game.world.setBlock(x,63,z,blockState(1));game.player.teleport(.5,64,.5);game.player.onGround=true;}
void simulationTests(){
    Game game;floor(game);Simulation simulation;simulation.start(game,189);
    auto stack=item(35,32,7);nbt::Tag tag;tag.type=10;tag.compound["custom"]=nbt::text("preserve");stack.nbt=nbt::encode(tag);
    const int id=simulation.spawnItem(game,stack,.5,64,.5,0);const float hover=game.entities.at(id).hoverStart;
    simulation.tick(game,{});
    require(game.entities.count(id)==0&&simulation.droppedEntities().count(id)==0,"local collection still removes authoritative item immediately");
    require(game.inventory[36].count==32&&game.inventory[36].nbt==stack.nbt,"pickup visual does not change inventory insertion");
    require(game.pickupEvents.size()==1&&game.pickupEvents.front().entity.id==id,"local item pickup queues the same client animation as S0D");
    require(entityItem(game.pickupEvents.front().entity).count==32&&entityItem(game.pickupEvents.front().entity).nbt==stack.nbt&&game.pickupEvents.front().entity.hoverStart==hover,"local snapshot is taken before publishing the consumed stack");
    PickupAnimationTracker tracker;tracker.update(game);require(tracker.size()==1,"local pickup renders after removal");
    const int xp=simulation.spawnExperience(game,7,.5,64,.5);simulation.tick(game,{});
    require(game.entities.count(xp)==0&&game.totalExperience==7&&game.pickupEvents.size()==1&&game.pickupEvents.front().entity.xpValue==7,"XP pickup has animation and retains exact authoritative experience");

    Game partial;floor(partial);Simulation partialSimulation;partialSimulation.start(partial,189);
    for(int i=9;i<45;++i)partial.inventory[static_cast<std::size_t>(i)]=item(1,64);
    partial.inventory[36]=item(4,63);const int remainder=partialSimulation.spawnItem(partial,item(4,3),.5,64,.5,0);partialSimulation.tick(partial,{});
    require(partial.inventory[36].count==64&&partialSimulation.droppedEntities().at(remainder).item.count==2,"partial inventory transfer retains uncollected items");
    require(partial.pickupEvents.empty(),"partial transfer follows Java false return: no full pickup effect");
    partial.inventory[37]={};partialSimulation.tick(partial,{});
    require(partial.entities.count(remainder)==0&&partial.pickupEvents.size()==1&&entityItem(partial.pickupEvents.front().entity).count==2,"remaining stack animates only when it is actually collected");
}

std::uint32_t bits(float value){std::uint32_t out;std::memcpy(&out,&value,4);return out;}
void trace(const char* path){
    std::ifstream input(path);if(!input)throw std::runtime_error("cannot read pickup reference fixture");
    for(std::string line;std::getline(input,line);){
        if(line.empty()||line[0]=='#')continue;
        std::istringstream fields(line);Game game;game.entityId=42;Entity source;source.id=5;Entity target;target.id=7;
        int age;float partial;double originX,originY,originZ;
        fields>>age>>partial>>source.x>>source.y>>source.z>>target.previousX>>target.previousY>>target.previousZ>>target.x>>target.y>>target.z>>source.yaw>>originX>>originY>>originZ;
        if(!fields)throw std::runtime_error("invalid pickup fixture");
        game.entities[5]=source;game.entities[7]=target;game.queuePickup(5,7);game.entities.erase(5);
        PickupAnimationTracker tracker;tracker.update(game);game.ticks=static_cast<std::uint64_t>(age);tracker.update(game);const auto frame=tracker.frame(partial);
        if(frame.empty())std::cout<<"dead\n";
        else std::cout<<"live "<<bits(static_cast<float>(frame[0].x-originX))<<' '<<bits(static_cast<float>(frame[0].y-originY))<<' '<<bits(static_cast<float>(frame[0].z-originZ))<<' '<<bits(frame[0].entity->yaw)<<'\n';
    }
}
}
int main(int argc,char** argv){try{if(argc==3&&std::string(argv[1])=="--trace"){trace(argv[2]);return 0;}packetTests();animationTests();capacityTests();simulationTests();std::cout<<"Pickup animations: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
