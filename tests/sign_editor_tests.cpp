#include "mc/controller.hpp"
#include "mc/gui_sign_input.hpp"
#include "mc/gui_nbt.hpp"
#include <iostream>
#include <stdexcept>

namespace {
int checks{};
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
std::uint64_t position(int x,int y,int z){return ((std::uint64_t(x)&0x3ffffffu)<<38)|((std::uint64_t(y)&4095u)<<26)|(std::uint64_t(z)&0x3ffffffu);}
void open(mc::Game& game,int x,int y,int z){game.consumePacket({0x36,mc::net::Writer().u64(position(x,y,z)).data});}
void inputTests(){
    mc::gui::SignInput input;input.load({});
    auto width=[](const std::string& text){float result=0;for(std::size_t i=0;i<text.size();i=mc::gui::TextInput::next(text,i))result+=text[i]=='i'?2:6;return result;};
    input.insert(std::u32string(16,U'W'),width);require(input.line(0)==std::string(15,'W'),"90-pixel line rejects the sixteenth wide glyph");
    input.moveLine(1);input.insert(std::u32string(46,U'i'),width);require(input.line(1)==std::string(45,'i'),"Narrow glyphs use font width, not a fixed character count");
    input.moveLine(1);input.insert(U"\u4f60\u597d\U0001f600",width);require(input.line(2)==u8"\u4f60\u597d\U0001f600","Sign text accepts BMP and supplementary Unicode");
    input.backspace();require(input.line(2)==u8"\u4f60\u597d","Backspace removes a whole UTF-8 code point");
    input.insert(U"\n\t\x7f\u00a7",width);require(input.line(2)==u8"\u4f60\u597d","Control and formatting characters are filtered like ChatAllowedCharacters");
    input.moveLine(1);input.insert(U"last",width);input.moveLine(1);require(input.row()==0,"Down or Enter wraps from the fourth line to the first");
    input.moveLine(-1);require(input.row()==3&&input.line(3)=="last","Up wraps and retains each independent line");
    input.load({{"saved","two","three","four"}});require(input.row()==0&&input.lines()[3]=="four","A newly opened editor starts at the first existing line");
}
void networkTests(){
    mc::Game game;std::vector<mc::net::Packet> sent;game.online=game.joined=game.spawned=true;
    game.outgoingObserver=[&](const mc::net::Packet& packet){sent.push_back(packet);};
    open(game,-17,70,32);
    require(game.editingSign&&game.editingSign->x==-17&&game.editingSign->z==32,"S36 opens an editor with signed coordinates even before its chunk arrives");
    require(game.world.chunks().empty()&&game.signs.empty()&&!game.unhandledPackets.count(0x36),"Temporary editors do not create chunks or retained sign tiles");
    const auto revision=game.signEditorRevision;
    try{game.consumePacket({0x36,mc::net::Writer().u64(position(1,2,3)).u8(1).data});require(false,"Trailing S36 bytes must fail");}catch(const mc::net::ProtocolError&){}
    require(game.signEditorRevision==revision&&game.editingSign->x==-17,"Malformed S36 leaves the active draft unchanged");
    game.editingSign->lines={{u8"\u4f60\u597d\U0001f600","a\"b\\c","", "four"}};
    const auto expected=game.editingSign->lines;
    require(game.finishSignEditing()&&!game.editingSign&&sent.size()==1&&sent[0].id==0x12,"Completing an editor emits C12 once");
    mc::net::Reader body(sent[0].data);require(body.u64()==position(-17,70,32),"C12 preserves the server's packed sign position");
    for(const auto& line:expected)require(mc::Game::plainChat(body.string(384))==line,"C12 encodes all four lines as escaped JSON chat components");body.requireEnd();
    require(game.finishSignEditing()&&sent.size()==1,"Closing the same editor again does not send duplicate updates");
    require(game.world.chunks().empty()&&game.signs.empty(),"Closing an unloaded sign cannot resurrect chunk caches");
    game.world.setBlock(1,65,1,mc::blockState(63));
    mc::SignText sign;sign.x=1;sign.y=65;sign.z=1;sign.lines[0]="existing";game.signs[position(1,65,1)]=sign;
    open(game,1,65,1);require(game.editingSign->lines[0]=="existing","An existing sign supplies its current text");
    game.editingSign->lines[0]="draft";
    mc::net::Writer update;update.u64(position(1,65,1));for(int i=0;i<4;++i)update.string("{\"text\":\"server\"}");game.consumePacket({0x33,update.data});
    require(game.editingSign->lines[0]=="draft"&&game.signs.at(position(1,65,1)).lines[0]=="existing","S33 does not overwrite a sign while its editor owns the draft");
    require(game.finishSignEditing()&&game.signs.at(position(1,65,1)).lines[0]=="draft","Finishing updates the loaded sign's visible text");
    open(game,1,65,1);game.editingSign->lines[0]="old session";const auto before=sent.size();open(game,2,65,1);
    require(sent.size()==before+1&&game.editingSign->x==2,"Opening another editor closes and sends the previous one");
    const auto prior=sent.size();game.disconnect();require(!game.editingSign&&sent.size()==prior,"Disconnect discards drafts without writing to a different session");
    game.online=game.joined=game.spawned=true;open(game,1,65,1);game.editingSign->lines[0]=std::string(400,'a');
    require(!game.finishSignEditing()&&game.editingSign&&sent.size()==prior,"Oversized JSON cannot partially emit C12");
    game.consumePacket({0x07,mc::net::Writer().i32(0).u8(2).u8(0).string("default").data});
    require(!game.editingSign,"Respawn clears the previous player's pending sign editor");
    open(game,1,65,1);game.consumePacket({0x40,mc::net::Writer().string("\"Disconnected\"").data});require(!game.editingSign,"Server disconnect closes the sign editor");
}
void placementTests(){
    auto prepare=[](mc::Game& game){game.joined=game.spawned=true;game.player.teleport(.5,64,.5);game.world.setBlock(2,63,0,mc::blockState(1));game.inventory[36]={323,2,0,{0}};};
    auto place=[](mc::Game& game,int face){mc::Controller control;mc::ControllerInput input;input.usePressed=true;input.target={true,2,63,0,face};control.tick(game,input);};
    mc::Game game;prepare(game);place(game,1);
    require(mc::blockId(game.world.getBlock(2,64,0))==63&&mc::blockMeta(game.world.getBlock(2,64,0))==8,"Local top-face placement creates a standing sign with vanilla yaw rotation");
    require(game.editingSign&&game.inventory[36].count==1,"Local placement opens editing and consumes one survival sign");
    game.editingSign->lines[0]=u8"\u4f60\U0001f600";require(game.finishSignEditing(),"Local editing completes without a connection");
    const auto key=position(2,64,0);const auto tile=mc::nbt::parse(game.tileEntities.at(key).nbt);
    require(tile.get("id")->string=="Sign"&&tile.get("x")->integer==2&&tile.get("y")->integer==64,"Local edits store a standard sign block entity for Anvil snapshots");
    require(mc::Game::plainChat(mc::gui_nbt::utf8(tile.get("Text1")->string))==u8"\u4f60\U0001f600","NBT text uses Java Modified UTF-8 and preserves supplementary Unicode");
    require(game.signs.at(key).lines[0]==u8"\u4f60\U0001f600","Local edits update rendered sign text");
    auto extended=tile;extended.compound["CustomName"]=mc::nbt::text("Preserved");game.tileEntities.at(key).nbt=mc::nbt::encode(extended);
    game.openSignEditor(2,64,0);game.editingSign->lines[1]="next";game.finishSignEditing();
    require(mc::nbt::parse(game.tileEntities.at(key).nbt).get("CustomName")->string=="Preserved","Editing preserves unknown block entity fields");
    mc::Game wall;prepare(wall);wall.gameMode=1;wall.creativeMode=true;place(wall,5);
    require(wall.world.getBlock(3,63,0)==mc::blockState(68,5)&&wall.inventory[36].count==2,"Creative side placement creates a wall sign without consuming the stack");
    mc::Game invalid;prepare(invalid);place(invalid,0);require(!invalid.editingSign&&invalid.inventory[36].count==2,"Signs cannot be placed on an underside");
    invalid.world.setBlock(2,63,0,mc::blockState(9));place(invalid,1);require(!invalid.editingSign&&invalid.inventory[36].count==2,"Sign support must have a solid material");
    invalid.world.setBlock(2,63,0,mc::blockState(1));invalid.world.setBlock(2,64,0,mc::blockState(1));place(invalid,1);require(!invalid.editingSign&&invalid.inventory[36].count==2,"Occupied destinations reject local sign placement");
    mc::Game online;prepare(online);online.online=true;std::vector<mc::net::Packet> sent;online.outgoingObserver=[&](const mc::net::Packet& p){sent.push_back(p);};place(online,1);
    require(!online.editingSign&&online.world.getBlock(2,64,0)==0&&online.inventory[36].count==2,"Online placement waits for the server's block and S36 packets");
    require(std::any_of(sent.begin(),sent.end(),[](const auto& p){return p.id==8;}),"Online sign placement sends the original C08 item-use packet");
    game.openSignEditor(2,64,0);game.world.setBlock(2,64,0,0);game.invalidateTile(2,64,0);game.finishSignEditing();
    require(!game.signs.count(key)&&!game.tileEntities.count(key),"A removed sign cannot be recreated by closing its old editor");
    mc::Game full;prepare(full);full.world.setBlock(2,64,0,mc::blockState(63));full.openSignEditor(2,64,0);full.editingSign->lines[0]="retained draft";
    for(std::uint64_t i=0;i<65536;++i)full.tileEntities[i]={};
    require(!full.finishSignEditing()&&full.editingSign&&full.editingSign->lines[0]=="retained draft"&&!full.signs.count(key),"Tile capacity failure preserves the draft without publishing unsaved text");
}
}
int main(){try{inputTests();networkTests();placementTests();std::cout<<checks<<" sign editor checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<"Sign editor test failure: "<<error.what()<<'\n';return 1;}}
