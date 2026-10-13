#include "mc/controller.hpp"
#include "mc/crafting.hpp"
#include "mc/nbt.hpp"
#include "mc/registry.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace mc;
using mc::net::Packet;
using mc::net::Reader;
using mc::net::Writer;

namespace {
std::size_t checks = 0;
void require(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
void near(double actual, double expected, const char* message, double tolerance = 1e-7) { require(std::abs(actual-expected) <= tolerance,message); }
ItemStack item(int id, int count=1, int damage=0) {
    ItemStack value; value.id = static_cast<std::int16_t>(id); value.count = static_cast<std::int8_t>(count); value.damage = static_cast<std::int16_t>(damage); return value;
}
void setup(Game& game, std::vector<Packet>& sent) {
    game.online = game.joined = game.spawned = true; game.entityId = 47; game.player.teleport(.5,64,.5); game.player.onGround = true;
    game.world.setBlock(0,63,0,blockState(1));
    game.outgoingObserver = [&](const Packet& packet) { sent.push_back(packet); };
}
ControllerTarget target(int x=2,int y=64,int z=0) {
    ControllerTarget value; value.block = true; value.x=x; value.y=y; value.z=z; value.face=1; value.hitY=1; return value;
}
std::vector<int> diggingActions(const std::vector<Packet>& packets) {
    std::vector<int> actions;
    for (const auto& packet : packets) if (packet.id == 7) { Reader body(packet.data); actions.push_back(body.varInt()); }
    return actions;
}
ItemStack enchanted(int id,int enchant,int level) {
    auto value = item(id); Writer nbt;
    nbt.u8(10).u16(0).u8(9).u16(4).bytes(std::vector<std::uint8_t>{'e','n','c','h'}).u8(10).i32(1)
        .u8(2).u16(2).bytes(std::vector<std::uint8_t>{'i','d'}).i16(static_cast<std::int16_t>(enchant))
        .u8(2).u16(3).bytes(std::vector<std::uint8_t>{'l','v','l'}).i16(static_cast<std::int16_t>(level)).u8(0).u8(0);
    value.nbt = nbt.data; return value;
}
void miningTests() {
    Game game; std::vector<Packet> sent; setup(game,sent); Controller control;
    const auto hit = target(); game.world.setBlock(hit.x,hit.y,hit.z,blockState(1));
    near(control.relativeBlockHardness(game,hit.x,hit.y,hit.z),1.0f/1.5f/100.0f,"stone by hand uses harvest penalty");
    game.inventory[36] = item(257);
    near(control.relativeBlockHardness(game,hit.x,hit.y,hit.z),6.0f/1.5f/30.0f,"iron pickaxe uses Java registry speed");
    ControllerInput input; input.target=hit; input.attackHeld=input.attackPressed=true;
    control.tick(game,input); input.attackPressed=false;
    require(diggingActions(sent)==std::vector<int>{0},"first mining tick emits START");
    require(sent.front().id==10 && sent.back().id==10,"click and held mining both swing in first tick");
    for(int i=1;i<8;++i)control.tick(game,input);
    require(diggingActions(sent)==std::vector<int>({0,2}),"stone with iron pick sends STOP on eighth damage tick");
    require(game.world.getBlock(hit.x,hit.y,hit.z)==0 && game.inventory[36].damage==1,"local break and tool durability");
    require(!control.mining && control.progress==0,"completed mining resets progress");
    game.world.setBlock(3,64,0,blockState(1)); input.target=target(3); sent.clear();
    for(int i=0;i<5;++i)control.tick(game,input);
    require(diggingActions(sent).empty(),"five-tick block-hit delay");
    control.tick(game,input); require(diggingActions(sent)==std::vector<int>{0},"next target starts after delay");
    input.attackHeld=false; sent.clear(); control.tick(game,input);
    require(diggingActions(sent)==std::vector<int>{1},"release mining sends ABORT");
    Reader abort(sent[0].data); abort.varInt(); abort.u64(); require(abort.u8()==0,"release ABORT uses DOWN face");
    control.reset(); sent.clear(); game.world.setBlock(2,64,0,blockState(1)); game.inventory[36]=enchanted(278,32,5);
    near(control.relativeBlockHardness(game,2,64,0),34.0f/1.5f/30.0f,"Efficiency5 adds26 above diamond speed8");
    game.effects[3]={0,100,false}; game.effects[4]={1,100,false};
    near(control.relativeBlockHardness(game,2,64,0),34.0f*1.2f*.09f/1.5f/30.0f,"haste/fatigue float operation order");
    game.effects.clear(); game.player.onGround=false;
    near(control.relativeBlockHardness(game,2,64,0),34.0f/5.0f/1.5f/30.0f,"airborne mining penalty");
    game.player.onGround=true; game.world.setBlock(0,65,0,blockState(9));
    near(control.relativeBlockHardness(game,2,64,0),34.0f/5.0f/1.5f/30.0f,"head underwater penalty independently of body flag");
    game.inventory[5]=enchanted(310,6,1);
    near(control.relativeBlockHardness(game,2,64,0),34.0f/1.5f/30.0f,"Aqua Affinity removes underwater mining penalty");
    game.world.setBlock(2,64,0,blockState(7)); require(control.relativeBlockHardness(game,2,64,0)==0,"bedrock is unbreakable");
    game.gameMode=1; game.creativeMode=true; game.world.setBlock(2,64,0,blockState(1)); game.inventory[36]=item(276);
    input.target=hit; input.attackHeld=input.attackPressed=true; sent.clear(); control.reset(); control.tick(game,input);
    require(game.world.getBlock(2,64,0)==blockState(1) && diggingActions(sent)==std::vector<int>{0},"creative sword sends source START but cannot destroy");
    game.inventory[36]=ItemStack(); sent.clear(); control.reset(); control.tick(game,input);
    require(game.world.getBlock(2,64,0)==0 && diggingActions(sent)==std::vector<int>{0},"creative instant break has START only");
}
void useAndCombatTests() {
    Game game; std::vector<Packet> sent; setup(game,sent); Controller control; ControllerInput input;
    game.inventory[36]=item(267); input.useHeld=input.usePressed=true; control.tick(game,input);
    require(control.usingItem()&&control.useTicksRemaining()==71999,"sword use starts72000-tick counter");
    input.usePressed=false; sent.clear(); for(int i=0;i<30;++i)control.tick(game,input);
    require(sent.empty()&&control.usingItem(),"held sword does not spam repeated item use");
    input.useHeld=false; control.tick(game,input);
    require(!control.usingItem()&&diggingActions(sent)==std::vector<int>{5},"release use sends RELEASE_USE_ITEM");
    control.reset(); sent.clear(); game.inventory[36]=item(260,2); game.food=10; input.useHeld=input.usePressed=true;
    control.tick(game,input); input.usePressed=false; for(int i=0;i<40;++i)control.tick(game,input);
    require(control.usingItem()&&control.useTicksRemaining()<0&&game.inventory[36].count==2,"remote food does not finish before server status9");
    game.consumePacket({0x1a,Writer().i32(47).i8(9).data}); input.useHeld=false; control.tick(game,input);
    require(!control.usingItem()&&game.inventory[36].count==1&&game.food==14,"status9 finishes food once");
    control.reset(); sent.clear(); game.inventory[36]=item(261); game.inventory[9]=item(262,5); input.useHeld=input.usePressed=true;
    control.tick(game,input); input.usePressed=false; for(int i=0;i<4;++i)control.tick(game,input);
    input.useHeld=false; control.tick(game,input);
    require(game.inventory[9].count==4&&game.inventory[36].damage==1,"bow release predicts one arrow and durability cost");
    Entity enemy; enemy.id=100; enemy.kind=EntityKind::Player; game.entities[100]=enemy;
    game.inventory[36]=item(267); game.player.setSprinting(true); game.player.motionX=1; game.player.motionZ=2;
    input={}; input.target.entityId=100; input.attackPressed=true; sent.clear(); control.reset(); control.tick(game,input);
    require(sent.size()==2&&sent[0].id==10&&sent[1].id==2,"attack swing then UseEntity packet");
    near(game.player.motionX,.6,"PvP sprint attack slows local X"); near(game.player.motionZ,1.2,"PvP sprint attack slows local Z");
    require(!game.player.sprinting&&game.inventory[36].damage==1,"PvP attack cancels sprint and damages sword");
    game.entities[100].kind=EntityKind::Mob; game.player.setSprinting(true); game.player.motionX=1; control.tick(game,input);
    require(game.player.sprinting&&game.player.motionX==1,"remote mob attackEntityFrom false does not invent PvP slowdown");
    control.reset(); game.inventory[36]=item(310); game.inventory[5]=ItemStack(); input={}; input.useHeld=input.usePressed=true;
    control.tick(game,input); require(game.inventory[36].empty()&&game.inventory[5].id==310,"ordinary armor right-click equips exact armor slot");
}
void placementTests() {
    Game game; std::vector<Packet> sent; setup(game,sent); Controller control; ControllerInput input;
    game.world.setBlock(2,64,0,blockState(1)); game.inventory[36]=item(5,4,2);
    input.target=target(); input.useHeld=input.usePressed=true; control.tick(game,input);
    require(game.world.getBlock(2,65,0)==blockState(5,2)&&game.inventory[36].count==3,"block placement predicts subtype and count");
    require(sent.size()==2&&sent[0].id==8&&sent[1].id==10,"successful placement then swing");
    input.usePressed=false; sent.clear(); for(int i=0;i<3;++i)control.tick(game,input);
    require(sent.empty(),"right-click cooldown spans next three ticks");
    control.tick(game,input); require(!sent.empty()&&sent[0].id==8,"held right-click retried at four ticks");
    game.world.setBlock(2,64,0,blockState(44,0)); game.world.setBlock(2,65,0,0); game.inventory[36]=item(44,2,0);
    control.reset(); sent.clear(); input.usePressed=true; control.tick(game,input);
    require(game.world.getBlock(2,64,0)==blockState(43,0)&&game.inventory[36].count==1,"matching slabs merge into double slab");
    game.world.setBlock(0,63,0,blockState(1)); game.inventory[36]=item(1,2); input.target=target(0,63,0);
    control.reset(); sent.clear(); control.tick(game,input);
    require(game.world.getBlock(0,64,0)==0&&game.inventory[36].count==2,"placement collision cannot embed local player");
    game.world.setBlock(2,64,0,blockState(84,0)); game.inventory[36]=item(2256); input.target=target();
    control.reset(); sent.clear(); control.tick(game,input);
    require(sent.size()==2&&sent[0].id==8&&sent[1].id==10&&game.inventory[36].count==1,
        "remote jukebox record use succeeds without guessed extra C08 or local consumption");
}
void inventoryTests() {
    Game game; std::vector<Packet> sent; setup(game,sent); Controller control;
    game.inventory[9]=item(1,10); require(control.clickSlot(game,9,0),"left pickup accepted");
    require(game.inventory[9].empty()&&game.cursor.count==10,"left pickup moves whole stack");
    Reader click(sent.back().data);
    require(sent.back().id==0x0e&&click.u8()==0&&click.i16()==9&&click.u8()==0&&click.i16()==1&&click.u8()==0,"C0E fields and first transaction ID");
    const auto returned=Game::readItem(click); require(returned.id==1&&returned.count==10,"C0E carries slotClick return original stack"); click.requireEnd();
    require(control.clickSlot(game,10,1),"right placement accepted"); require(game.inventory[10].count==1&&game.cursor.count==9,"right placement moves one");
    require(control.clickSlot(game,11,0),"left placement accepted"); require(game.inventory[11].count==9&&game.cursor.empty(),"left placement empties cursor");
    require(control.clickSlot(game,11,1),"right pickup accepted"); require(game.cursor.count==5&&game.inventory[11].count==4,"right pickup rounds half upward");
    require(control.clickSlot(game,-999,1),"outside right drop accepted"); require(game.cursor.count==4,"outside right drop one");
    game.cursor=item(1,10); game.inventory[9]=game.inventory[10]=game.inventory[11]=ItemStack();
    require(control.clickSlot(game,-999,0,5),"drag start");
    control.clickSlot(game,9,1,5); control.clickSlot(game,10,1,5); control.clickSlot(game,11,1,5); control.clickSlot(game,-999,2,5);
    require(game.inventory[9].count==3&&game.inventory[10].count==3&&game.inventory[11].count==3&&game.cursor.count==1,"left drag distributes equally leaving remainder");
    game.cursor=ItemStack(); game.inventory[12]=item(310); require(control.clickSlot(game,12,0,1),"shift armor accepted");
    require(game.inventory[5].id==310&&game.inventory[12].empty(),"shift click equips empty matching armor slot");
    game.cursor=item(1,3); const auto previous=game.inventory[5]; control.clickSlot(game,5,0);
    require(game.inventory[5].id==previous.id&&game.cursor.count==3,"armor slot rejects ordinary blocks");
    game.cursor=ItemStack(); game.inventory[13]=item(3,16); control.clickSlot(game,13,0,1);
    require(game.inventory[36].id==3&&game.inventory[13].empty(),"shift backpack transfers into hotbar");
    const auto count=sent.size(); require(control.clickSlot(game,0,0)&&sent.size()==count+1,"empty crafting result follows ordinary click transaction");
    game.window.open=true; game.window.id=2; game.window.type="minecraft:container"; game.window.slots.resize(63); ++game.windowRevision;
    game.window.slots[0]=item(4,32); game.cursor=ItemStack(); control.clickSlot(game,0,0,1);
    require(game.window.slots[0].empty()&&game.inventory[44].id==4,"chest shift pickup uses reverse player inventory order");
    require(control.closeWindow(game)&&!game.window.open&&sent.back().id==0x0d,"close window packet and local state");
    Game creative;std::vector<Packet> creativePackets;setup(creative,creativePackets);Controller local;
    creative.inventory[9]=item(1,5);
    require(local.clickSlot(creative,9,0,0,false)&&creative.cursor.count==5&&creative.inventory[9].empty(),"local prediction overload still updates inventory");
    require(creativePackets.empty(),"local prediction emits no C0E");
    require(local.clickSlot(creative,10,0),"default notification remains enabled");
    Reader firstTransaction(creativePackets.back().data);firstTransaction.u8();firstTransaction.i16();firstTransaction.u8();
    require(firstTransaction.i16()==1,"local prediction does not consume transaction ID");
}
std::vector<std::uint8_t> unhex(const std::string& value) {
    if(value=="-"||value.empty())return {};
    if(value.size()%2)throw std::runtime_error("bad fixture hex");
    std::vector<std::uint8_t> out;for(std::size_t i=0;i<value.size();i+=2)out.push_back(static_cast<std::uint8_t>(std::stoi(value.substr(i,2),nullptr,16)));return out;
}
std::string hex(const std::string& value) {
    static const char* digits="0123456789abcdef";std::string out;
    for(unsigned char c:value){out+=digits[c>>4];out+=digits[c&15];}return out;
}
void printTag(const nbt::Tag& tag) {
    std::cout<<tag.type<<':';
    if(tag.type>=1&&tag.type<=4)std::cout<<tag.integer;
    else if(tag.type==5){float f=static_cast<float>(tag.number);std::uint32_t bits;std::memcpy(&bits,&f,4);std::cout<<bits;}
    else if(tag.type==6){std::uint64_t bits;std::memcpy(&bits,&tag.number,8);std::cout<<bits;}
    else if(tag.type==7||tag.type==11)std::cout<<hex(std::string(tag.bytes.begin(),tag.bytes.end()));
    else if(tag.type==8)std::cout<<hex(tag.string);
    else if(tag.type==9){std::cout<<tag.subtype<<'[';for(const auto& child:tag.list){printTag(child);std::cout<<';';}std::cout<<']';}
    else if(tag.type==10){std::cout<<'{';for(const auto& pair:tag.compound){std::cout<<hex(pair.first)<<'=';printTag(pair.second);std::cout<<';';}std::cout<<'}';}
}
void printItem(const ItemStack& value) {
    if (value.empty()) std::cout<<" -1 0 0";
    else std::cout<<' '<<value.id<<' '<<int(value.count)<<' '<<value.damage;
}
void printDetailedItem(const ItemStack& value){printItem(value);std::cout<<' ';printTag(value.empty()?nbt::Tag{}:nbt::parse(value.nbt));}
ItemStack fixtureItem(std::istream& input){int id,count,damage;std::string tag;input>>id>>count>>damage>>tag;auto stack=item(id,count,damage);stack.nbt=unhex(tag);return stack;}
int trace(const char* path) {
    std::ifstream file(path); if(!file)throw std::runtime_error("cannot read controller fixture");
    Game game; std::vector<Packet> sent; setup(game,sent); Controller control;
    for(std::string line;std::getline(file,line);) {
        if(line.empty()||line[0]=='#')continue;
        std::istringstream input(line); char command; input>>command;
        if(command=='W') { game=Game(); sent.clear(); setup(game,sent); control.reset(); }
        else if(command=='H') {
            int block,meta,tool,efficiency,haste,fatigue,water,ground,aqua;
            input>>block>>meta>>tool>>efficiency>>haste>>fatigue>>water>>ground>>aqua;
            game.world.clear(); game.inventory={}; game.effects.clear(); game.player.teleport(.5,64,.5); game.player.onGround=ground!=0;
            game.world.setBlock(2,64,0,blockState(static_cast<unsigned>(block),static_cast<unsigned>(meta)));
            if(water)game.world.setBlock(0,65,0,blockState(9));
            if(tool>=0)game.inventory[36]=efficiency>0?enchanted(tool,32,efficiency):item(tool);
            if(aqua)game.inventory[5]=enchanted(310,6,1);
            if(haste>=0)game.effects[3]={haste,1000,false};
            if(fatigue>=0)game.effects[4]={fatigue,1000,false};
            const float hardness=control.relativeBlockHardness(game,2,64,0); std::uint32_t bits;std::memcpy(&bits,&hardness,sizeof bits);
            std::cout<<"H "<<bits<<'\n';
        } else if(command=='R') {
            int width,height;input>>width>>height;std::vector<ItemStack> grid;
            for(int i=0;i<width*height;++i){int id,count,damage;input>>id>>count>>damage;grid.push_back(item(id,count,damage));}
            const auto result=crafting::match(grid,width,height);std::cout<<"R";printItem(result.output);for(const auto& stack:result.remaining)printItem(stack);std::cout<<'\n';
        } else if(command=='A') {
            auto first=fixtureItem(input),second=fixtureItem(input);std::string encoded;int creative;input>>encoded>>creative;const auto text=unhex(encoded);
            const auto result=crafting::anvilResult(first,second,std::string(text.begin(),text.end()),creative!=0);
            std::cout<<"A "<<result.cost<<' '<<result.materialCost;printDetailedItem(result.output);std::cout<<'\n';
        } else if(command=='C') {
            std::string kind;input>>kind;int count=0;std::string type="minecraft:container";
            if(kind=="workbench"){count=10;type="minecraft:crafting_table";}
            else if(kind=="furnace"){count=3;type="minecraft:furnace";}
            else if(kind=="brewing"){count=4;type="minecraft:brewing_stand";}
            else if(kind=="enchant"){count=2;type="minecraft:enchanting_table";}
            else if(kind=="beacon"){count=1;type="minecraft:beacon";}
            else if(kind=="anvil"){count=3;type="minecraft:anvil";}
            else if(kind=="merchant"){count=3;type="minecraft:villager";}
            else if(kind=="horse"){count=2;type="EntityHorse";}
            else if(kind=="donkey"){count=17;type="EntityHorse";}
            else count=std::stoi(kind);
            if(!count)game.window=ContainerWindow();
            else { game.window=ContainerWindow();game.window.open=true;game.window.id=1;game.window.type=type;game.window.slots.resize(static_cast<std::size_t>(count)+36);
                for(int i=0;i<36;++i)game.window.slots[static_cast<std::size_t>(count+i)]=game.inventory[static_cast<std::size_t>(9+i)];
                if(type=="EntityHorse"){game.window.entityId=999;Entity horse;horse.id=999;horse.type=100;horse.kind=EntityKind::Mob;horse.metadata[19].integer=kind=="donkey"?1:0;game.entities[999]=horse;}}
            ++game.windowRevision;
        } else if(command=='S') {
            int slot,id,count,damage;input>>slot>>id>>count>>damage;
            auto value=item(id,count,damage);std::string tag;if(input>>tag)value.nbt=unhex(tag);
            Writer body;body.i8(static_cast<std::int8_t>(game.window.open?game.window.id:0)).i16(static_cast<std::int16_t>(slot));Game::writeItem(body,value);
            game.consumePacket({0x2f,body.data});
        } else if(command=='K') {
            int id,count,damage;input>>id>>count>>damage;game.cursor=item(id,count,damage);
        } else if(command=='G') { int mode;input>>mode;game.creativeMode=mode!=0; }
        else if(command=='X'){input>>game.experienceLevel;}
        else if(command=='N'){std::string encoded;input>>encoded;const auto text=unhex(encoded);control.renameItem(game,std::string(text.begin(),text.end()));}
        else if(command=='O'){TradeOffer offer;offer.first=fixtureItem(input);offer.second=fixtureItem(input);offer.result=fixtureItem(input);input>>offer.uses>>offer.maxUses;offer.disabled=offer.uses>=offer.maxUses;
            game.window.trades.push_back(offer);game.window.tradesLoaded=true;crafting::refreshMerchant(game);}
        else if(command=='J'){int index;input>>index;control.selectTrade(game,index);}
        else if(command=='V'){int key,value;input>>key>>value;game.consumePacket({0x31,Writer().u8(static_cast<std::uint8_t>(game.window.id)).i16(static_cast<std::int16_t>(key)).i16(static_cast<std::int16_t>(value)).data});}
        else if(command=='E'){int choice;input>>choice;std::cout<<"E "<<control.enchantItem(game,choice)<<' '<<game.experienceLevel;printDetailedItem(game.window.slots[0]);printDetailedItem(game.window.slots[1]);std::cout<<'\n';}
        else if(command=='P'){
            std::cout<<"P "<<game.experienceLevel;
            if(game.window.type=="minecraft:anvil")std::cout<<' '<<game.window.properties[0]<<' '<<game.window.repairMaterialCost;
            else if(game.window.type=="minecraft:villager"){std::cout<<' '<<game.window.activeTrade<<" [";for(const auto& offer:game.window.trades)std::cout<<offer.uses<<','<<offer.maxUses<<','<<(offer.disabled||offer.uses>=offer.maxUses)<<';';std::cout<<']';}
            printDetailedItem(game.cursor);for(std::size_t i=0;i<std::min<std::size_t>(3,game.window.slots.size());++i)printDetailedItem(game.window.slots[i]);std::cout<<'\n';
        }
        else if(command=='I') {
            int slot,button,mode;input>>slot>>button>>mode;sent.clear();
            if(!control.clickSlot(game,slot,button,mode)||sent.empty()) {std::cout<<"REJECT\n";continue;}
            Reader body(sent.back().data);body.u8();body.i16();body.u8();body.i16();body.u8();const auto returned=Game::readItem(body);
            std::cout<<"I";printItem(returned);printItem(game.cursor);
            if(game.window.open)for(const auto& value:game.window.slots)printItem(value);else for(const auto& value:game.inventory)printItem(value);
            std::cout<<'\n';
        }
    }
    return 0;
}
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc==3&&std::string(argv[1])=="--trace")return trace(argv[2]);
        miningTests(); useAndCombatTests(); placementTests(); inventoryTests();
        std::cout<<"controller tests passed ("<<checks<<" checks)\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
