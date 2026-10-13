#include "mc/gui_inventory.hpp"
#include "mc/controller.hpp"
#include "mc/game.hpp"
#include "mc/renderer.hpp"
#include "mc/i18n.hpp"
#include "mc/nbt.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

// No GL context/window is needed to verify hit regions and real click packets.
namespace mc {
bool Renderer::registerUITexture(const std::string&,const std::string&){return true;}
void Renderer::rect(float,float,float,float,std::uint32_t){}
void Renderer::gradient(float,float,float,float,std::uint32_t,std::uint32_t){}
void Renderer::text(const std::string&,float,float,float,std::uint32_t,bool){}
float Renderer::textWidth(const std::string& text,float scale)const{return float(text.size())*6*scale;}
void Renderer::sprite(const std::string&,float,float,float,float,float,float,float,float,std::uint32_t){}
void Renderer::itemIcon(int,int,float,float,float,int){}
void Renderer::itemIcon(const ItemStack&,float,float,float,int){}
void Renderer::previewEntity(float,float,float,float,float){}
}

namespace {
int checks=0;
void require(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
mc::ItemStack item(int id,int count){mc::ItemStack result;result.id=std::int16_t(id);result.count=std::int8_t(count);return result;}
mc::InventoryInput at(const mc::Game& game,int slot,double time=1) {
    mc::InventoryInput input;input.timeSeconds=time;
    auto layout=mc::InventoryScreen::layout(game);
    for(auto& entry:layout.slots)if(entry.index==slot){input.x=float(entry.x+8);input.y=float(entry.y+8);return input;}
    throw std::runtime_error("Test slot is absent");
}
struct Click {int window,slot,button,mode,transaction;mc::ItemStack returned;};
Click click(const mc::net::Packet& packet) {
    require(packet.id==0x0e,"UI must emit C0E click-window packet");mc::net::Reader r(packet.data);Click value;
    value.window=r.u8();value.slot=r.i16();value.button=r.u8();value.transaction=r.i16();value.mode=r.u8();
    value.returned=mc::Game::readItem(r);r.requireEnd();return value;
}
void layouts() {
    mc::Game game;auto player=mc::InventoryScreen::layout(game);
    require(player.player&&player.slots.size()==45,"player layout includes crafting,armor,inventory,hotbar");
    require(player.hit(145,37)==0&&player.hit(89,27)==1&&player.hit(9,9)==5,"original output/crafting/helmet coordinates");
    require(player.hit(9,85)==9&&player.hit(153,143)==44,"original inventory and hotbar coordinates");
    require(player.hit(7,84)==9&&player.hit(25,84)==10,"GuiContainer one-pixel slot hit margins");
    require(player.hit(50,50)==-1&&player.hit(-1,50)==-999,"panel interior distinct from outside drop target");
    game.window.open=true;game.window.type="minecraft:chest";
    for(int rows=1;rows<=6;++rows) {
        game.window.slots.resize(rows*9+36);auto layout=mc::InventoryScreen::layout(game);
        require(layout.height==114+18*rows&&layout.chestRows==rows,"chest height follows original row formula");
        require(layout.slots.size()==std::size_t(rows*9+36),"all chest/player slots represented");
        require(layout.hit(9,19)==0&&layout.hit(9,rows*18+32)==rows*9,"chest/player boundary slot numbers");
        require(layout.hit(153,rows*18+90)==rows*9+35,"chest hotbar last slot index");
    }
    for(auto pair:std::vector<std::pair<std::string,int>>{{"minecraft:crafting_table",10},{"minecraft:furnace",3},{"minecraft:brewing_stand",4},
             {"minecraft:hopper",5},{"minecraft:dispenser",9},{"minecraft:anvil",3},{"minecraft:enchanting_table",2},{"minecraft:beacon",1},{"minecraft:villager",3},{"EntityHorse",17}}) {
        game.window.type=pair.first;game.window.slots.resize(pair.second+36);auto layout=mc::InventoryScreen::layout(game);
        require(layout.supported&&layout.containerSlots==pair.second,"known container layout uses protocol slot count");
        std::vector<int> indices;for(auto& slot:layout.slots){indices.push_back(slot.index);require(layout.hit(float(slot.x+8),float(slot.y+8))==slot.index,"slot centre selects same index");}
        std::sort(indices.begin(),indices.end());for(int i=0;i<pair.second+36;++i)require(indices[std::size_t(i)]==i,"every container protocol slot appears once");
    }
}
void interactions() {
    mc::Game game;game.online=game.joined=game.spawned=true;
    std::vector<mc::net::Packet> packets;game.outgoingObserver=[&](const auto& packet){packets.push_back(packet);};
    mc::Controller controller;mc::InventoryScreen screen;
    game.inventory[9]=item(1,31);
    auto input=at(game,9);input.rightPressed=true;input.rightDown=true;screen.interact(game,controller,input);
    require(game.cursor.count==16&&game.inventory[9].count==15,"right click picks up rounded-up half through Controller");
    auto first=click(packets.back());require(first.window==0&&first.slot==9&&first.button==1&&first.mode==0&&first.returned.count==31,"pickup packet carries source slot snapshot");
    input=at(game,10,2);input.rightPressed=input.rightDown=true;std::size_t before=packets.size();screen.interact(game,controller,input);
    require(packets.size()==before,"cursor placement waits for release to permit drag splitting");
    input=at(game,10,2.1);input.rightReleased=true;screen.interact(game,controller,input);
    require(game.inventory[10].count==1&&game.cursor.count==15,"right release deposits one item");
    auto place=click(packets.back());require(place.slot==10&&place.mode==0&&place.button==1&&place.returned.empty(),"empty-slot placement packet");
    input={};input.x=-10;input.leftPressed=true;input.timeSeconds=3;screen.interact(game,controller,input);
    require(game.cursor.empty()&&click(packets.back()).slot==-999,"outside left click drops carried stack");
    input=at(game,9,4);input.shift=input.leftPressed=true;screen.interact(game,controller,input);
    require(game.inventory[9].empty()&&game.inventory[36].count==15&&click(packets.back()).mode==1,"shift click transfers main inventory to hotbar");
    game.inventory[9]=item(2,7);input=at(game,9,5);input.numberSlot=0;screen.interact(game,controller,input);
    require(game.inventory[9].id==1&&game.inventory[36].id==2&&click(packets.back()).mode==2,"number key swaps actual hovered and hotbar slots");
    input=at(game,9,6);input.dropPressed=true;screen.interact(game,controller,input);
    require(game.inventory[9].count==14&&click(packets.back()).mode==4,"Q drops one item from hovered slot");
    input.control=true;screen.interact(game,controller,input);require(game.inventory[9].empty()&&click(packets.back()).button==1,"Ctrl-Q drops full hovered stack");
    for(auto& value:game.inventory)value={};
    game.cursor=item(1,10);screen.reset();controller.reset();packets.clear();
    input=at(game,9,8);input.leftPressed=input.leftDown=true;screen.interact(game,controller,input);
    input=at(game,10,8.1);input.leftDown=true;screen.interact(game,controller,input);
    input.leftDown=false;input.leftReleased=true;screen.interact(game,controller,input);
    require(game.inventory[9].count==5&&game.inventory[10].count==5&&game.cursor.empty(),"drag distributes actual carried stack without inventing items");
    require(packets.size()==4,"drag emits start,slot,slot,end packets");
    const int expectedButtons[]={0,1,1,2};for(int i=0;i<4;++i){auto c=click(packets[std::size_t(i)]);require(c.mode==5&&c.button==expectedButtons[i],"drag protocol phase encoding");}
    game.inventory[9]=item(1,10);game.inventory[10]=item(1,4);screen.reset();controller.reset();packets.clear();
    input=at(game,9,10);input.leftPressed=true;screen.interact(game,controller,input);
    input.timeSeconds=10.1;screen.interact(game,controller,input);
    require(game.cursor.count==14&&game.inventory[10].empty()&&click(packets.back()).mode==6,"double click collects matching items");
    game.cursor={};game.window.open=true;game.window.id=7;game.window.type="minecraft:chest";game.window.slots.assign(63,{});game.window.slots[0]=item(264,7);packets.clear();
    input=at(game,0,12);input.rightPressed=true;screen.interact(game,controller,input);
    auto chest=click(packets.back());require(chest.window==7&&chest.slot==0&&game.cursor.count==4,"chest click uses server window ID and server slot zero");
    game.window.type="unported:container";packets.clear();input=at(game,0,13);input.leftPressed=true;screen.interact(game,controller,input);
    require(packets.empty(),"unsupported container rejects UI mutations");
}
void creative(const std::string& assets) {
    mc::InventoryScreen screen;std::string error;require(screen.load(assets,error),error.c_str());
    require(screen.creativeAvailable()&&screen.creativeItemCount(0)==158&&screen.creativeItemCount(5)==599,"original building and search subitem catalog");
    mc::I18n tr;require(tr.load(assets,"en_US"),"load source language");
    require(screen.itemName(item(276,1),tr)=="Diamond Sword","source translation key differs from registry identifier");
    mc::nbt::Tag tag;tag.type=10;tag.compound["display"].type=10;tag.compound["display"].compound["Name"]=mc::nbt::text("Named sword");
    auto custom=item(276,1);custom.nbt=mc::nbt::encode(tag);require(screen.itemName(custom,tr)=="Named sword","NBT display name has precedence");
    mc::Game game;game.online=game.joined=game.spawned=true;game.gameMode=1;game.creativeMode=true;
    mc::Controller controller;std::vector<mc::net::Packet> packets;game.outgoingObserver=[&](const auto& packet){packets.push_back(packet);};
    mc::InventoryInput input;input.x=17;input.y=26;input.leftPressed=true;input.timeSeconds=20;
    screen.interact(game,controller,input);require(game.cursor.id==1&&game.cursor.count==1&&packets.empty(),"creative catalog pickup is local cursor selection");
    input.leftPressed=false;input.numberSlot=2;screen.interact(game,controller,input);
    require(game.inventory[38].id==1&&game.inventory[38].count==64&&packets.size()==1&&packets[0].id==0x10,"creative number key sends authorized C10 full stack");
    {mc::net::Reader reader(packets[0].data);require(reader.i16()==38,"C10 uses player hotbar slot");auto sent=mc::Game::readItem(reader);require(sent.id==1&&sent.count==64,"C10 serializes selected catalog stack");reader.requireEnd();}
    packets.clear();auto transaction=game.nextWindowTransaction();
    input={};input.x=17;input.y=120;input.leftPressed=input.leftDown=true;input.timeSeconds=22;screen.interact(game,controller,input);
    input.leftPressed=input.leftDown=false;input.leftReleased=true;screen.interact(game,controller,input);
    require(game.inventory[36].id==1&&game.inventory[36].count==1&&game.cursor.empty(),"creative hotbar placement uses local slot prediction");
    require(packets.size()==1&&packets[0].id==0x10,"creative placement emits C10 without C0E");
    require(game.nextWindowTransaction()==transaction+1,"creative local prediction does not consume C0E transaction numbers");
    screen.reset();controller.reset();packets.clear();for(auto& value:game.inventory)value={};game.cursor=item(1,10);
    input={};input.x=17;input.y=120;input.leftPressed=input.leftDown=true;input.timeSeconds=24;screen.interact(game,controller,input);
    input.leftPressed=false;input.x=35;screen.interact(game,controller,input);input.leftDown=false;input.leftReleased=true;screen.interact(game,controller,input);
    require(game.inventory[36].count==5&&game.inventory[37].count==5&&game.cursor.empty(),"creative drag predicts inventory without phantom cursor");
    require(packets.size()==11&&std::all_of(packets.begin(),packets.end(),[](const auto& p){return p.id==0x10;}),"creative drag synchronizes visited slots and all nine final hotbar slots as C10");
    for(int i=0;i<11;++i){mc::net::Reader body(packets[std::size_t(i)].data);require(body.i16()==(i<2?36+i:34+i),"creative drag C10 source slot order");}
    packets.clear();game.gameMode=0;game.creativeMode=false;game.cursor={};screen.reset();
    input={};input.x=17;input.y=26;input.leftPressed=true;input.timeSeconds=26;screen.interact(game,controller,input);
    require(std::none_of(packets.begin(),packets.end(),[](const auto& p){return p.id==0x10;}),"survival never emits creative slot packets");
    require(game.cursor.empty(),"survival cannot take items from creative catalog");
}
void controls(const std::string& assets) {
    mc::InventoryScreen screen;std::string error;require(screen.load(assets,error),error.c_str());
    mc::Game game;game.online=game.joined=game.spawned=true;game.experienceLevel=50;
    mc::Controller controller;std::vector<mc::net::Packet> sent;game.outgoingObserver=[&](const auto& packet){sent.push_back(packet);};
    game.window.open=true;game.window.id=5;game.window.type="minecraft:anvil";game.window.slots.assign(39,{});game.window.slots[0]=item(276,1);
    auto buttons=mc::InventoryScreen::controls(game);
    require(buttons.size()==1&&buttons[0].hit(62,24)&&buttons[0].hit(164.9f,35.9f)&&!buttons[0].hit(165,24),"original anvil name field hit bounds");
    mc::InventoryInput input;input.x=63;input.y=25;input.leftPressed=true;screen.interact(game,controller,input);
    require(screen.renameFocused()&&screen.renameText()=="Diamond Sword","anvil initializes source display name and focuses on click");
    require(sent.size()==1&&sent[0].id==0x17,"anvil initialization sends only rename custom payload");
    {mc::net::Reader r(sent.back().data);require(r.string(20)=="MC|ItemName"&&r.string(30).empty(),"ordinary displayed name normalizes to empty rename request");r.requireEnd();}
    input={};input.selectAll=true;screen.interact(game,controller,input);sent.clear();
    input={};input.textInput=U"Blade e";screen.interact(game,controller,input);
    require(screen.renameText()=="Blade e"&&game.window.repairName=="Blade e","anvil text reaches Controller rename state");
    require(sent.size()==1&&sent[0].id==0x17,"typing sends custom payload, not a slot click");
    {mc::net::Reader r(sent.back().data);require(r.string(20)=="MC|ItemName"&&r.string(30)=="Blade e","rename payload exact UTF-8 text");r.requireEnd();}
    require(!game.window.slots[2].empty(),"anvil Controller produces rename result preview");
    input={};input.backspace=true;screen.interact(game,controller,input);require(screen.renameText()=="Blade ","anvil backspace edits at cursor");
    input={};input.homeKey=true;screen.interact(game,controller,input);input.homeKey=false;input.deletePressed=true;screen.interact(game,controller,input);require(screen.renameText()=="lade ","anvil home/delete cursor editing");
    input={};input.selectAll=true;input.textInput=std::u32string(45,U'A');screen.interact(game,controller,input);require(screen.renameText().size()==30,"anvil field obeys original30-character limit");
    input={};input.selectAll=true;input.textInput=U"\u540d\u5b57\U0001f600";screen.interact(game,controller,input);
    require(game.window.repairName==u8"\u540d\u5b57\U0001f600","anvil sends valid non-ASCII UTF-8");
    input={};input.backspace=true;screen.interact(game,controller,input);require(screen.renameText()==u8"\u540d\u5b57","Unicode backspace never splits UTF-8 bytes");
    game.window.slots[3]=item(1,8);sent.clear();input=at(game,3,30);input.leftPressed=true;screen.interact(game,controller,input);
    require(!screen.renameFocused()&&game.cursor.id==1&&sent.back().id==0x0e,"click outside name field returns to real slot interactions");

    screen.reset();controller.reset();game.cursor={};game.window={};game.window.open=true;game.window.id=6;game.window.type="minecraft:villager";game.window.slots.assign(39,{});
    mc::TradeOffer first;first.first=item(388,1);first.result=item(297,2);mc::TradeOffer second=first;second.result=item(264,1);game.window.trades={first,second};
    buttons=mc::InventoryScreen::controls(game);require(buttons.size()==2&&!buttons[0].enabled&&buttons[1].enabled,"merchant previous/next enable states");
    require(buttons[1].hit(147,23)&&!buttons[1].hit(159,23),"source merchant arrow hit bounds");
    sent.clear();input={};input.x=148;input.y=24;input.leftPressed=true;screen.interact(game,controller,input);
    require(game.window.selectedTrade==1&&sent.size()==1&&sent[0].id==0x17,"merchant next changes selected offer through Controller");
    {mc::net::Reader r(sent.back().data);require(r.string(20)=="MC|TrSel"&&r.i32()==1,"merchant custom payload index");r.requireEnd();}
    sent.clear();screen.interact(game,controller,input);require(sent.empty()&&game.window.selectedTrade==1,"disabled merchant next does not send packets");
    input.x=18;screen.interact(game,controller,input);require(game.window.selectedTrade==0,"merchant previous selects prior offer");

    screen.reset();controller.reset();game.window={};game.window.open=true;game.window.id=9;game.window.type="minecraft:enchanting_table";game.window.slots.assign(38,{});
    game.window.slots[0]=item(276,1);game.window.slots[1]=item(351,3);game.window.slots[1].damage=4;game.window.properties={{0,1},{1,15},{2,30}};game.experienceLevel=30;
    buttons=mc::InventoryScreen::controls(game);require(buttons.size()==3&&buttons[0].enabled&&buttons[1].enabled&&buttons[2].enabled,"all affordable enchanting choices enabled");
    require(buttons[1].hit(60,33)&&!buttons[1].hit(168,33)&&!buttons[1].hit(60,52),"original enchanting choice hit bounds");
    sent.clear();input={};input.x=61;input.y=34;input.leftPressed=true;screen.interact(game,controller,input);
    require(sent.size()==1&&sent[0].id==0x11,"enchant click sends C11 instead of C0E");
    {mc::net::Reader r(sent[0].data);require(r.u8()==9&&r.u8()==1,"C11 contains current window ID and choice");r.requireEnd();}
    require(game.experienceLevel==30&&game.window.slots[1].count==3,"UI does not spend levels or lapis before server result");
    game.experienceLevel=5;sent.clear();input.y=53;screen.interact(game,controller,input);require(sent.empty(),"insufficient enchantment level blocks action");
    game.experienceLevel=30;game.window.slots[1].count=1;screen.interact(game,controller,input);require(sent.empty(),"insufficient lapis blocks action");
    game.creativeMode=true;screen.interact(game,controller,input);require(sent.size()==1&&sent[0].id==0x11,"creative enchantment capability bypasses payment check only");

    screen.reset();controller.reset();game.creativeMode=false;game.window={};game.window.open=true;game.window.id=10;game.window.type="minecraft:beacon";game.window.slots.assign(37,{});
    game.window.slots[0]=item(388,1);game.window.properties={{0,1},{1,0},{2,0}};sent.clear();
    input={};input.x=165;input.y=108;input.leftPressed=true;screen.interact(game,controller,input);require(sent.empty()&&!screen.requestedClose(),"beacon confirm requires selected primary power");
    input.x=54;input.y=48;screen.interact(game,controller,input);require(game.window.properties[1]==0,"beacon tier locks unavailable primary power");
    input.y=23;screen.interact(game,controller,input);require(game.window.properties[1]==1&&sent.empty(),"beacon selection remains local until confirm");
    game.window.properties[0]=4;input.x=169;input.y=48;screen.interact(game,controller,input);require(game.window.properties[2]==1,"level four enables primary power upgrade");
    input.x=165;input.y=108;screen.interact(game,controller,input);require(sent.size()==1&&sent[0].id==0x17&&screen.requestedClose(),"beacon confirm sends payload and asks caller to close");
    {mc::net::Reader r(sent[0].data);require(r.string(20)=="MC|Beacon"&&r.i32()==1&&r.i32()==1,"beacon primary/secondary payload order");r.requireEnd();}
    screen.reset();sent.clear();input.x=191;screen.interact(game,controller,input);require(screen.requestedClose()&&sent.empty(),"beacon cancel closes without applying effects");
}
}
int main(int argc,char** argv){try{const std::string assets=argc>1?argv[1]:"assets";layouts();interactions();creative(assets);controls(assets);std::cout<<checks<<" inventory GUI checks passed without graphics or server connection.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
