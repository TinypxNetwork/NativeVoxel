#include "mc/simulation.hpp"
#include "mc/crafting.hpp"
#include "mc/brewing.hpp"
#include "mc/controller.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace mc;
namespace {
std::size_t checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
ItemStack item(int id,int count=1,int damage=0){ItemStack value;value.id=static_cast<std::int16_t>(id);value.count=static_cast<std::int8_t>(count);value.damage=static_cast<std::int16_t>(damage);return value;}
void foodTests(){Game game;Simulation sim;game.food=20;game.saturation=5;sim.food.exhaustion=4;sim.tickFood(game);require(game.saturation==5&&sim.food.exhaustion==4,"exhaustion threshold is strictly greater than four");
    sim.food.exhaustion=9;sim.tickFood(game);require(sim.food.exhaustion==5&&game.saturation==4,"one exhaustion step per tick");sim.tickFood(game);require(sim.food.exhaustion==1&&game.saturation==3,"remaining exhaustion consumed next tick");
    sim.addExhaustion(100);require(sim.food.exhaustion==40,"exhaustion capped at forty");
    sim.food={};game.health=19;game.food=20;game.saturation=5;for(int i=0;i<79;++i)sim.tickFood(game);require(game.health==19&&sim.food.timer==79,"natural regeneration waits eighty ticks");sim.tickFood(game);require(game.health==20&&sim.food.timer==0&&sim.food.exhaustion==3,"natural regeneration and exhaustion cost");
    for(int difficulty=0;difficulty<4;++difficulty){Game starving;Simulation hunger;hunger.settings.difficulty=difficulty;starving.health=20;starving.food=0;starving.saturation=0;
        for(int i=0;i<1600;++i)hunger.tickFood(starving);
        const float expected=difficulty==3?0:(difficulty==2?1:10);require(starving.health==expected,"difficulty-specific starvation floor");}
    Game immune;Simulation creative;immune.invulnerable=true;immune.food=0;immune.health=20;for(int i=0;i<160;++i)creative.tickFood(immune);require(immune.health==20,"creative invulnerability prevents starvation damage");
    Game online;online.online=true;bool rejected=false;try{sim.tickFood(online);}catch(const std::logic_error&){rejected=true;}require(rejected,"simulation cannot update online player");}
void furnaceTests(){FurnaceState furnace;furnace.slots[0]=item(15,2);furnace.slots[1]=item(263);furnace.totalCookTime=200;Simulation::tickFurnace(furnace);
    require(furnace.burnTime==1600&&furnace.cookTime==1&&furnace.slots[1].empty(),"fuel ignition does not decrement the new fuel on same tick");
    for(int i=1;i<200;++i)Simulation::tickFurnace(furnace);
    require(furnace.burnTime==1401&&furnace.cookTime==0&&furnace.slots[0].count==1&&furnace.slots[2].id==265,"first smelt exact tick and inventory");
    for(int i=0;i<200;++i)Simulation::tickFurnace(furnace);
    require(furnace.slots[0].empty()&&furnace.slots[2].count==2,"second smelt consumes one input");
    FurnaceState cooling;cooling.cookTime=100;cooling.totalCookTime=200;Simulation::tickFurnace(cooling);require(cooling.cookTime==98,"empty fuel cools by two");cooling.slots[0]=item(15);cooling.slots[1]=item(1);Simulation::tickFurnace(cooling);require(cooling.cookTime==0,"invalid present fuel resets active cooking");
    FurnaceState sponge;sponge.slots[0]=item(19,1,1);sponge.slots[1]=item(327);sponge.totalCookTime=200;for(int i=0;i<200;++i)Simulation::tickFurnace(sponge);require(sponge.slots[1].id==326&&sponge.slots[2].id==19&&sponge.slots[2].damage==0,"wet sponge fills lava fuel's empty bucket");
    FurnaceState full;full.slots[0]=item(15);full.slots[1]=item(263);full.slots[2]=item(265,64);full.totalCookTime=200;Simulation::tickFurnace(full);require(full.burnTime==0&&full.slots[1].count==1,"full output does not consume fuel");}
void brewingTests(){BrewingState stand;stand.slots[0]=item(373);stand.slots[1]=item(373);stand.slots[3]=item(372,2);brewing::tick(stand);require(stand.brewTime==400&&stand.ingredientId==372,"brewing begins at four hundred ticks");
    for(int i=0;i<399;++i)brewing::tick(stand);
    require(stand.brewTime==1&&stand.slots[0].damage==0,"brewing does not finish early");brewing::tick(stand);require(stand.brewTime==0&&stand.slots[0].damage==16&&stand.slots[1].damage==16&&stand.slots[3].count==1,"brewing converts all eligible bottles and one ingredient");
    stand.slots[3]=item(353);brewing::tick(stand);require(stand.brewTime==400,"awkward potion accepts sugar");stand.slots[3]=item(370);brewing::tick(stand);require(stand.brewTime==0,"ingredient identity change cancels brewing");
    BrewingState empty;empty.slots[0]=item(374);empty.slots[3]=item(372);brewing::tick(empty);require(empty.brewTime==0&&empty.filledMask==1,"empty bottle affects block occupancy but cannot brew");}
void integrationTests(){Game game;game.world.setBlock(0,63,0,blockState(1));game.world.setBlock(2,64,0,blockState(61,2));game.player.teleport(.5,64,.5);game.player.onGround=true;Simulation sim;sim.start(game,123);
    require(sim.interactBlock(game,2,64,0)&&game.window.type=="minecraft:furnace"&&game.window.slots.size()==39,"native furnace opens without network");game.window.slots[0]=item(15);game.window.slots[1]=item(263);sim.commitContainer(game);
    for(int i=0;i<200;++i)sim.tick(game,{});
    require(game.window.slots[2].id==265&&game.world.getBlock(2,64,0)==blockState(62,2),"furnace ticks update world and visible container");require(game.ticks==200&&game.player.ticks==200&&sim.ticks()==200,"simulation owns one player step per tick");require(game.worldAge==200&&game.worldTime==200,"world time advances at twenty ticks per second");
    sim.food.exhaustion=3.25f;sim.food.timer=31;game.health=17;game.inventory[36]=item(257,1,42);sim.commitContainer(game);const auto saved=sim.saveState(game);Game loaded;Simulation restored;restored.loadState(loaded,saved);
    require(restored.running()&&restored.food.exhaustion==3.25f&&restored.food.timer==31&&loaded.health==17,"food and health persist");require(loaded.inventory[36].id==257&&loaded.inventory[36].damage==42&&loaded.worldAge==200,"inventory and time persist");require(restored.furnaces().size()==1&&restored.furnaceAt(2,64,0).slots[2].id==265,"furnace state persists");
    auto corrupt=saved;corrupt.compound["Player"].compound["SelectedItemSlot"]=nbt::integer(3,30);bool failed=false;try{restored.loadState(loaded,corrupt);}catch(const net::ProtocolError&){failed=true;}require(failed&&loaded.selectedSlot==0&&loaded.health==17,"bad save leaves current game unchanged");
    std::filesystem::path directory=std::filesystem::u8path("build/simulation_tests");std::filesystem::create_directories(directory);const auto file=directory/"state.dat";std::string error;
    require(sim.saveStateFile(file.u8string(),game,&error),"native compressed simulation save");Game disk;Simulation read;require(read.loadStateFile(file.u8string(),disk,&error)&&disk.inventory[36].damage==42,"native compressed simulation reload");
    game.world.setBlock(3,64,0,blockState(117));require(sim.interactBlock(game,3,64,0)&&game.window.type=="minecraft:brewing_stand","native brewing window opens");game.window.slots[0]=item(373);game.window.slots[3]=item(372);
    for(int i=0;i<401;++i)sim.tick(game,{});
    require(game.window.slots[0].damage==16&&game.window.slots[3].empty()&&game.world.getBlock(3,64,0)==blockState(117,1),"brewing updates live inventory and bottle block state");
    game.window.slots[3]=item(353);sim.tick(game,{});const auto brewingSave=sim.saveState(game);Game resumedGame;Simulation resumed;resumed.loadState(resumedGame,brewingSave);
    require(resumed.brewingAt(3,64,0).brewTime==400&&resumed.brewingAt(3,64,0).ingredientId==-1,"brewing persistence preserves timer and vanilla missing ingredient cache");Simulation::tickBrewing(resumed.brewingAt(3,64,0));require(resumed.brewingAt(3,64,0).brewTime==0,"loaded brewing restarts like Java on first resumed tick");
    require(sim.coverage().food&&sim.coverage().furnace&&sim.coverage().brewing&&!sim.coverage().redstone,"coverage distinguishes implemented stages");}
void bridgeTests(){Game game;game.world.setBlock(0,63,0,blockState(1));game.world.setBlock(2,64,0,blockState(61,2));game.player.teleport(.5,64,.5);Simulation sim;sim.start(game);Controller controller;int opened=0,committed=0;
    game.offlineBlockInteraction=[&](int x,int y,int z){++opened;return sim.interactBlock(game,x,y,z);};game.beforeOfflineContainerClose=[&]{++committed;sim.commitContainer(game);};
    ControllerInput input;input.target.block=true;input.target.x=2;input.target.y=64;input.target.face=1;input.target.hitY=1;input.useHeld=input.usePressed=true;controller.tick(game,input);
    require(opened==1&&game.window.open,"offline activation delegates to simulation");game.window.slots[0]=item(15);game.window.slots[1]=item(263);controller.closeWindow(game);require(committed==1&&sim.furnaceAt(2,64,0).slots[0].id==15,"close commits a furnace edit before the next tick");
    game.online=true;controller.reset();controller.tick(game,input);game.closeContainer();require(opened==1&&committed==1,"online path never calls offline hooks");
    game.online=false;game.player.sneaking=true;game.inventory[36]=item(1,2);controller.reset();controller.tick(game,input);require(opened==1,"sneaking with held item skips activation hook");
    game.inventory[36]=ItemStack{};controller.reset();controller.tick(game,input);require(opened==2,"sneaking with empty hand retains activation hook");}
void testFloor(Game& game){for(int x=-8;x<=8;++x)for(int z=-8;z<=8;++z)game.world.setBlock(x,63,z,blockState(1));game.player.teleport(.5,64,.5);game.player.onGround=true;}
void breakBridgeTests(){Game game;testFloor(game);game.world.setBlock(1,64,0,blockState(1));game.inventory[36]=item(278);Simulation sim;sim.start(game);sim.settings.weatherCycle=false;Controller controller;int called=0;
    game.offlineBlockBreak=[&](int x,int y,int z,int face){++called;return sim.breakBlock(game,x,y,z,face);};
    ControllerInput input;input.target.block=true;input.target.x=1;input.target.y=64;input.target.face=4;input.attackHeld=input.attackPressed=true;
    for(int tick=0;tick<20&&game.world.getBlock(1,64,0);++tick){controller.tick(game,input);sim.tick(game,{});input.attackPressed=false;}
    require(game.world.getBlock(1,64,0)==0&&called==1&&game.inventory[36].damage==1,"authority bridge destroys and damages exactly once");
    require(sim.droppedEntities().size()==1&&sim.droppedEntities().begin()->second.item.id==4&&sim.food.exhaustion>=.025f,"survival break creates one real cobblestone entity and exhaustion");
    const auto entityId=sim.droppedEntities().begin()->first;const auto& visible=game.entities.at(entityId);net::Reader slot(visible.metadata.at(10).raw);require(visible.kind==EntityKind::Object&&visible.type==2&&Game::readItem(slot).id==4,"native item publishes actual Slot metadata for rendering");
    for(int tick=0;tick<30;++tick)sim.tick(game,{});
    require(sim.droppedEntities().empty()&&game.inventory[37].id==4&&game.inventory[37].count==1,"dropped block follows pickup delay then enters hotbar once");
    game.world.setBlock(2,64,0,blockState(144));const auto before=nbt::encode(sim.saveState(game));require(!sim.breakBlock(game,2,64,0,1)&&game.world.getBlock(2,64,0)==blockState(144),"unsupported tile break is rejected");require(before==nbt::encode(sim.saveState(game)),"rejected break preserves tools, RNG, counters and entities");
    sim.settings.gameMode=1;sim.tick(game,{});input.target.x=2;input.attackPressed=true;controller.reset();const int callsBefore=called;controller.tick(game,input);
    require(called==callsBefore+1&&game.world.getBlock(2,64,0)==blockState(144),"false authority result has no client fallback mutation");
    game.online=true;game.world.setBlock(3,64,0,blockState(31,1));input.target.x=3;controller.reset();controller.tick(game,input);require(called==callsBefore+1&&game.world.getBlock(3,64,0)==0,"online breaking bypasses offline authority hook");game.online=false;
    game.world.setBlock(4,64,0,blockState(1));const auto damage=game.inventory[36].damage;require(sim.breakBlock(game,4,64,0,1)&&sim.droppedEntities().empty()&&game.inventory[36].damage==damage,"creative authority suppresses loot and wear");
    sim.settings.gameMode=0;sim.settings.tileDrops=false;sim.tick(game,{});game.world.setBlock(4,64,0,blockState(56));require(sim.breakBlock(game,4,64,0,1)&&!sim.droppedEntities().empty(),"disabled tile drops still create ore XP");bool onlyExperience=true;for(const auto& pair:sim.droppedEntities())onlyExperience=onlyExperience&&pair.second.orb&&game.entities.at(pair.first).xpValue==pair.second.value;require(onlyExperience,"native XP amounts publish explicitly without item drops");
    game.world.setBlock(5,64,0,blockState(61));sim.furnaceAt(5,64,0).slots[0]=item(15);const auto furnaceBefore=nbt::encode(sim.saveState(game));require(!sim.breakBlock(game,5,64,0,1)&&furnaceBefore==nbt::encode(sim.saveState(game)),"unported nonempty machine drops cannot delete contents");
    sim.furnaceAt(5,64,0).slots[0]=ItemStack{};require(sim.breakBlock(game,5,64,0,1)&&sim.furnaces().empty(),"empty furnace breaks and removes machine state");
}
void entityPersistenceTests(){Game game;testFloor(game);Simulation sim;sim.start(game,987);sim.settings.weatherCycle=false;sim.spawnItem(game,item(4,7),5.5,66,.5);sim.spawnExperience(game,7,5.5,66,1.5);for(int i=0;i<7;++i)sim.tick(game,{});
    const auto saved=sim.saveState(game);Game loaded;testFloor(loaded);Simulation restored;restored.loadState(loaded,saved);require(restored.droppedEntities().size()==2&&loaded.entities.size()==2,"save restores dropped item and XP authority/render states");
    for(int i=0;i<12;++i){sim.tick(game,{});restored.tick(loaded,{});}require(nbt::encode(sim.saveState(game))==nbt::encode(restored.saveState(loaded)),"saved RNG, bounds, timers and motion continue exactly after load");
    auto corrupt=saved;corrupt.compound["DroppedEntities"].list[0].compound["Width"]=nbt::integer(3,1000);const auto unchanged=nbt::encode(restored.saveState(loaded));bool rejected=false;try{restored.loadState(loaded,corrupt);}catch(const net::ProtocolError&){rejected=true;}require(rejected&&unchanged==nbt::encode(restored.saveState(loaded)),"invalid entity state cannot partially replace an active save");
}
void playerThrowTests(){Game game;testFloor(game);Simulation sim;sim.start(game,123);game.inventory[36]=item(4,7);require(sim.dropHeldItem(game)&&game.inventory[36].count==6&&sim.droppedEntities().size()==1,"Q drops one held item through authority");
    const auto& first=sim.droppedEntities().begin()->second;require(first.item.count==1&&first.pickupDelay==40&&first.thrower==sim.settings.playerName&&first.y>game.player.y+1,"player drop uses eye offset, forty tick delay and thrower");
    require(sim.dropHeldItem(game,true)&&game.inventory[36].empty()&&sim.droppedEntities().size()==2,"control Q drops entire remaining stack once");
    game.beforeOfflineContainerClose=[&]{sim.beforeContainerClose(game);};game.cursor=item(264,2);game.inventory[1]=item(5,3);game.inventory[2]=item(280,2);game.inventory[0]=item(268);const auto previous=sim.droppedEntities().size();game.closeContainer();
    require(game.cursor.empty()&&game.inventory[0].empty()&&game.inventory[1].empty()&&game.inventory[2].empty()&&sim.droppedEntities().size()==previous+3,"closing player crafting drops cursor and inputs, not derived result");
    game.world.setBlock(2,64,0,blockState(58));sim.interactBlock(game,2,64,0);game.cursor=item(266);game.window.slots[1]=item(17,2);game.window.slots[9]=item(263);game.window.slots[0]=item(5,4);const auto beforeWorkbench=sim.droppedEntities().size();game.closeContainer();require(!game.window.open&&sim.droppedEntities().size()==beforeWorkbench+3,"closing workbench drops cursor and every grid input once");
    game.world.setBlock(2,64,0,blockState(61));sim.interactBlock(game,2,64,0);game.window.slots[0]=item(15);game.window.slots[1]=item(263);game.cursor=item(4);const auto beforeFurnace=sim.droppedEntities().size();game.closeContainer();require(sim.droppedEntities().size()==beforeFurnace+1&&sim.furnaceAt(2,64,0).slots[0].id==15&&sim.furnaceAt(2,64,0).slots[1].id==263,"machine close preserves machine inventory and throws cursor only");
}
void guiDropTests(){Game game;testFloor(game);Simulation sim;sim.start(game);Controller controller;int called=0;game.offlineItemDrop=[&](const ItemStack&,bool){++called;return false;};
    game.cursor=item(4,7);require(!controller.clickSlot(game,-999,1)&&game.cursor.count==7&&called==1,"rejected outside click preserves cursor");game.cursor=ItemStack{};game.inventory[9]=item(17,10);require(!controller.clickSlot(game,9,0,4)&&game.inventory[9].count==10&&called==2,"rejected slot drop preserves inventory");
    game.offlineItemDrop=[&](const ItemStack& stack,bool trace){++called;return sim.dropFromPlayer(game,stack,trace)>=0;};require(controller.clickSlot(game,9,0,4)&&game.inventory[9].count==9&&sim.droppedEntities().size()==1,"slot drop invokes authority once then commits decrement");
    const auto beforeInvalid=nbt::encode(sim.saveState(game));require(!sim.dropFromPlayerBatch(game,{item(4),item(4095)})&&beforeInvalid==nbt::encode(sim.saveState(game)),"invalid batch is rejected before any spawn or RNG consumption");
    game.world.setBlock(2,64,0,blockState(58));sim.interactBlock(game,2,64,0);for(int slot=10;slot<46;++slot)game.window.slots[static_cast<std::size_t>(slot)]=item(1,64);for(int slot=9;slot<45;++slot)game.inventory[static_cast<std::size_t>(slot)]=item(1,64);
    const int ingredients[]={335,335,335,353,344,353,296,296,296};for(int i=0;i<9;++i)game.window.slots[static_cast<std::size_t>(i+1)]=item(ingredients[i],i<3?2:1);crafting::refresh(game);
    const int oldCalled=called;const auto oldDrops=sim.droppedEntities().size();require(!controller.clickSlot(game,0,0)&&called==oldCalled&&game.cursor.empty()&&game.window.slots[1].count==2&&sim.droppedEntities().size()==oldDrops,"multi-container overflow cannot partially invoke a single-item authority");
    game.offlineItemDrops=[&](const std::vector<ItemStack>& stacks,bool trace){return sim.dropFromPlayerBatch(game,stacks,trace);};require(controller.clickSlot(game,0,0)&&game.cursor.id==354&&game.window.slots[1].count==1&&sim.droppedEntities().size()==oldDrops+3,"crafting overflow atomically drops three buckets and commits recipe");
    game.window=ContainerWindow{};game.cursor=ItemStack{};game.inventory[9]=item(4,8);game.online=true;std::vector<net::Packet> sent;game.outgoingObserver=[&](const net::Packet& packet){sent.push_back(packet);};const auto onlineBefore=sim.droppedEntities().size();controller.clickSlot(game,9,0,4);require(sim.droppedEntities().size()==onlineBefore&&game.inventory[9].count==7&&!sent.empty()&&sent.back().id==0x0e,"online slot drop sends original click and never invokes native drop authority");game.online=false;
    game.offlineHeldItemDrop=[&](bool all){return sim.dropHeldItem(game,all);};game.inventory[36]=item(4,3);const auto beforeQ=sim.droppedEntities().size();require(game.dropHeldItem()&&game.inventory[36].count==2&&sim.droppedEntities().size()==beforeQ+1,"Game Q entry point delegates one inventory decrement to simulation");
}
std::uint32_t bits(float value){std::uint32_t result;std::memcpy(&result,&value,4);return result;}
std::uint64_t bits(double value){std::uint64_t result;std::memcpy(&result,&value,8);return result;}
void printItem(const ItemStack& stack){if(stack.empty())std::cout<<" -1 0 0";else std::cout<<' '<<stack.id<<' '<<int(stack.count)<<' '<<stack.damage;}
int trace(const char* path){std::ifstream file(path);if(!file)throw std::runtime_error("cannot open simulation fixture");Game game;Simulation sim;FurnaceState furnace;BrewingState stand;char mode='f';
    for(std::string line;std::getline(file,line);){if(line.empty()||line[0]=='#')continue;std::istringstream input(line);char command;input>>command;
        if(command=='F'){game=Game{};sim=Simulation{};int natural,immune;input>>sim.settings.difficulty>>natural>>immune>>game.health>>game.maxHealth>>game.food>>game.saturation>>sim.food.exhaustion>>sim.food.timer;sim.settings.naturalRegeneration=natural!=0;game.invulnerable=immune!=0;mode='f';}
        else if(command=='E'){float amount;input>>amount;sim.addExhaustion(amount);}
        else if(command=='W'){game=Game{};sim=Simulation{};std::uint64_t seed;int daylight,raining,thundering;input>>seed>>game.dimension>>daylight;sim.start(game,seed);sim.settings.daylightCycle=daylight!=0;input>>sim.weather.totalTime>>sim.weather.dayTime>>sim.weather.cleanWeatherTime>>sim.weather.rainTime>>sim.weather.thunderTime>>raining>>thundering>>sim.weather.rainStrength>>sim.weather.thunderStrength;sim.weather.previousRainStrength=sim.weather.rainStrength;sim.weather.previousThunderStrength=sim.weather.thunderStrength;sim.weather.raining=raining!=0;sim.weather.thundering=thundering!=0;mode='w';}
        else if(command=='C'){furnace=FurnaceState{};for(auto& slot:furnace.slots){int id,count,damage;input>>id>>count>>damage;slot=item(id,count,damage);}input>>furnace.burnTime>>furnace.currentItemBurnTime>>furnace.cookTime>>furnace.totalCookTime;mode='c';}
        else if(command=='B'){stand=BrewingState{};for(auto& slot:stand.slots){int id,count,damage;input>>id>>count>>damage;slot=item(id,count,damage);}input>>stand.brewTime>>stand.ingredientId;mode='b';}
        else if(command=='S'){int slot,id,count,damage;input>>slot>>id>>count>>damage;stand.slots[static_cast<std::size_t>(slot)]=item(id,count,damage);}
        else if(command=='R'){int meta,id,variant;input>>meta>>id>>variant;const auto ingredient=item(id,1,variant);const int after=brewing::result(meta,ingredient);BrewingState probe;probe.slots[0]=item(373,1,meta);probe.slots[3]=ingredient;std::cout<<"R "<<after<<' '<<brewing::canBrew(probe)<<'\n';}
        else if(command=='O'){int kind,id,count,damage;std::uint64_t seed;input>>kind>>seed;game=Game{};sim=Simulation{};game.player.teleport(1.25,64,-2.5);sim.start(game,seed);input>>id>>count>>damage;game.cursor=item(id,count,damage);std::array<ItemStack,9> stacks;for(auto& stack:stacks){input>>id>>count>>damage;stack=item(id,count,damage);}const int size=kind==0?4:kind==1?9:kind==2?3:4;
            if(kind==0)for(int slot=0;slot<4;++slot)game.inventory[static_cast<std::size_t>(slot+1)]=stacks[static_cast<std::size_t>(slot)];
            else{game.world.setBlock(2,64,0,blockState(kind==1?58:kind==2?61:117));sim.interactBlock(game,2,64,0);for(int slot=0;slot<size;++slot)game.window.slots[static_cast<std::size_t>(slot+(kind==1?1:0))]=stacks[static_cast<std::size_t>(slot)];}
            game.beforeOfflineContainerClose=[&]{sim.beforeContainerClose(game);};game.closeContainer();std::cout<<"O";printItem(game.cursor);std::cout<<' '<<size;for(int slot=0;slot<size;++slot){if(kind==0)printItem(game.inventory[static_cast<std::size_t>(slot+1)]);else if(kind==1)printItem(ItemStack{});else if(kind==2)printItem(sim.furnaceAt(2,64,0).slots[static_cast<std::size_t>(slot)]);else printItem(sim.brewingAt(2,64,0).slots[static_cast<std::size_t>(slot)]);}std::cout<<' '<<sim.droppedEntities().size();for(const auto& pair:sim.droppedEntities()){const auto& entity=pair.second;printItem(entity.item);std::cout<<' '<<bits(entity.y)<<' '<<bits(entity.motionX)<<' '<<bits(entity.motionY)<<' '<<bits(entity.motionZ);}const auto saved=sim.saveState(game);worldgen::JavaRandom random(saved.get("PlayerRandomState")->integer^0x5deece66dLL);const auto first=static_cast<std::uint32_t>(random.nextInt()),second=static_cast<std::uint32_t>(random.nextInt());std::cout<<' '<<first<<' '<<second<<'\n';}
        else if(command=='T'){int count;input>>count;for(int i=0;i<count;++i){if(mode=='f'){sim.tickFood(game);std::cout<<"F "<<bits(game.health)<<' '<<game.food<<' '<<bits(game.saturation)<<' '<<bits(sim.food.exhaustion)<<' '<<sim.food.timer<<' '<<sim.food.previousFoodLevel<<'\n';}
            else if(mode=='w'){sim.tickTimeWeather(game);const auto& weather=sim.weather;std::cout<<"W "<<weather.totalTime<<' '<<weather.dayTime<<' '<<weather.cleanWeatherTime<<' '<<weather.rainTime<<' '<<weather.thunderTime<<' '<<weather.raining<<' '<<weather.thundering<<' '<<bits(weather.previousRainStrength)<<' '<<bits(weather.rainStrength)<<' '<<bits(weather.previousThunderStrength)<<' '<<bits(weather.thunderStrength)<<'\n';}
            else if(mode=='c'){Simulation::tickFurnace(furnace);std::cout<<"C "<<furnace.burnTime<<' '<<furnace.currentItemBurnTime<<' '<<furnace.cookTime<<' '<<furnace.totalCookTime;for(const auto& slot:furnace.slots)printItem(slot);std::cout<<'\n';}
            else{brewing::tick(stand);std::cout<<"B "<<stand.brewTime<<' '<<stand.ingredientId<<' '<<stand.filledMask;for(const auto& slot:stand.slots)printItem(slot);std::cout<<'\n';}}}
    }return 0;}
}
int main(int argc,char** argv){try{if(argc==3&&std::string(argv[1])=="--trace")return trace(argv[2]);foodTests();furnaceTests();brewingTests();integrationTests();bridgeTests();breakBridgeTests();entityPersistenceTests();playerThrowTests();guiDropTests();std::cout<<"simulation tests passed ("<<checks<<" checks)\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
