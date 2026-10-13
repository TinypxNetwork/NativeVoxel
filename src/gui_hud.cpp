#include "mc/gui_hud.hpp"
#include "mc/renderer.hpp"
#include "mc/gui_nbt.hpp"
#include "mc/item_nbt.hpp"
#include "mc/item_names.hpp"
#include "mc/item_tooltip.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <iomanip>

namespace mc::hud {
namespace {
const std::string Section="\xc2\xa7";
void rectangle(Layout& out,float x,float y,float width,float height,std::uint32_t rgba){Draw value;value.kind=Draw::Kind::Rectangle;value.bounds={x,y,width,height};value.rgba=rgba;out.draws.push_back(std::move(value));}
void label(Layout& out,std::string text,float x,float y,float scale=1,std::uint32_t rgba=0xffffffff,bool shadow=true){Draw value;value.kind=Draw::Kind::Text;value.text=std::move(text);value.bounds={x,y,0,9*scale};value.scale=scale;value.rgba=rgba;value.shadow=shadow;out.draws.push_back(std::move(value));}
void sprite(Layout& out,float x,float y,float width,float height,float u,float v,float sourceWidth,float sourceHeight,const std::string& texture="hud_icons"){Draw value;value.kind=Draw::Kind::Sprite;value.bounds={x,y,width,height};value.texture=texture;value.u=u;value.v=v;value.sourceWidth=sourceWidth;value.sourceHeight=sourceHeight;out.draws.push_back(std::move(value));}
int scoreValue(const Game& game,const std::string& objective,const std::string& name){const auto scores=game.scores.find(objective);if(scores==game.scores.end())return 0;const auto found=scores->second.find(name);return found==scores->second.end()?0:found->second;}
int insensitiveCompare(const std::string& a,const std::string& b){std::string x=a,y=b;for(char& c:x)if(c>='A'&&c<='Z')c=static_cast<char>(c+32);for(char& c:y)if(c>='A'&&c<='Z')c=static_cast<char>(c+32);return x<y?-1:x>y?1:0;}
std::string healthText(int points){std::ostringstream out;out<<points/2<<(points%2?".5":".0");return out.str();}
std::int64_t elapsed(std::uint64_t now,std::uint64_t before){return now>=before?static_cast<std::int64_t>(std::min<std::uint64_t>(now-before,1000000000)): -static_cast<std::int64_t>(std::min<std::uint64_t>(before-now,1000000000));}
std::string hoverItemName(const ItemStack& item,const std::string& assets,const I18n& tr){
    if(const auto name=variantItemName(item,tr))return *name;
    using Names=std::map<std::uint32_t,std::pair<std::string,std::string>>;static std::map<std::string,Names> caches;
    auto found=caches.find(assets);if(found==caches.end()){if(caches.size()>=4)caches.clear();Names names;std::ifstream input(assets+"/runtime/items/names.txt");std::string line;while(std::getline(input,line)){std::istringstream fields(line);std::string id,meta,key,fallback;if(!std::getline(fields,id,'\t')||!std::getline(fields,meta,'\t')||!std::getline(fields,key,'\t'))continue;std::getline(fields,fallback);if(!fallback.empty()&&fallback.back()=='\r')fallback.pop_back();try{names[(unsigned(std::stoi(id))<<16)|std::uint16_t(std::stoi(meta))]={key,fallback};}catch(const std::exception&){}}found=caches.emplace(assets,std::move(names)).first;}
    const int metadata=registry::item(item.id).maxDamage?0:item.damage;for(int variant:{metadata,0,-1}){const auto name=found->second.find((unsigned(item.id)<<16)|std::uint16_t(variant));if(name!=found->second.end()){const auto value=tr(name->second.first);return value!=name->second.first?value:name->second.second;}}
    return registry::item(item.id).name;
}
int entityTypeId(const std::string& name){static const std::map<std::string,int> ids{
    {"Item",1},{"XPOrb",2},{"ThrownEgg",7},{"LeashKnot",8},{"Painting",9},{"Arrow",10},{"Snowball",11},{"Fireball",12},{"SmallFireball",13},{"ThrownEnderpearl",14},{"EyeOfEnderSignal",15},{"ThrownPotion",16},{"ThrownExpBottle",17},{"ItemFrame",18},{"WitherSkull",19},{"PrimedTnt",20},{"FallingSand",21},{"FireworksRocketEntity",22},{"ArmorStand",30},{"MinecartCommandBlock",40},{"Boat",41},{"MinecartRideable",42},{"MinecartChest",43},{"MinecartFurnace",44},{"MinecartTNT",45},{"MinecartHopper",46},{"MinecartSpawner",47},{"Mob",48},{"Monster",49},{"Creeper",50},{"Skeleton",51},{"Spider",52},{"Giant",53},{"Zombie",54},{"Slime",55},{"Ghast",56},{"PigZombie",57},{"Enderman",58},{"CaveSpider",59},{"Silverfish",60},{"Blaze",61},{"LavaSlime",62},{"EnderDragon",63},{"WitherBoss",64},{"Bat",65},{"Witch",66},{"Endermite",67},{"Guardian",68},{"Pig",90},{"Sheep",91},{"Cow",92},{"Chicken",93},{"Squid",94},{"Wolf",95},{"MushroomCow",96},{"SnowMan",97},{"Ozelot",98},{"VillagerGolem",99},{"EntityHorse",100},{"Rabbit",101},{"Villager",120},{"EnderCrystal",200}};const auto found=ids.find(name);return found==ids.end()?90:found->second;}
std::vector<std::string> itemHover(const nbt::Tag& root,const std::string& assets,const I18n& tr,bool advanced){
    auto stackRoot=root;stackRoot.compound["Count"]=nbt::integer(1,1);
    const auto item=item_nbt::read(stackRoot);if(item.empty())throw net::ProtocolError("Invalid hover item");
    return item_tooltip::item(item,tr,[&](const ItemStack& stack){return hoverItemName(stack,assets,tr);},advanced);
}
}
void Layout::append(Layout other){draws.insert(draws.end(),std::make_move_iterator(other.draws.begin()),std::make_move_iterator(other.draws.end()));hits.insert(hits.end(),std::make_move_iterator(other.hits.begin()),std::make_move_iterator(other.hits.end()));}
const Hit* Layout::hit(float x,float y)const{for(auto i=hits.rbegin();i!=hits.rend();++i)if(i->bounds.contains(x,y))return &*i;return nullptr;}
const ScoreTeam* playerTeam(const Game& game,const std::string& name,std::string* registered){for(const auto& pair:game.teams)if(std::find(pair.second.members.begin(),pair.second.members.end(),name)!=pair.second.members.end()){if(registered)*registered=pair.first;return &pair.second;}if(registered)registered->clear();return nullptr;}
std::string teamName(const Game& game,const std::string& name){const auto* team=playerTeam(game,name);return team?team->prefix+name+team->suffix:name;}
NameTagPolicy nameTagPolicy(const NameTagInput& input){
    NameTagPolicy out;const bool sameTeam=!input.entityTeam.empty()&&input.entityTeam==input.viewerTeam;
    const bool invisibleToViewer=input.invisible&&!input.viewerSpectator&&!(sameTeam&&input.seeFriendlyInvisibles);
    out.bodyAlpha=!input.invisible?1.f:invisibleToViewer?0.f:.15f;
    if(input.player&&!input.sameAsLocalPlayer&&!input.entityTeam.empty()){
        if(input.visibility=="never")out.visible=false;
        else if(input.visibility=="hideForOtherTeams")out.visible=input.viewerTeam.empty()||sameTeam;
        else if(input.visibility=="hideForOwnTeam")out.visible=input.viewerTeam.empty()||!sameTeam;
        else out.visible=true;
    }else out.visible=input.guiEnabled&&!input.sameAsViewEntity&&!invisibleToViewer&&!input.ridden;
    const float distance=input.sneaking?32.f:64.f;out.visible=out.visible&&input.distanceSquared<static_cast<double>(distance*distance);
    out.heightOffset=input.entityHeight+.5f-(input.child?input.entityHeight/2:0);
    if(input.sneaking){out.pixelOffsetY=9.374999f;out.passes={{true,true,true,32.f/255.f}};}
    else out.passes={{false,false,true,32.f/255.f},{true,true,false,1.f}};
    return out;
}
int titleAlpha(const Game& game,float partial){if(game.titleTicksRemaining<=0)return 0;const float remaining=static_cast<float>(game.titleTicksRemaining)-partial;int alpha=255;
    if(static_cast<std::int64_t>(game.titleTicksRemaining)>static_cast<std::int64_t>(game.titleFadeOut)+game.titleStay&&game.titleFadeIn>0){const float elapsed=static_cast<float>(static_cast<std::int64_t>(game.titleFadeIn)+game.titleStay+game.titleFadeOut)-remaining;alpha=static_cast<int>(elapsed*255.f/static_cast<float>(game.titleFadeIn));}
    if(game.titleTicksRemaining<=game.titleFadeOut&&game.titleFadeOut>0)alpha=static_cast<int>(remaining*255.f/static_cast<float>(game.titleFadeOut));return std::clamp(alpha,0,255);}
int pingIcon(int ping){return ping<0?5:ping<150?0:ping<300?1:ping<600?2:ping<1000?3:4;}
int Hud::chatWidth(float setting){return static_cast<int>(std::floor(setting*280.f+40.f));}
int Hud::chatHeight(float setting){return static_cast<int>(std::floor(setting*160.f+20.f));}
bool Hud::load(const std::string& assets,const std::string& language,std::string& error){assets_=assets;const bool translated=translations_.load(assets,language);fontReady_=font_.load(assets,error);unicode_=translations_.unicode();chatRevision_=~std::uint64_t(0);if(!translated&&error.empty())error="HUD language is unavailable";return fontReady_&&translated;}
bool Hud::registerTextures(Renderer& renderer)const{return renderer.registerUITexture("hud_icons",assets_+"/runtime/gui/icons.rgba")&&renderer.registerUITexture("hud_widgets",assets_+"/runtime/gui/widgets.rgba");}
mc::chat::Document Hud::text(const std::string& json)const{return mc::chat::parse(json,[&](const std::string& key){return translations_(key);});}
Layout Hud::chat(const Game& game,int width,int height,const ChatOptions& options)const{
    (void)width;Layout out;if(options.hidden)return out;const auto font=metrics();const float scale=std::clamp(options.scale,.01f,1.f);const int pixelWidth=chatWidth(std::clamp(options.widthSetting,0.f,1.f)),wrapWidth=std::max(1,static_cast<int>(std::floor(static_cast<float>(pixelWidth)/scale)));
    if(chatGame_!=&game||chatRevision_!=game.chatRevision||chatCount_!=game.chat.size()||chatWrapWidth_!=wrapWidth||chatColors_!=options.colors||chatUnicode_!=unicode_||chatSystemOnly_!=options.systemOnly){chatLines_.clear();int messages=0;
        for(auto message=game.chat.rbegin();message!=game.chat.rend()&&messages<100&&chatLines_.size()<100;++message){if(message->position==2||(options.systemOnly&&message->position==0))continue;++messages;const auto wrapped=mc::chat::wrap(message->json.empty()?mc::chat::literal(message->text):text(message->json),wrapWidth,font,false,options.colors);for(auto line=wrapped.rbegin();line!=wrapped.rend()&&chatLines_.size()<100;++line)chatLines_.push_back({*line,message->tick});}
        chatGame_=&game;chatRevision_=game.chatRevision;chatCount_=game.chat.size();chatWrapWidth_=wrapWidth;chatColors_=options.colors;chatUnicode_=unicode_;chatSystemOnly_=options.systemOnly;
    }
    const int rows=chatHeight(options.open?options.focusedHeight:options.unfocusedHeight)/9;const int scroll=std::clamp(options.scroll,0,std::max(0,static_cast<int>(chatLines_.size())-rows));const float bottom=static_cast<float>(height-28);int eligible=0;
    const float opacity=std::clamp(options.opacity,0.f,1.f)*.9f+.1f;
    for(int row=0;row<rows&&row+scroll<static_cast<int>(chatLines_.size());++row){const auto& value=chatLines_[static_cast<std::size_t>(row+scroll)];const auto age=elapsed(game.ticks,value.tick);if(age>=200&&!options.open)continue;
        double fade=std::clamp((1-static_cast<double>(age)/200)*10,0.0,1.0);fade*=fade;int alpha=options.open?255:static_cast<int>(255*fade);alpha=static_cast<int>(static_cast<float>(alpha)*opacity);++eligible;if(alpha<=3)continue;
        const float y=bottom-(row*9+8)*scale;rectangle(out,2,bottom-(row*9+9)*scale,(std::ceil(static_cast<float>(pixelWidth)/scale)+4)*scale,9*scale,static_cast<std::uint32_t>(alpha/2));label(out,value.line.formatted(),2,y,scale,0xffffff00u|static_cast<unsigned>(alpha));
        if(options.open){int x=0;for(const auto& span:value.line.spans){const int spanWidth=font.width(options.colors?span.text:mc::chat::stripFormatting(span.text));if(spanWidth>0)out.hits.push_back({{3+x*scale,static_cast<float>(height-27)-(row+1)*9*scale,spanWidth*scale,9*scale},span.style,mc::chat::stripFormatting(span.text),span.component,value.tick});x+=spanWidth;}}
    }
    if(options.open){
        const int all=static_cast<int>(chatLines_.size())*10,shown=eligible*10;
        if(all>0&&all!=shown){const int y=scroll*shown/static_cast<int>(chatLines_.size()),bar=shown*shown/all;const auto alpha=static_cast<unsigned>(y>0?170:96);rectangle(out,-1,bottom-(y+bar)*scale,2*scale,bar*scale,((scroll>0?13382451u:3355562u)<<8)|alpha);rectangle(out,-1+2*scale,bottom-(y+bar)*scale,scale,bar*scale,0xcccccc00u|alpha);}
        if(!options.completionHint.empty()) {const auto hint=font.trim(options.completionHint,std::max(1,width-8));rectangle(out,2,static_cast<float>(height-27),float(font.width(hint)+4),10,0x000000a0);label(out,hint,4,static_cast<float>(height-26),1,0xffffffff);}
        rectangle(out,2,static_cast<float>(height-14),static_cast<float>(width-4),12,0x00000080);
        if(options.selectionEndX>options.selectionStartX)rectangle(out,4+options.selectionStartX,static_cast<float>(height-13),options.selectionEndX-options.selectionStartX,10,0x3366dd90);
        label(out,options.input,4,static_cast<float>(height-12),1,0xffffffff);
        if(options.cursorVisible){if(options.cursorAtEnd)label(out,"_",4+options.cursorX,static_cast<float>(height-12),1,0xffffffff);else rectangle(out,3+options.cursorX,static_cast<float>(height-13),1,10,0xd0d0d0ff);}
    }
    out.bounds={2,bottom-rows*9*scale,static_cast<float>(pixelWidth+4),rows*9*scale};return out;
}
Layout Hud::sidebar(const Game& game,int width,int height,const std::string& localName)const{
    Layout out;std::string objective=game.displayedObjectives[1];if(const auto* team=playerTeam(game,localName);team&&team->color>=0&&team->color<16){const auto& colored=game.displayedObjectives[static_cast<std::size_t>(3+team->color)];if(!colored.empty())objective=colored;}
    const auto definition=game.objectives.find(objective);const auto scores=game.scores.find(objective);if(objective.empty()||definition==game.objectives.end()||scores==game.scores.end())return out;
    std::vector<std::pair<std::string,int>> entries(scores->second.begin(),scores->second.end());std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.second!=b.second?a.second<b.second:insensitiveCompare(b.first,a.first)<0;});const auto allCount=entries.size();entries.erase(std::remove_if(entries.begin(),entries.end(),[](const auto& value){return !value.first.empty()&&value.first[0]=='#';}),entries.end());
    if(entries.size()>15){const auto skip=std::min(entries.size(),allCount-15);entries.erase(entries.begin(),entries.begin()+static_cast<std::ptrdiff_t>(skip));}
    const auto font=metrics();int maximum=font.width(definition->second.displayName);for(const auto& entry:entries)maximum=std::max(maximum,font.width(teamName(game,entry.first)+": "+Section+'c'+std::to_string(entry.second)));
    const int bottom=height/2+static_cast<int>(entries.size())*9/3,left=width-maximum-3,right=width-1;int row=0;
    for(const auto& entry:entries){const int y=bottom-(++row)*9;rectangle(out,static_cast<float>(left-2),static_cast<float>(y),static_cast<float>(right-left+2),9,0x00000050);label(out,teamName(game,entry.first),static_cast<float>(left),static_cast<float>(y),1,0xffffffff,false);const auto points=Section+'c'+std::to_string(entry.second);label(out,points,static_cast<float>(right-font.width(points)),static_cast<float>(y),1,0xffffffff,false);
        if(row==static_cast<int>(entries.size())){rectangle(out,static_cast<float>(left-2),static_cast<float>(y-10),static_cast<float>(right-left+2),9,0x00000060);rectangle(out,static_cast<float>(left-2),static_cast<float>(y-1),static_cast<float>(right-left+2),1,0x00000050);label(out,definition->second.displayName,static_cast<float>(left+maximum/2-font.width(definition->second.displayName)/2),static_cast<float>(y-9),1,0xffffffff,false);}}
    out.bounds={static_cast<float>(left-2),static_cast<float>(bottom-(static_cast<int>(entries.size())+1)*9-1),static_cast<float>(maximum+4),static_cast<float>((entries.size()+1)*9+1)};return out;
}
Layout Hud::tab(const Game& game,int width,const TabOptions& options){
    Layout out;if(!options.visible){tabOpen_=false;return out;}if(!tabOpen_)tabOpenedAt_=options.nowMilliseconds;tabOpen_=true;const auto font=metrics();struct PlayerEntry{const PlayerInfo* player;std::string team,name;};std::vector<PlayerEntry> players;int nameWidth=0,scoreWidth=0;const auto objective=game.displayedObjectives[0];const auto objectiveInfo=game.objectives.find(objective);const bool scores=!objective.empty()&&objectiveInfo!=game.objectives.end(),hearts=scores&&objectiveInfo->second.renderType=="hearts";
    for(const auto& pair:game.playerList){const auto& info=pair.second;std::string registered;playerTeam(game,info.name,&registered);const auto name=!info.displayNameJson.empty()?text(info.displayNameJson).formatted():!info.displayName.empty()?info.displayName:teamName(game,info.name);players.push_back({&info,registered,name});nameWidth=std::max(nameWidth,font.width(name));if(scores&&!hearts)scoreWidth=std::max(scoreWidth,font.width(" "+std::to_string(scoreValue(game,objective,info.name))));}
    std::sort(players.begin(),players.end(),[](const auto& a,const auto& b){const bool sa=a.player->gameMode==3,sb=b.player->gameMode==3;if(sa!=sb)return !sa;if(a.team!=b.team)return a.team<b.team;return a.player->name<b.player->name;});if(players.size()>80)players.resize(80);
    const int count=static_cast<int>(players.size());int columns=1,rows=count;while(rows>20){++columns;rows=(count+columns-1)/columns;}const int scoreSpace=scores?(hearts?90:scoreWidth):0;
    const int cell=std::max(1,std::min(columns*((options.showHeads?9:0)+nameWidth+scoreSpace+13),std::max(1,width-50))/columns),left=width/2-(cell*columns+(columns-1)*5)/2;int y=10,panelWidth=cell*columns+(columns-1)*5;
    auto header=mc::chat::wrapFormatted(game.playerListHeaderJson.empty()?game.playerListHeader:text(game.playerListHeaderJson).formatted(),std::max(1,width-50),font);
    auto footer=mc::chat::wrapFormatted(game.playerListFooterJson.empty()?game.playerListFooter:text(game.playerListFooterJson).formatted(),std::max(1,width-50),font);
    const bool hasHeader=!game.playerListHeaderJson.empty()? !text(game.playerListHeaderJson).plain(false).empty():!game.playerListHeader.empty();const bool hasFooter=!game.playerListFooterJson.empty()?!text(game.playerListFooterJson).plain(false).empty():!game.playerListFooter.empty();
    if(hasHeader)for(const auto& line:header)panelWidth=std::max(panelWidth,font.width(line));if(hasFooter)for(const auto& line:footer)panelWidth=std::max(panelWidth,font.width(line));
    if(hasHeader){rectangle(out,static_cast<float>(width/2-panelWidth/2-1),static_cast<float>(y-1),static_cast<float>(panelWidth+2),static_cast<float>(header.size()*9+1),0x00000080);for(const auto& line:header){label(out,line,static_cast<float>(width/2-font.width(line)/2),static_cast<float>(y));y+=9;}++y;}
    rectangle(out,static_cast<float>(width/2-panelWidth/2-1),static_cast<float>(y-1),static_cast<float>(panelWidth+2),static_cast<float>(rows*9+1),0x00000080);
    for(int index=0;index<count;++index){const auto& entry=players[static_cast<std::size_t>(index)];const auto& player=*entry.player;const int x=left+(index/rows)*(cell+5),rowY=y+(index%rows)*9;rectangle(out,static_cast<float>(x),static_cast<float>(rowY),static_cast<float>(cell),8,0xffffff20);int nameX=x;
        if(options.showHeads){Draw head;head.kind=Draw::Kind::PlayerHead;head.bounds={static_cast<float>(x),static_cast<float>(rowY),8,8};head.uuid=player.uuid;head.text=player.name;head.hat=false;for(const auto& pair:game.entities){const auto& entity=pair.second;if(entity.kind!=EntityKind::Player||entity.uuid!=player.uuid)continue;const auto parts=entity.metadata.find(10);const auto flags=parts==entity.metadata.end()?0:parts->second.integer;head.hat=(flags&64)!=0;head.flipped=(flags&1)!=0&&(player.name=="Dinnerbone"||player.name=="Grumm");break;}out.draws.push_back(std::move(head));nameX+=9;}
        label(out,(player.gameMode==3?Section+'o':std::string{})+entry.name,static_cast<float>(nameX),static_cast<float>(rowY),1,player.gameMode==3?0xffffff90u:0xffffffffu);
        const int scoreLeft=nameX+nameWidth+1,scoreRight=scoreLeft+scoreSpace;
        if(scores&&player.gameMode!=3&&scoreRight-scoreLeft>5){const int value=scoreValue(game,objective,player.name);
            if(!hearts){const auto number=Section+'e'+std::to_string(value);label(out,number,static_cast<float>(scoreRight-font.width(number)),static_cast<float>(rowY));}
            else{auto& state=hearts_[player.uuid];if(state.openedAt==tabOpenedAt_){if(value<state.last){state.changeAt=options.nowMilliseconds;state.blinkUntil=game.ticks+20;}else if(value>state.last){state.changeAt=options.nowMilliseconds;state.blinkUntil=game.ticks+10;}}
                if(elapsed(options.nowMilliseconds,state.changeAt)>1000||state.openedAt!=tabOpenedAt_){state.last=state.displayed=value;state.changeAt=options.nowMilliseconds;}state.openedAt=tabOpenedAt_;state.last=value;
                const int visible=static_cast<int>(std::ceil(static_cast<float>(std::max(value,state.displayed))/2.f)),slots=std::max({value/2,state.displayed/2,10});const bool blink=state.blinkUntil>game.ticks&&((state.blinkUntil-game.ticks)/3)%2==1;
                if(visible>0){const float step=std::min(static_cast<float>(scoreRight-scoreLeft-4)/static_cast<float>(slots),9.f);
                    if(step>3){for(int i=visible;i<slots;++i)sprite(out,scoreLeft+i*step,static_cast<float>(rowY),9,9,blink?25.f:16.f,0,9,9);for(int i=0;i<visible;++i){const float at=scoreLeft+i*step;sprite(out,at,static_cast<float>(rowY),9,9,blink?25.f:16.f,0,9,9);if(blink){if(i*2+1<state.displayed)sprite(out,at,static_cast<float>(rowY),9,9,70,0,9,9);if(i*2+1==state.displayed)sprite(out,at,static_cast<float>(rowY),9,9,79,0,9,9);}if(i*2+1<value)sprite(out,at,static_cast<float>(rowY),9,9,i>=10?160.f:52.f,0,9,9);if(i*2+1==value)sprite(out,at,static_cast<float>(rowY),9,9,i>=10?169.f:61.f,0,9,9);}}
                    else{auto number=healthText(value);if(scoreRight-font.width(number+"hp")>=scoreLeft)number+="hp";const float fraction=std::clamp(static_cast<float>(value)/20.f,0.f,1.f);const auto color=(static_cast<std::uint32_t>((1-fraction)*255)<<24)|(static_cast<std::uint32_t>(fraction*255)<<16)|255;label(out,number,static_cast<float>((scoreRight+scoreLeft)/2-font.width(number)/2),static_cast<float>(rowY),1,color);}
                }
            }
        }
        sprite(out,static_cast<float>(x+cell-11),static_cast<float>(rowY),10,8,0,176.f+pingIcon(player.ping)*8,10,8);
    }
    if(hasFooter){y+=rows*9+1;rectangle(out,static_cast<float>(width/2-panelWidth/2-1),static_cast<float>(y-1),static_cast<float>(panelWidth+2),static_cast<float>(footer.size()*9+1),0x00000080);for(const auto& line:footer){label(out,line,static_cast<float>(width/2-font.width(line)/2),static_cast<float>(y));y+=9;}}
    out.bounds={static_cast<float>(width/2-panelWidth/2-1),9,static_cast<float>(panelWidth+2),static_cast<float>((hasFooter?y:y+rows*9)-9)};return out;
}
Layout Hud::titles(const Game& game,int width,int height,float partial)const{Layout out;const int alpha=titleAlpha(game,partial);if(alpha<=8)return out;const auto title=game.titleJson.empty()?game.title:text(game.titleJson).formatted(),subtitle=game.subtitleJson.empty()?game.subtitle:text(game.subtitleJson).formatted();const auto font=metrics();label(out,title,width/2.f-(font.width(title)/2)*4.f,height/2.f-40,4,0xffffff00u|static_cast<unsigned>(alpha));label(out,subtitle,width/2.f-(font.width(subtitle)/2)*2.f,height/2.f+10,2,0xffffff00u|static_cast<unsigned>(alpha));return out;}
Layout Hud::experience(const Game& game,int width,int height)const{Layout out;if(game.gameMode==1||game.gameMode==3)return out;const int x=width/2-91,y=height-29;sprite(out,static_cast<float>(x),static_cast<float>(y),182,5,0,64,182,5);const int filled=std::clamp(static_cast<int>(game.experienceProgress*183.f),0,183);if(filled>0)sprite(out,static_cast<float>(x),static_cast<float>(y),static_cast<float>(filled),5,0,69,static_cast<float>(filled),5);if(game.experienceLevel>0){const auto value=std::to_string(game.experienceLevel);const int px=(width-metrics().width(value))/2,py=height-35;label(out,value,static_cast<float>(px+1),static_cast<float>(py),1,0x000000ff,false);label(out,value,static_cast<float>(px-1),static_cast<float>(py),1,0x000000ff,false);label(out,value,static_cast<float>(px),static_cast<float>(py+1),1,0x000000ff,false);label(out,value,static_cast<float>(px),static_cast<float>(py-1),1,0x000000ff,false);label(out,value,static_cast<float>(px),static_cast<float>(py),1,0x80ff20ff,false);}return out;}
Layout Hud::actionBar(const Game& game,int width,int height,float partial)const{Layout out;if(game.actionBarTicksRemaining<=0)return out;const float remaining=static_cast<float>(game.actionBarTicksRemaining)-partial;const int alpha=std::clamp(static_cast<int>(remaining*256.f/20.f),0,255);if(alpha>0){const auto value=game.actionBarJson.empty()?game.actionBarText:text(game.actionBarJson).plain(false);label(out,value,static_cast<float>(width/2-metrics().width(value)/2),static_cast<float>(height-68),1,0xffffff00u|static_cast<unsigned>(alpha),false);}return out;}
Layout Hud::tooltip(const Hit& hit,float x,float y,int width,int height)const{return tooltip(hit,x,y,width,height,false);}
Layout Hud::tooltip(const Hit& hit,float x,float y,int width,int height,bool advanced)const{
    Layout out;if(!hit.style.hover)return out;const auto& event=*hit.style.hover;const auto value=text(event.valueJson);std::vector<std::string> rows;
    if(event.action=="show_text")rows=mc::chat::wrapFormatted(value.formatted(),std::max(20,std::min(240,width-20)),metrics());
    else if(event.action=="show_item"){try{const auto root=gui_nbt::Parser(value.plain(false)).parse();rows=itemHover(root,assets_,translations_,advanced);}catch(const std::exception&){rows={Section+'c'+"Invalid Item!"};}}
    else if(event.action=="show_entity"){if(!advanced)return out;try{const auto root=gui_nbt::Parser(value.plain(false)).parse();rows.push_back(gui_nbt::string(root,"name"));if(const auto* type=root.get("type");type&&type->type==8){const auto name=gui_nbt::utf8(type->string);rows.push_back("Type: "+name+" ("+std::to_string(entityTypeId(name))+")");}rows.push_back(gui_nbt::string(root,"id"));}catch(const std::exception&){rows={Section+'c'+"Invalid Entity!"};}}
    else if(event.action=="show_achievement"){
        const auto id=value.plain(false),title=translations_(id);const bool achievement=id.compare(0,12,"achievement.")==0;
        if(title==id||(!achievement&&id.compare(0,5,"stat.")!=0))rows={Section+'c'+"Invalid statistic/achievement!"};
        else{const std::string special="|onARail|flyPig|snipeSkeleton|ghast|theEnd|theEnd2|overkill|fullBeacon|exploreAllBiomes|overpowered|";const bool rare=achievement&&special.find('|'+id.substr(12)+'|')!=std::string::npos;rows.push_back(Section+(rare?'5':'a')+title);rows.push_back(Section+'o'+translations_(achievement?"stats.tooltip.type.achievement":"stats.tooltip.type.statistic"));if(achievement){const auto description=translations_(id+".desc");if(description!=id+".desc"){auto lines=mc::chat::wrapFormatted(description,150,metrics());rows.insert(rows.end(),lines.begin(),lines.end());}}}
    }else return out;
    if(rows.empty())return out;int maxWidth=0;for(const auto& row:rows)maxWidth=std::max(maxWidth,metrics().width(row));float left=x+12,top=y-12;const float boxHeight=8.f+(rows.size()>1?2.f+(rows.size()-1)*10.f:0.f);if(left+maxWidth+6>width)left=x-16-maxWidth;if(top+boxHeight+6>height)top=height-boxHeight-6;left=std::max(4.f,left);top=std::max(4.f,top);
    rectangle(out,left-3,top-4,static_cast<float>(maxWidth+6),boxHeight+8,0x100010f0);rectangle(out,left-2,top-3,static_cast<float>(maxWidth+4),1,0x5000ff50);rectangle(out,left-2,top+boxHeight+2,static_cast<float>(maxWidth+4),1,0x28007f50);rectangle(out,left-3,top-3,1,boxHeight+6,0x5000ff50);rectangle(out,left+maxWidth+2,top-3,1,boxHeight+6,0x5000ff50);int line=0;for(const auto& row:rows){label(out,row,left,top+line*10+(line?2:0));++line;}out.bounds={left-3,top-4,static_cast<float>(maxWidth+6),boxHeight+8};return out;
}
void Hud::draw(Renderer& renderer,const Layout& layout,float scale,const std::function<void(const Draw&,float)>& head){for(const auto& value:layout.draws){const auto& b=value.bounds;switch(value.kind){case Draw::Kind::Rectangle:renderer.rect(b.x*scale,b.y*scale,b.width*scale,b.height*scale,value.rgba);break;case Draw::Kind::Text:renderer.text(value.text,b.x*scale,b.y*scale,value.scale*scale,value.rgba,value.shadow);break;case Draw::Kind::Sprite:renderer.sprite(value.texture,b.x*scale,b.y*scale,b.width*scale,b.height*scale,value.u,value.v,value.sourceWidth,value.sourceHeight,value.rgba);break;case Draw::Kind::PlayerHead:if(head)head(value,scale);break;}}}
void Hud::rememberSent(std::string value){if(value.empty()||(!sent_.empty()&&sent_.back()==value))return;sent_.push_back(std::move(value));if(sent_.size()>100)sent_.erase(sent_.begin());}

} // namespace mc::hud
