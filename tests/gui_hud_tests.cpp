#include "mc/chat_text.hpp"
#include "mc/gui_hud.hpp"
#include "mc/gui_nbt.hpp"
#include "mc/server_list.hpp"
#include "mc/resource_packs.hpp"
#include "mc/renderer.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

// Exercise the actual layouts, font metrics, chat parser and server-list IO
// without requiring a window. Pixel drawing and PNG decoding have separate tests.
namespace mc {
bool Renderer::registerUITexture(const std::string&,const std::string&){return true;}
void Renderer::rect(float,float,float,float,std::uint32_t){}
void Renderer::text(const std::string&,float,float,float,std::uint32_t,bool){}
void Renderer::sprite(const std::string&,float,float,float,float,float,float,float,float,std::uint32_t){}
bool decodeResourcePng(const std::vector<std::uint8_t>&,ResourceImage&,std::string&){return false;}
}
namespace {
int checks=0;
const std::string S="\xc2\xa7";
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
std::vector<std::string> labels(const mc::hud::Layout& layout){std::vector<std::string> out;for(const auto& draw:layout.draws)if(draw.kind==mc::hud::Draw::Kind::Text)out.push_back(mc::chat::stripFormatting(draw.text));return out;}
bool contains(const std::vector<std::string>& rows,const std::string& part){return std::any_of(rows.begin(),rows.end(),[&](const auto& row){return row.find(part)!=std::string::npos;});}
void fontReference(const std::string& assets,const std::string& fixture,const std::string& output){
    mc::UnicodeFont font;std::string error;if(!font.load(assets,error))throw std::runtime_error(error);
    std::ifstream input(fixture,std::ios::binary);std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});mc::net::Reader reader(bytes);mc::net::Writer result;const int count=reader.i32();result.i32(count);
    for(int i=0;i<count;++i){const int width=reader.i32();const bool unicode=reader.u8()!=0;const auto raw=reader.bytes(reader.i32());const std::string text(raw.begin(),raw.end());const auto lines=mc::chat::wrapFormatted(text,width,{&font,unicode,6});result.i32(static_cast<int>(lines.size()));for(const auto& line:lines)result.i32(static_cast<int>(line.size())).bytes(reinterpret_cast<const std::uint8_t*>(line.data()),line.size());}
    reader.requireEnd();std::ofstream out(output,std::ios::binary);out.write(reinterpret_cast<const char*>(result.data.data()),static_cast<std::streamsize>(result.data.size()));if(!out)throw std::runtime_error("Cannot write font reference result");
}
mc::hud::Hit hover(const std::string& action,const std::string& value){mc::hud::Hit hit;hit.style.hover=mc::chat::HoverEvent{action,mc::chat::escapeJsonString(value)};return hit;}
void chat(){
    using namespace mc::chat;Metrics font;
    auto doc=parse(R"({"text":"root","color":"red","bold":true,"clickEvent":{"action":"suggest_command","value":"/join"},"extra":[{"text":" child","bold":false},{"text":" tail","color":"blue"}]})");
    require(doc.valid&&doc.plain()=="root child tail","component content and extras");
    require(doc.spans[0].style.bold&&!doc.spans[1].style.bold&&doc.spans[1].style.color=='c'&&doc.spans[2].style.color=='9',"explicit false overrides inherited formatting");
    require(click(doc.spans[1].style).kind==ActionRequest::Kind::SuggestCommand,"child retains parent click action");
    const auto translation=parse(R"({"translate":"test","with":[{"text":"A","color":"green"},"B"]})",[](const std::string&){return "%2$s: %1$s %%";});
    require(translation.valid&&translation.plain()=="B: A %","indexed translate arguments and escaped percent");
    const auto numeric=parse(R"({"translate":"numeric","with":["2.5","Damage"]})",[](const std::string&){return "+%1$.2f %2$s / %1$d";});require(numeric.valid&&numeric.plain()=="+2.5 Damage / 2.5","LanguageMap normalizes indexed numeric translation formats");
    auto invalid=parse(R"({"text":"x","extra":[]})");require(!invalid.valid&&!invalid.spans[0].style.click,"malformed component is inert visible text");
    auto array=parse(R"([{"text":"one","color":"green"},{"text":"two"}])");require(array.spans[1].style.color=='a',"component array inherits first component style");
    Style style;style.insertion="name";style.click=ClickEvent{"run_command","/help"};require(click(style,true).kind==ActionRequest::Kind::InsertText,"shift insertion takes priority");
    for(const auto& url:{"file:///tmp/a","javascript:alert(1)","https://bad\nurl","without-scheme"}){style.click=ClickEvent{"open_url",url};require(click(style).kind==ActionRequest::Kind::None,"unsafe or invalid URI is rejected");}
    style.click=ClickEvent{"open_url","https://example.com/path"};require(click(style).kind==ActionRequest::Kind::OpenUrl&&click(style,false,false).kind==ActionRequest::Kind::None,"HTTP link honors links setting");
    auto wrapped=wrap(doc,35,font,false,false);std::string plain;
    for(const auto& line:wrapped){plain+=line.plain();require(line.formatted().find(S+'c')==std::string::npos&&line.formatted().find(S+'l')==std::string::npos,"disabled chat colours stay disabled in final draw string");for(const auto& span:line.spans)require(span.style.click.has_value(),"wrapped hit retains original event");}
    require(plain==doc.plain(),"chat wrapping preserves all content including separating spaces");
    auto lines=wrapFormatted(S+"aABC DEF",18,font);require(lines==std::vector<std::string>{S+"aABC",S+"aDEF"},"MOTD carries colour across word wrapping");
    lines=wrapFormatted(S+"lAA "+S+"cBBB",21,font);require(lines.size()==2&&lines[1].find(S+"cBBB")!=std::string::npos,"font wrapper resets bold on colour code");
    require(wrapFormatted("a\nb\n",60,font)==std::vector<std::string>{"a","b"},"GUI wrapper matches Java trailing-line rule");
    require(wrapFormatted("",60,font)==std::vector<std::string>{""},"empty GUI string remains one line");
    require(wrapFormatted("W",1,font)==std::vector<std::string>{"W"},"oversized glyph makes progress");
    const std::string emoji="\xf0\x9f\x98\x80";lines=wrapFormatted(emoji+"x",6,font);std::string combined;for(auto line:lines)combined+=stripFormatting(line);require(combined==emoji+"x","wrapping does not split supplementary UTF-8 bytes");
}
void nbt(){
    using namespace mc;const std::string unicode="\xe6\xb5\x8b\xe8\xaf\x95\xf0\x9f\x98\x80";
    require(gui_nbt::utf8(nbt::modifiedUtf8(unicode))==unicode,"GUI converts modified UTF-8 surrogate pairs");
    const std::string nul("a\0b",3);require(gui_nbt::utf8(nbt::modifiedUtf8(nul))==nul,"GUI converts Java NBT NUL");
    auto root=gui_nbt::Parser(R"({id:"minecraft:diamond_sword",Count:1b,Damage:3s,tag:{display:{Name:"Sword",Lore:[0:"first",1:"second"]},ench:[0:{id:16s,lvl:2s}],Unbreakable:true}})").parse();
    require(root.get("Count")->type==1&&root.get("Damage")->type==2,"SNBT preserves numeric tag types");
    require(root.get("tag")->get("display")->get("Lore")->list.size()==2,"SNBT reads indexed 1.8 lists");
    require(root.get("tag")->get("ench")->list[0].get("lvl")->integer==2,"SNBT reads nested item tags");
    root=gui_nbt::Parser("{array:[1,-2,3],float:1.5f,overflow:128b}").parse();require(root.get("array")->type==11&&root.get("array")->bytes.size()==12,"SNBT integer array format");require(root.get("float")->type==5&&root.get("overflow")->type==8,"SNBT float and out-of-range byte follow original types");
    for(const std::string bad:{"{","{a:","{a:[}","{a:1} trailing","[]"}){bool rejected=false;try{gui_nbt::Parser(bad).parse();}catch(const std::exception&){rejected=true;}require(rejected,"malformed hover NBT rejected");}
    bool rejected=false;try{gui_nbt::Parser(std::string(70,'{')+std::string(70,'}')).parse();}catch(const std::exception&){rejected=true;}require(rejected,"deep hover NBT bounded");
}
void hud(mc::hud::Hud& ui){
    using namespace mc;Game game;game.ticks=10;game.chat.push_back({"",R"({"text":"click","color":"red","clickEvent":{"action":"run_command","value":"/help"}})",0,10});++game.chatRevision;
    hud::ChatOptions options;options.open=true;options.input="/";auto layout=ui.chat(game,427,240,options);require(!layout.hits.empty(),"open chat exposes clickable spans");const auto bounds=layout.hits[0].bounds;const auto* hit=layout.hit(bounds.x+1,bounds.y+1);require(hit&&chat::click(hit->style).value=="/help","hit testing returns real chat event");
    game.ticks=220;options.open=false;layout=ui.chat(game,427,240,options);require(layout.draws.empty(),"closed chat fades out after 200 ticks");options.open=true;layout=ui.chat(game,427,240,options);require(!layout.draws.empty(),"opening chat reveals old history");
    game.displayedObjectives[1]="points";game.objectives["points"]={"Points","integer"};game.scores["points"]={{"A",1},{"B",2},{"#hidden",3}};auto rows=labels(ui.sidebar(game,427,240));require(contains(rows,"Points")&&contains(rows,"A")&&contains(rows,"B")&&!contains(rows,"#hidden"),"sidebar shows objective and excludes hidden scores");
    game.teams["blue"]={"Blue","[B] ","","always",0,9,{"A"}};game.displayedObjectives[12]="team";game.objectives["team"]={"Team","integer"};game.scores["team"]={{"A",9}};rows=labels(ui.sidebar(game,427,240,"A"));require(contains(rows,"Team")&&contains(rows,"[B] A"),"team colour selects matching sidebar slot");
    game.playerList["a"]={"a","Alice","",0,149};game.playerList["b"]={"b","Bob","",3,1000};game.playerList["c"]={"c","Carol","",0,150};game.playerListHeaderJson=R"({"text":"Header","color":"gold"})";layout=ui.tab(game,427,{true,true,100});rows=labels(layout);require(rows.size()>=4&&rows[0]=="Header"&&rows[1]=="Alice"&&rows[2]=="Carol"&&rows[3]=="Bob","Tab header and spectator-last ordering");
    int heads=0;for(const auto& draw:layout.draws)if(draw.kind==hud::Draw::Kind::PlayerHead){++heads;require(!draw.hat,"Tab cannot invent hat visibility without an entity");}require(heads==3,"Tab contains one portrait per player");
    require(hud::pingIcon(-1)==5&&hud::pingIcon(149)==0&&hud::pingIcon(150)==1&&hud::pingIcon(600)==3&&hud::pingIcon(1000)==4,"original ping thresholds");
    for(int i=0;i<100;++i)game.playerList[std::to_string(i)]={std::to_string(i),"Player"+std::to_string(i),"",0,1};layout=ui.tab(game,427,{true,true,100});heads=0;for(const auto& draw:layout.draws)if(draw.kind==hud::Draw::Kind::PlayerHead)++heads;require(heads==80,"Tab caps at 80 players");
    game.titleFadeIn=10;game.titleStay=70;game.titleFadeOut=20;game.titleTicksRemaining=100;require(hud::titleAlpha(game,0)==0&&hud::titleAlpha(game,.5f)==12,"title initial fade and partial ticks");game.titleTicksRemaining=80;require(hud::titleAlpha(game,0)==255,"title stay opacity");game.titleTicksRemaining=10;require(hud::titleAlpha(game,0)==127,"title fade out opacity");
    game.gameMode=0;game.experienceProgress=.5f;game.experienceLevel=3;layout=ui.experience(game,427,240);require(layout.draws.size()==7&&layout.draws[1].bounds.width==91,"experience bar fraction and outlined level");game.gameMode=1;require(ui.experience(game,427,240).draws.empty(),"creative has no experience bar");
    rows=labels(ui.tooltip(hover("show_item",R"({id:"minecraft:diamond_sword",tag:{display:{Name:"Named sword",Lore:["A legend"]},ench:[{id:16s,lvl:2s}]}})"),40,40,427,240));require(contains(rows,"Named sword")&&contains(rows,"A legend")&&contains(rows,"Sharpness II"),"item hover reads display names lore and enchantments");
    rows=labels(ui.tooltip(hover("show_item","{id:264}"),40,40,427,240));require(contains(rows,"Diamond"),"item hover accepts the vanilla id-only item compound");
    rows=labels(ui.tooltip(hover("show_item","{id:9999}"),40,40,427,240));require(contains(rows,"Invalid Item!"),"invalid hover item is visibly reported");
    rows=labels(ui.tooltip(hover("show_item",R"({id:276,tag:{AttributeModifiers:[{AttributeName:"generic.attackDamage",Name:"bonus",Amount:2.5d,Operation:0,UUIDMost:1L,UUIDLeast:2L}],CanDestroy:["minecraft:stone"],CanPlaceOn:["minecraft:dirt"],Unbreakable:1b}})"),40,40,427,240));require(contains(rows,"+2.5 Attack Damage")&&contains(rows,"Can break:")&&contains(rows,"Stone")&&contains(rows,"Can be placed on:")&&contains(rows,"Dirt")&&contains(rows,"Unbreakable"),"item hover applies custom attributes and adventure restrictions");
    rows=labels(ui.tooltip(hover("show_item",R"({id:276,tag:{HideFlags:31,ench:[{id:16s,lvl:2s}],CanDestroy:["minecraft:stone"],Unbreakable:1b}})"),40,40,427,240));require(rows.size()==1,"item hide flags remove the corresponding tooltip sections");
    const auto entity=hover("show_entity",R"({name:"Boss",type:"WitherBoss",id:"abc"})");require(ui.tooltip(entity,40,40,427,240).draws.empty(),"entity hover honors advanced tooltip option");rows=labels(ui.tooltip(entity,40,40,427,240,true));require(contains(rows,"Boss")&&contains(rows,"Type: WitherBoss (64)")&&contains(rows,"abc"),"advanced entity tooltip includes original type id");
    rows=labels(ui.tooltip(hover("show_achievement","achievement.mineWood"),40,40,427,240));require(contains(rows,"Getting Wood")&&contains(rows,"Achievement"),"achievement hover resolves original language strings");
    const auto tooltip=ui.tooltip(hover("show_text","A\nB"),420,230,427,240);require(tooltip.bounds.x>=0&&tooltip.bounds.y>=0&&tooltip.bounds.x+tooltip.bounds.width<=427&&tooltip.bounds.y+tooltip.bounds.height<=240,"tooltip remains inside screen edges");
}
void servers(mc::hud::Hud& ui,const std::string& assets,const std::string& directory){
    using namespace mc;namespace s=serverui;I18n tr;tr.load(assets,"en_US");s::ServerList list;s::Server server;server.name="\xe6\xb5\x8b\xe8\xaf\x95\xf0\x9f\x98\x80";server.address="example.com:25565";server.resourceMode=s::ResourceMode::Enabled;list.servers.push_back(server);std::string error;
    std::filesystem::create_directories(directory);const auto file=directory+"/servers-test.dat";require(list.write(file,error),"server list writes standard uncompressed NBT");s::ServerList copy;require(copy.read(file,error)&&copy.servers[0].name==server.name&&copy.servers[0].resourceMode==server.resourceMode,"server list Unicode and resource policy round trip");
    list.beginPing(0);require(list.servers[0].state==s::PingState::Pinging&&list.servers[0].pingMilliseconds==-2,"status ping starts visibly pending");
    require(list.applyStatus(0,R"({"version":{"name":"1.8.9","protocol":47},"players":{"online":3,"max":20,"sample":[{"name":"Alice"}]},"description":{"text":"Hello","color":"green","extra":[{"text":" World"}]}})",37,error),"real status JSON accepted");require(list.servers[0].state==s::PingState::Ready&&list.servers[0].players=="Alice\n... and 2 more ...","status ping populates players tooltip");
    list.selected=0;auto layout=list.layout(ui,tr,427,240,{});require(layout.rows[0].x==63&&layout.rows[0].y==36,"server list original row origin");require(contains(labels(layout.drawing),"Hello World"),"server MOTD rich text reaches drawing");
    s::Input input;input.mouseX=layout.rows[0].x+24;input.mouseY=layout.rows[0].y+8;input.click=true;list.selected=-1;require(list.interact(layout,input).kind==s::Action::Kind::Join,"first icon click connects an unselected server");
    input={};input.mouseX=layout.rows[0].x+305-10;input.mouseY=layout.rows[0].y+3;layout=list.layout(ui,tr,427,240,input);require(layout.tooltip=="37ms"&&contains(labels(layout.drawing),"37ms"),"server latency hover is actually drawn");
    for(int i=0;i<8;++i){server.name="Server"+std::to_string(i);list.servers.push_back(server);}list.scroll=18;layout=list.layout(ui,tr,427,240,{});require(layout.rows[0].y==18&&contains(labels(layout.drawing),list.servers[0].name),"partly scrolled server remains rendered under the mask");
    input={};input.click=true;input.shift=true;input.mouseX=layout.rows[2].x+5;input.mouseY=layout.rows[2].y+24;require(list.interact(layout,input).kind==s::Action::Kind::Reordered&&list.selected==8,"Shift down-arrow swaps with final server");
    s::ConnectionView connection;connection.phase=s::ConnectionPhase::Failed;connection.message="One\nTwo\nThree";const auto button=s::connectionButton(ui,427,240,connection);require(button.y==142,"disconnected button follows wrapped message height");input={};input.click=true;input.mouseX=button.x+1;input.mouseY=button.y+1;require(s::connectionClicked(ui,427,240,connection,input),"disconnect button layout and hit test agree");
    auto rows=labels(s::connection(ui,tr,427,240,connection));require(contains(rows,"One")&&contains(rows,"Two")&&contains(rows,"Three")&&contains(rows,"Failed to connect to the server"),"disconnected page lays out all reason lines");
}
}
int main(int argc,char** argv){try{const std::string assets=argc>1?argv[1]:"assets",directory=argc>2?argv[2]:"build/agent-ui";if(argc==5&&directory=="--wrap-fixtures"){fontReference(assets,argv[3],argv[4]);return 0;}chat();nbt();mc::hud::Hud ui;std::string error;require(ui.load(assets,"en_US",error),"load original HUD font and language");hud(ui);servers(ui,assets,directory);std::cout<<checks<<" UI/chat/server checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"GUI HUD test failure: "<<e.what()<<'\n';return 1;}}
