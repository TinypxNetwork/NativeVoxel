#include "mc/settings.hpp"
#include "mc/gui_inventory.hpp"
#include "mc/item_names.hpp"
#include "mc/item_tooltip.hpp"
#include "mc/controller.hpp"
#include "mc/game.hpp"
#include "mc/i18n.hpp"
#include "mc/platform.hpp"
#include "mc/nbt.hpp"
#include "mc/registry.hpp"
#include "mc/renderer.hpp"
#include "mc/render_fidelity.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace mc {
namespace {
const ItemStack emptyStack{};
bool creativeAllowed(const Game& game){return game.joined&&game.spawned&&game.creativeMode&&game.gameMode==1&&!game.window.open;}
bool equalStack(const ItemStack& a,const ItemStack& b) {
    if(a.empty()&&b.empty())return true;
    if(a.id!=b.id||a.count!=b.count||a.damage!=b.damage)return false;
    if(a.nbt==b.nbt)return true;
    try{return nbt::parse(a.nbt)==nbt::parse(b.nbt);}catch(const net::ProtocolError&){return false;}
}
bool sendCreative(Game& game,int slot,const ItemStack& item) {
    if(!creativeAllowed(game)||slot<-1||slot>=45)return false;
    if((game.online||slot<0)&&!game.setCreativeSlot(slot,item))return false;
    if(slot>=0)game.inventory[slot]=item;
    return true;
}
std::string lowercase(std::string value){for(char& c:value)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return value;}
void appendUtf8(std::string& text,char32_t code) {
    if(code<0x80)text+=char(code);
    else if(code<0x800){text+=char(0xc0|(code>>6));text+=char(0x80|(code&63));}
    else if(code<0x10000){text+=char(0xe0|(code>>12));text+=char(0x80|((code>>6)&63));text+=char(0x80|(code&63));}
    else if(code<=0x10ffff){text+=char(0xf0|(code>>18));text+=char(0x80|((code>>12)&63));text+=char(0x80|((code>>6)&63));text+=char(0x80|(code&63));}
}
void eraseUtf8(std::string& text){if(text.empty())return;std::size_t i=text.size()-1;while(i>0&&(static_cast<unsigned char>(text[i])&0xc0)==0x80)--i;text.resize(i);}
std::size_t previousUtf8(const std::string& text,std::size_t cursor){if(!cursor)return 0;--cursor;while(cursor&&(static_cast<unsigned char>(text[cursor])&0xc0)==0x80)--cursor;return cursor;}
std::size_t nextUtf8(const std::string& text,std::size_t cursor){if(cursor>=text.size())return text.size();++cursor;while(cursor<text.size()&&(static_cast<unsigned char>(text[cursor])&0xc0)==0x80)++cursor;return cursor;}
int javaUnits(const std::string& text){int units=0;for(unsigned char byte:text)if((byte&0xc0)!=0x80)units+=byte>=0xf0?2:1;return units;}
std::string formatOne(std::string text,const std::string& value) {
    for(const std::string spec:{"%1$s","%1$d","%s","%d"}){auto at=text.find(spec);if(at!=std::string::npos){text.replace(at,spec.size(),value);break;}}
    return text;
}
const char* enchantmentKey(int id) {
    switch(id) {
    case 0:return "enchantment.protect.all";case 1:return "enchantment.protect.fire";case 2:return "enchantment.protect.fall";
    case 3:return "enchantment.protect.explosion";case 4:return "enchantment.protect.projectile";case 5:return "enchantment.oxygen";
    case 6:return "enchantment.waterWorker";case 7:return "enchantment.thorns";case 8:return "enchantment.waterWalker";
    case 16:return "enchantment.damage.all";case 17:return "enchantment.damage.undead";case 18:return "enchantment.damage.arthropods";
    case 19:return "enchantment.knockback";case 20:return "enchantment.fire";case 21:return "enchantment.lootBonus";
    case 32:return "enchantment.digging";case 33:return "enchantment.untouching";case 34:return "enchantment.durability";case 35:return "enchantment.lootBonusDigger";
    case 48:return "enchantment.arrowDamage";case 49:return "enchantment.arrowKnockback";case 50:return "enchantment.arrowFire";case 51:return "enchantment.arrowInfinite";
    case 61:return "enchantment.lootBonusFishing";case 62:return "enchantment.fishingSpeed";default:return nullptr;
    }
}
std::string enchantmentName(int id,int level,const I18n& tr) {
    auto key=enchantmentKey(id);if(!key)return {};
    auto result=tr(key);
    if(level!=1||(id!=6&&id!=33&&id!=50&&id!=51))result+=" "+tr("enchantment.level."+std::to_string(level));
    return result;
}
std::array<std::string,3> enchantmentWords(int seed) {
    static const std::vector<std::string> words=[] {
        std::istringstream input("the elder scrolls klaatu berata niktu xyzzy bless curse light darkness fire air earth water hot dry cold wet ignite snuff embiggen twist shorten stretch fiddle destroy imbue galvanize enchant free limited range of towards inside sphere cube self other ball mental physical grow shrink demon elemental spirit animal creature beast humanoid undead fresh stale");
        std::vector<std::string> result;std::string word;while(input>>word)result.push_back(word);return result;
    }();
    std::uint64_t random=(std::uint64_t(std::int64_t(seed))^0x5deece66dull)&((1ull<<48)-1);
    auto bits=[&](){random=(random*0x5deece66dull+11)&((1ull<<48)-1);return std::uint32_t(random>>17);};
    auto next=[&](std::uint32_t n){if((n&(n-1))==0)return std::uint32_t((std::uint64_t(n)*bits())>>31);std::uint32_t b,v;do{b=bits();v=b%n;}while(b-v+n-1>0x7fffffffu);return v;};
    std::array<std::string,3> result;
    for(auto& line:result){int count=int(next(2))+3;for(int i=0;i<count;++i){if(i)line+=' ';line+=words[next(std::uint32_t(words.size()))];}}
    return result;
}
void galactic(Renderer& renderer,const std::string& text,const std::array<std::uint8_t,256>& widths,float x,float y,float scale,std::uint32_t color) {
    float cursor=0;int row=0;std::istringstream input(text);std::string word;
    while(input>>word) {
        int width=0;for(unsigned char c:word)width+=widths[c];
        if(cursor>0&&cursor+4+width>86){cursor=0;++row;}
        if(cursor>0)cursor+=4;
        for(unsigned char c:word) {
            float drawn=std::max(0.f,float(widths[c])-1.01f);
            renderer.sprite("enchant_font",x+cursor*scale,y+row*9*scale,drawn*scale,7.99f*scale,float(c%16*8),float(c/16*8),drawn,7.99f,color);
            cursor+=widths[c];
        }
    }
}
struct ItemDetails {std::string name;std::vector<std::string> lore;int color=-1;bool unbreakable=false;};
const ItemDetails& details(const ItemStack& item) {
    static const ItemDetails empty;
    static std::unordered_map<std::string,ItemDetails> cache;
    if(item.nbt.empty()||item.nbt[0]==0)return empty;
    const std::string key(item.nbt.begin(),item.nbt.end());auto found=cache.find(key);if(found!=cache.end())return found->second;
    ItemDetails value;
    try {
        auto root=nbt::parse(item.nbt);
        if(auto tag=root.get("Unbreakable"))value.unbreakable=tag->integer!=0;
        if(auto display=root.get("display")) {
            if(auto name=display->get("Name");name&&name->type==8)value.name=gui_nbt::utf8(name->string);
            if(auto color=display->get("color"))value.color=int(color->integer)&0xffffff;
            if(auto lore=display->get("Lore");lore&&lore->type==9)for(const auto& line:lore->list) {
                if(line.type==8)value.lore.push_back(gui_nbt::utf8(line.string));
                if(value.lore.size()>=32)break;
            }
        }
    } catch(const net::ProtocolError&){}
    if(cache.size()>=256)cache.clear();
    return cache.emplace(key,std::move(value)).first->second;
}
const ItemStack& stackAt(const Game& game,int slot) {
    if(slot<0)return emptyStack;
    if(game.window.open)return static_cast<std::size_t>(slot)<game.window.slots.size()?game.window.slots[slot]:emptyStack;
    return static_cast<std::size_t>(slot)<game.inventory.size()?game.inventory[slot]:emptyStack;
}
void addPlayer(InventoryLayout& result,int first,int x=8,int y=84,int hotbar=142) {
    for(int row=0;row<3;++row)for(int col=0;col<9;++col)result.slots.push_back({first+row*9+col,x+col*18,y+row*18});
    for(int col=0;col<9;++col)result.slots.push_back({first+27+col,x+col*18,hotbar});
}
std::string translated(const I18n& tr,const std::string& key,const std::string& fallback) {
    auto value=tr(key);return value==key?fallback:value;
}
std::string titleKey(const std::string& type) {
    if(type=="minecraft:crafting_table")return "container.crafting";
    if(type=="minecraft:furnace")return "container.furnace";
    if(type=="minecraft:brewing_stand")return "container.brewing";
    if(type=="minecraft:hopper")return "container.hopper";
    if(type=="minecraft:dispenser")return "container.dispenser";
    if(type=="minecraft:dropper")return "container.dropper";
    if(type=="minecraft:anvil")return "container.repair";
    if(type=="minecraft:enchanting_table")return "container.enchant";
    if(type=="minecraft:beacon")return "container.beacon";
    if(type=="minecraft:villager")return "entity.Villager.name";
    return "container.chest";
}
int property(const Game& game,int id,int fallback=0) {
    auto found=game.window.properties.find(id);return found==game.window.properties.end()?fallback:found->second;
}
void stack(Renderer& renderer,const ItemStack& item,float x,float y,float scale) {
    if(item.empty())return;
    const auto& tags=details(item);
    renderer.itemIcon(item,x,y,16*scale,item.id>=298&&item.id<=301?tags.color:-1);
    const auto& info=registry::item(item.id);
    if(info.maxDamage>0&&item.damage>0&&!tags.unbreakable) {
        float fraction=std::clamp(float(item.damage)/info.maxDamage,0.f,1.f);
        int green=int(std::round(255*(1-fraction))),length=int(std::round(13*(1-fraction)));
        renderer.rect(x+2*scale,y+13*scale,13*scale,2*scale,0x000000ffu);
        renderer.rect(x+2*scale,y+13*scale,12*scale,scale,(std::uint32_t((255-green)/4)<<24)|0x004000ffu);
        if(length>0)renderer.rect(x+2*scale,y+13*scale,length*scale,scale,(std::uint32_t(255-green)<<24)|(std::uint32_t(green)<<16)|255u);
    }
    if(item.count!=1) {
        auto count=std::to_string(int(item.count));
        renderer.text(count,x+17*scale-renderer.textWidth(count,scale),y+9*scale,scale);
    }
}
void tooltip(Renderer& renderer,const std::vector<std::string>& lines,float mx,float my,float scale,int width,int height) {
    if(lines.empty())return;
    float textWidth=0;for(const auto& line:lines)textWidth=std::max(textWidth,renderer.textWidth(line,scale));
    float x=mx+12*scale,y=my-12*scale,h=(lines.size()==1?8:10+10*(lines.size()-1))*scale;
    if(x+textWidth+6*scale>width)x=mx-16*scale-textWidth;
    x=std::max(4*scale,x);if(y+h+6*scale>height)y=height-h-6*scale;y=std::max(4*scale,y);
    renderer.rect(x-3*scale,y-4*scale,textWidth+6*scale,h+8*scale,0x100010f0u);
    renderer.gradient(x-3*scale,y-3*scale,scale,h+6*scale,0x5000ff50u,0x28007f50u);
    renderer.gradient(x+textWidth+2*scale,y-3*scale,scale,h+6*scale,0x5000ff50u,0x28007f50u);
    renderer.rect(x-2*scale,y-3*scale,textWidth+4*scale,scale,0x5000ff50u);
    renderer.rect(x-2*scale,y+h+2*scale,textWidth+4*scale,scale,0x28007f50u);
    for(std::size_t i=0;i<lines.size();++i)renderer.text(lines[i],x,y+(i?2+10*i:0)*scale,scale,i?0xaaaaaaffu:0xffffffffu);
}
} // namespace

struct InventoryScreen::CreativeData {
    struct Entry {ItemStack item;std::string key,name,search;};
    struct Tab {int id=0;std::string key,background;bool title=true,scrollbar=true;ItemStack icon;std::vector<Entry> entries;};
    std::vector<Tab> tabs;
    int selected=0,row=0,filteredTab=-1;
    std::string search,filteredSearch,filteredLanguage;
    std::vector<std::size_t> filtered;
    bool scrolling=false;
    Tab* tab(){for(auto& t:tabs)if(t.id==selected)return &t;return tabs.empty()?nullptr:&tabs[0];}
    const Tab* tab()const{for(const auto& t:tabs)if(t.id==selected)return &t;return tabs.empty()?nullptr:&tabs[0];}
    void rebuild(const InventoryScreen& screen,const I18n& tr) {
        if(filteredTab==selected&&filteredSearch==search&&filteredLanguage==tr.language())return;
        filtered.clear();auto current=tab();if(!current)return;
        auto query=lowercase(search);
        for(std::size_t i=0;i<current->entries.size();++i) {
            const auto& e=current->entries[i];
            if(selected!=5||query.empty()||lowercase(screen.itemName(e.item,tr)+" "+e.search+" "+e.key).find(query)!=std::string::npos)filtered.push_back(i);
        }
        filteredTab=selected;filteredSearch=search;filteredLanguage=tr.language();row=0;
    }
};

int InventoryLayout::hit(float x,float y) const noexcept {
    for(const auto& slot:slots)if(x>=slot.x-1&&x<slot.x+17&&y>=slot.y-1&&y<slot.y+17)return slot.index;
    return x<0||y<0||x>=width||y>=height?-999:-1;
}

std::vector<InventoryControl> InventoryScreen::controls(const Game& game) {
    std::vector<InventoryControl> result;if(!game.window.open)return result;
    const auto& type=game.window.type;
    if(type=="minecraft:anvil")result.push_back({InventoryAction::AnvilName,0,62,24,103,12,!stackAt(game,0).empty(),false});
    else if(type=="minecraft:villager") {
        result.push_back({InventoryAction::PreviousTrade,-1,17,23,12,19,game.window.selectedTrade>0,false});
        result.push_back({InventoryAction::NextTrade,1,147,23,12,19,game.window.selectedTrade+1<int(game.window.trades.size()),false});
    } else if(type=="minecraft:enchanting_table") {
        for(int i=0;i<3;++i) {
            int cost=property(game,i);const auto& lapis=stackAt(game,1);
            bool enabled=cost>0&&!stackAt(game,0).empty()&&(game.creativeMode||(!lapis.empty()&&lapis.count>=i+1&&game.experienceLevel>=cost&&game.experienceLevel>=i+1));
            result.push_back({InventoryAction::Enchant,i,60,14+i*19,108,19,enabled,false});
        }
    } else if(type=="minecraft:beacon") {
        int levels=property(game,0,-1),primary=property(game,1),secondary=property(game,2);
        const int effects[][2]={{1,3},{11,8},{5,0}};
        for(int row=0;row<3;++row) {
            int count=row==2?1:2,span=count*22+(count-1)*2;
            for(int i=0;i<count;++i)result.push_back({InventoryAction::BeaconPrimary,effects[row][i],76+i*24-span/2,22+row*25,22,22,row<levels,primary==effects[row][i]});
        }
        result.push_back({InventoryAction::BeaconSecondary,10,144,47,22,22,levels>=4,secondary==10});
        if(primary>0)result.push_back({InventoryAction::BeaconSecondary,primary,168,47,22,22,levels>=4,secondary==primary});
        result.push_back({InventoryAction::BeaconConfirm,0,164,107,22,22,!stackAt(game,0).empty()&&primary>0,false});
        result.push_back({InventoryAction::BeaconCancel,0,190,107,22,22,true,false});
    }
    return result;
}

void InventoryScreen::syncAnvil(Game& game,Controller& controller,const I18n* language) {
    if(!game.window.open||game.window.type!="minecraft:anvil"){anvilWindowId_=-1;renameFocused_=false;return;}
    const auto& input=stackAt(game,0);
    std::string signature=std::to_string(input.id)+":"+std::to_string(int(input.count))+":"+std::to_string(input.damage)+":"+std::string(input.nbt.begin(),input.nbt.end());
    if(anvilWindowId_==game.window.id&&signature==anvilInputSignature_)return;
    if(anvilWindowId_!=game.window.id)renameFocused_=false;
    anvilWindowId_=game.window.id;anvilInputSignature_=signature;renameText_.clear();renameSelectedAll_=false;
    if(!input.empty()) {
        if(language)renameText_=itemName(input,*language);
        else if(!details(input).name.empty())renameText_=details(input).name;
        else for(int metadata:{int(input.damage),0,-1}) {
            auto found=names_.find((unsigned(input.id)<<16)|std::uint16_t(metadata));
            if(found!=names_.end()){renameText_=found->second.fallback;break;}
        }
        if(renameText_.empty())renameText_=registry::item(input.id).name;
        renameOriginal_=renameText_;
        while(javaUnits(renameText_)>30)eraseUtf8(renameText_);
        renameCursor_=renameText_.size();
        controller.renameItem(game,details(input).name.empty()&&renameText_==renameOriginal_?"":renameText_);
    } else {renameOriginal_.clear();renameCursor_=0;renameFocused_=false;}
}

bool InventoryScreen::interactControls(Game& game,Controller& controller,const InventoryInput& input) {
    syncAnvil(game,controller);
    auto widgets=controls(game);bool mouse=input.leftPressed||input.rightPressed||input.middlePressed;
    if(game.window.open&&game.window.type=="minecraft:anvil") {
        const auto& field=widgets.front();
        if(mouse){renameFocused_=field.enabled&&field.hit(input.x,input.y);if(renameFocused_){renameCursor_=renameText_.size();renameSelectedAll_=false;return true;}}
        if(renameFocused_&&field.enabled&&!mouse) {
            bool consumed=input.selectAll||input.backspace||input.deletePressed||input.leftKey||input.rightKey||input.homeKey||input.endKey||
                std::any_of(input.textInput.begin(),input.textInput.end(),[](char32_t c){return c>=32&&c!=127&&c!=167&&c<=0x10ffff&&!(c>=0xd800&&c<=0xdfff);});
            if(input.selectAll)renameSelectedAll_=true;
            auto eraseSelection=[&](){if(renameSelectedAll_){renameText_.clear();renameCursor_=0;renameSelectedAll_=false;return true;}return false;};
            if(input.backspace&&!eraseSelection()&&renameCursor_){auto previous=previousUtf8(renameText_,renameCursor_);renameText_.erase(previous,renameCursor_-previous);renameCursor_=previous;}
            if(input.deletePressed&&!eraseSelection()&&renameCursor_<renameText_.size())renameText_.erase(renameCursor_,nextUtf8(renameText_,renameCursor_)-renameCursor_);
            if(input.leftKey){renameCursor_=renameSelectedAll_?0:previousUtf8(renameText_,renameCursor_);renameSelectedAll_=false;}
            if(input.rightKey){renameCursor_=renameSelectedAll_?renameText_.size():nextUtf8(renameText_,renameCursor_);renameSelectedAll_=false;}
            if(input.homeKey){renameCursor_=0;renameSelectedAll_=false;}if(input.endKey){renameCursor_=renameText_.size();renameSelectedAll_=false;}
            for(char32_t code:input.textInput)if(code>=32&&code!=127&&code!=167&&code<=0x10ffff&&!(code>=0xd800&&code<=0xdfff)) {
                eraseSelection();std::string added;appendUtf8(added,code);
                if(javaUnits(renameText_)+javaUnits(added)<=30){renameText_.insert(renameCursor_,added);renameCursor_+=added.size();}
            }
            if(consumed)controller.renameItem(game,details(stackAt(game,0)).name.empty()&&renameText_==renameOriginal_?"":renameText_);
            return true;
        }
    }
    if(mouse)for(const auto& widget:widgets)if(widget.action!=InventoryAction::AnvilName&&widget.hit(input.x,input.y)) {
        if(!widget.enabled||(widget.action!=InventoryAction::Enchant&&!input.leftPressed))return true;
        switch(widget.action) {
        case InventoryAction::PreviousTrade:case InventoryAction::NextTrade:controller.selectTrade(game,game.window.selectedTrade+widget.value);break;
        case InventoryAction::Enchant:controller.enchantItem(game,widget.value);break;
        case InventoryAction::BeaconPrimary:game.window.properties[1]=widget.value;break;
        case InventoryAction::BeaconSecondary:game.window.properties[2]=widget.value;break;
        case InventoryAction::BeaconConfirm:
            if(!game.online||game.sendCustomPayload("MC|Beacon",net::Writer().i32(property(game,1)).i32(property(game,2)).data))closeRequested_=true;
            break;
        case InventoryAction::BeaconCancel:closeRequested_=true;break;
        default:break;
        }
        return true;
    }
    return false;
}

InventoryLayout InventoryScreen::layout(const Game& game) {
    InventoryLayout result;
    if(!game.window.open) {
        result.player=true;result.containerSlots=9;
        result.slots.push_back({0,144,36});
        for(int row=0;row<2;++row)for(int col=0;col<2;++col)result.slots.push_back({1+row*2+col,88+col*18,26+row*18});
        for(int i=0;i<4;++i)result.slots.push_back({5+i,8,8+i*18});
        addPlayer(result,9);return result;
    }
    const auto& type=game.window.type;
    auto add=[&](int i,int x,int y){result.slots.push_back({i,x,y});};
    int first=0;
    if(type=="minecraft:chest"||type=="minecraft:container") {
        int count=std::max(9,int(game.window.slots.size())-36);
        result.chestRows=std::clamp((count+8)/9,1,6);first=result.chestRows*9;
        result.texture="chest";result.height=114+result.chestRows*18;result.inventoryLabelY=result.height-94;
        for(int i=0;i<first;++i)add(i,8+i%9*18,18+i/9*18);
        addPlayer(result,first,8,result.chestRows*18+31,result.chestRows*18+89);
    } else if(type=="minecraft:crafting_table") {
        result.texture="crafting";first=10;add(0,124,35);
        for(int i=0;i<9;++i)add(i+1,30+i%3*18,17+i/3*18);
        addPlayer(result,first);
    } else if(type=="minecraft:furnace") {
        result.texture="furnace";first=3;add(0,56,17);add(1,56,53);add(2,116,35);addPlayer(result,first);
    } else if(type=="minecraft:brewing_stand") {
        result.texture="brewing";first=4;add(0,56,51);add(1,79,58);add(2,102,51);add(3,79,17);addPlayer(result,first);
    } else if(type=="minecraft:hopper") {
        result.texture="hopper";result.height=133;result.inventoryLabelY=39;first=5;
        for(int i=0;i<5;++i)add(i,44+i*18,20);
        addPlayer(result,first,8,51,109);
    } else if(type=="minecraft:dispenser"||type=="minecraft:dropper") {
        result.texture="dispenser";first=9;for(int i=0;i<9;++i)add(i,62+i%3*18,17+i/3*18);addPlayer(result,first);
    } else if(type=="minecraft:anvil") {
        result.texture="anvil";first=3;add(0,27,47);add(1,76,47);add(2,134,47);addPlayer(result,first);
    } else if(type=="minecraft:enchanting_table") {
        result.texture="enchanting";first=2;add(0,15,47);add(1,35,47);addPlayer(result,first);
    } else if(type=="minecraft:beacon") {
        result.texture="beacon";result.width=230;result.height=219;first=1;result.inventoryLabelX=36;result.inventoryLabelY=125;
        add(0,136,110);addPlayer(result,first,36,137,195);
    } else if(type=="minecraft:villager") {
        result.texture="villager";first=3;add(0,36,53);add(1,62,53);add(2,120,53);addPlayer(result,first);
    } else if(type=="EntityHorse") {
        result.texture="horse";first=std::clamp(int(game.window.slots.size())-36,2,17);add(0,8,18);add(1,8,36);
        for(int i=2;i<first;++i)add(i,80+(i-2)%5*18,18+(i-2)/5*18);
        addPlayer(result,first);
    } else {
        result.supported=false;result.texture="chest";result.chestRows=std::clamp((int(game.window.slots.size())-36+8)/9,1,6);
        first=result.chestRows*9;result.height=114+result.chestRows*18;result.inventoryLabelY=result.height-94;
        for(int i=0;i<first;++i)add(i,8+i%9*18,18+i/9*18);
        addPlayer(result,first,8,result.chestRows*18+31,result.chestRows*18+89);
    }
    result.containerSlots=first;return result;
}

bool InventoryScreen::load(const std::string& directory,std::string& error) {
    if(!std::ifstream(directory+"/runtime/gui/inventory.rgba",std::ios::binary)){error="Missing original inventory texture";return false;}
    names_.clear();std::ifstream names(directory+"/runtime/items/names.txt");std::string line;
    try {
        while(std::getline(names,line)) {
            if(line.size()>65536)throw std::runtime_error("Inventory name record is too large");
            std::istringstream fields(line);std::string id,meta,key,fallback;
            if(!std::getline(fields,id,'\t')||!std::getline(fields,meta,'\t')||!std::getline(fields,key,'\t'))continue;
            std::getline(fields,fallback);int number=std::stoi(id),metadata=std::stoi(meta);
            if(number<0||number>65535||metadata<-1||metadata>65535)continue;
            names_[(unsigned(number)<<16)|std::uint16_t(metadata)]={key,fallback};
        }
    } catch(const std::exception& e){error=e.what();return false;}
    galacticWidths_.fill(6);std::ifstream galacticFile(directory+"/runtime/font/sga-widths.bin",std::ios::binary);
    galacticFont_=bool(galacticFile.read(reinterpret_cast<char*>(galacticWidths_.data()),galacticWidths_.size()));
    creative_.reset();
    try {
        std::ifstream input(directory+"/runtime/gui/creative_catalog.bin",std::ios::binary|std::ios::ate);
        if(input) {
            auto size=input.tellg();if(size<12||size>16*1024*1024)throw std::runtime_error("Invalid creative catalog size");
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));input.seekg(0);input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(size));
            net::Reader reader(bytes);auto magic=reader.bytes(8);if(std::string(magic.begin(),magic.end())!="NVCREAT1")throw std::runtime_error("Unknown creative catalog format");
            auto result=std::make_shared<CreativeData>();int count=reader.i32();if(count<1||count>64)throw std::runtime_error("Invalid creative tab count");
            auto text=[&](){auto raw=reader.bytes(reader.u16());return std::string(raw.begin(),raw.end());};
            for(int i=0;i<count;++i) {
                CreativeData::Tab tab;tab.id=reader.i32();tab.key=text();tab.background=text();tab.title=reader.boolean();tab.scrollbar=reader.boolean();tab.icon=Game::readItem(reader);
                int entries=reader.i32();if(entries<0||entries>65536)throw std::runtime_error("Invalid creative item count");
                for(int j=0;j<entries;++j){CreativeData::Entry entry;entry.item=Game::readItem(reader);entry.key=text();entry.name=text();entry.search=text();tab.entries.push_back(std::move(entry));}
                result->tabs.push_back(std::move(tab));
            }
            reader.requireEnd();creative_=std::move(result);
            if(auto tab=creative_->tab())for(std::size_t i=0;i<tab->entries.size();++i)creative_->filtered.push_back(i);
        }
    } catch(const std::exception& e){error=e.what();return false;}
    assetsDirectory_=directory;texturesRegistered_=false;reset();error.clear();return true;
}
void InventoryScreen::reset() {
    windowId_=-1;pressedSlot_=pressedButton_=lastClickSlot_=lastClickButton_=-1;lastClickTime_=-1;placing_=false;dragSlots_.clear();
    if(creative_){creative_->scrolling=false;creative_->row=0;creative_->search.clear();creative_->filteredTab=-1;}
    anvilWindowId_=-1;anvilInputSignature_.clear();renameText_.clear();renameOriginal_.clear();renameCursor_=0;renameFocused_=renameSelectedAll_=closeRequested_=false;
}
bool InventoryScreen::creativeAvailable()const noexcept{return creative_&&!creative_->tabs.empty();}
std::size_t InventoryScreen::creativeItemCount(int tab)const noexcept{if(creative_)for(const auto& t:creative_->tabs)if(t.id==tab)return t.entries.size();return 0;}
void InventoryScreen::selectCreativeTab(int tab) {
    if(!creative_)return;
    for(const auto& t:creative_->tabs)if(t.id==tab) {
        creative_->selected=tab;creative_->search.clear();creative_->row=0;creative_->filtered.clear();creative_->filteredTab=-1;
        for(std::size_t i=0;i<t.entries.size();++i)creative_->filtered.push_back(i);
        placing_=false;dragSlots_.clear();return;
    }
}

void InventoryScreen::interact(Game& game,Controller& controller,const InventoryInput& input) {
    const int windowId=game.window.open?game.window.id:0;
    if(windowId_!=windowId){reset();windowId_=windowId;}
    if(creativeAllowed(game)&&creativeAvailable()){interactCreative(game,controller,input);return;}
    if(interactControls(game,controller,input))return;
    auto view=layout(game);if(!view.supported)return;
    const int slot=view.hit(input.x,input.y);
    if(placing_&&game.cursor.empty()){placing_=false;pressedButton_=-1;dragSlots_.clear();}
    if(input.numberSlot>=0&&input.numberSlot<9&&slot>=0)controller.clickSlot(game,slot,input.numberSlot,2);
    if(input.dropPressed&&slot>=0&&game.cursor.empty())controller.clickSlot(game,slot,input.control?1:0,4);
    if(input.middlePressed&&slot>=0&&game.creativeMode)controller.clickSlot(game,slot,2,3);
    int button=input.leftPressed?0:input.rightPressed?1:-1;
    if(button>=0) {
        const bool doubleClick=button==0&&slot>=0&&slot==lastClickSlot_&&lastClickButton_==0&&input.timeSeconds-lastClickTime_<=.25;
        lastClickSlot_=slot;lastClickButton_=button;lastClickTime_=input.timeSeconds;
        pressedSlot_=slot;pressedButton_=button;dragSlots_.clear();placing_=false;
        if(doubleClick&&!input.shift&&!game.cursor.empty()){controller.clickSlot(game,slot,button,6);pressedButton_=-1;}
        else if(slot==-999){controller.clickSlot(game,-999,button,0);pressedButton_=-1;}
        else if(slot>=0&&input.shift){controller.clickSlot(game,slot,button,1);pressedButton_=-1;}
        else if(slot>=0&&game.cursor.empty()){controller.clickSlot(game,slot,button,0);pressedButton_=-1;}
        else if(slot>=0&&!game.cursor.empty()){placing_=true;dragSlots_.insert(slot);}
    }
    if(placing_&&slot>=0&&((pressedButton_==0&&input.leftDown)||(pressedButton_==1&&input.rightDown)))dragSlots_.insert(slot);
    if(placing_&&((pressedButton_==0&&input.leftReleased)||(pressedButton_==1&&input.rightReleased))) {
        if(dragSlots_.size()>1) {
            int mode=pressedButton_==1?1:0;
            controller.clickSlot(game,-999,mode<<2,5);
            for(int index:dragSlots_)controller.clickSlot(game,index,(mode<<2)|1,5);
            controller.clickSlot(game,-999,(mode<<2)|2,5);
        } else if(slot>=0)controller.clickSlot(game,slot,pressedButton_,0);
        else if(slot==-999)controller.clickSlot(game,-999,pressedButton_,0);
        placing_=false;pressedButton_=-1;dragSlots_.clear();
    }
}

std::string InventoryScreen::itemName(const ItemStack& item,const I18n& tr) const {
    if(item.empty())return {};
    if(!details(item).name.empty())return details(item).name;
    if(const auto name=variantItemName(item,tr))return *name;
    int metadata=registry::item(item.id).maxDamage>0?0:item.damage;
    for(int variant:{metadata,0,-1}) {
        auto found=names_.find((unsigned(item.id)<<16)|std::uint16_t(variant));
        if(found==names_.end())continue;
        auto value=tr(found->second.key);if(value!=found->second.key)return value;
        if(!found->second.fallback.empty())return found->second.fallback;
    }
    std::string name=registry::item(item.id).name;auto colon=name.find(':');if(colon!=std::string::npos)name=name.substr(colon+1);
    std::vector<std::string> keys={"item."+name+".name","tile."+name+".name"};
    if(item.id==1){const char* v[]={"stone","granite","graniteSmooth","diorite","dioriteSmooth","andesite","andesiteSmooth"};keys.insert(keys.begin(),"tile.stone."+std::string(v[std::clamp<int>(item.damage,0,6)])+".name");}
    if(item.id==5){const char* v[]={"oak","spruce","birch","jungle","acacia","big_oak"};keys.insert(keys.begin(),"tile.wood."+std::string(v[std::clamp<int>(item.damage,0,5)])+".name");}
    for(const auto& key:keys){auto text=tr(key);if(text!=key)return text;}
    std::replace(name.begin(),name.end(),'_',' ');return name;
}
std::vector<std::string> InventoryScreen::itemTooltip(const ItemStack& item,const I18n& tr,bool advanced) const {
    return item_tooltip::item(item,tr,[&](const ItemStack& stack){return itemName(stack,tr);},advanced);
}

void InventoryScreen::interactCreative(Game& game,Controller& controller,const InventoryInput& input) {
    if(!creativeAllowed(game)||!creative_)return;
    auto& data=*creative_;auto tab=data.tab();if(!tab)return;
    InventoryLayout view;view.width=195;view.height=136;
    if(data.selected==11) {
        for(int i=0;i<4;++i)view.slots.push_back({5+i,9+(i/2)*54,6+(i%2)*27});
        addPlayer(view,9,9,54,112);
    } else for(int i=0;i<9;++i)view.slots.push_back({36+i,9+i*18,112});
    int slot=view.hit(input.x,input.y);
    bool trash=data.selected==11&&input.x>=172&&input.x<190&&input.y>=111&&input.y<129;
    int cell=-1;
    if(data.selected!=11&&input.x>=8&&input.x<170&&input.y>=17&&input.y<107)cell=int((input.y-17)/18)*9+int((input.x-8)/18);
    const CreativeData::Entry* entry=nullptr;
    if(cell>=0) {
        std::size_t index=static_cast<std::size_t>(data.row*9+cell);
        if(index<data.filtered.size())entry=&tab->entries[data.filtered[index]];
    }
    auto prediction=[&](int target,int button,int mode) {
        auto before=game.inventory;
        if(!controller.clickSlot(game,target,button,mode,false))return false;
        bool success=true;
        if(data.selected!=11&&mode==5&&(button&3)==2) {
            for(int i=36;i<45;++i)success=sendCreative(game,i,game.inventory[i])&&success;
        } else {
            for(int i=0;i<45;++i)if(!equalStack(before[i],game.inventory[i])||(data.selected!=11&&i==target))success=sendCreative(game,i,game.inventory[i])&&success;
        }
        return success;
    };
    auto maximum=[&](const ItemStack& source){auto value=source;value.count=static_cast<std::int8_t>(controller.maxStackSize(value));return value;};
    if(entry&&input.numberSlot>=0&&input.numberSlot<9)sendCreative(game,36+input.numberSlot,maximum(entry->item));
    else if(slot>=0&&input.numberSlot>=0&&input.numberSlot<9)prediction(slot,input.numberSlot,2);
    if(input.dropPressed) {
        if(entry){auto dropped=input.control?maximum(entry->item):entry->item;dropped.count=input.control?dropped.count:1;sendCreative(game,-1,dropped);}
        else if(slot>=0&&!stackAt(game,slot).empty()) {
            auto old=game.inventory[slot],dropped=old;dropped.count=input.control?old.count:1;
            old.count=static_cast<std::int8_t>(old.count-dropped.count);if(old.count<=0)old={};
            if(game.online){if(sendCreative(game,slot,old))sendCreative(game,-1,dropped);}
            else if(sendCreative(game,-1,dropped))sendCreative(game,slot,old);
        }
    }
    if(input.middlePressed&&game.cursor.empty()) {
        if(entry)game.cursor=maximum(entry->item);else if(slot>=0)prediction(slot,2,3);
    }
    int button=input.leftPressed?0:input.rightPressed?1:-1;
    if(button>=0) {
        if(trash) {
            if(input.shift)for(int i=0;i<45;++i)sendCreative(game,i,{});
            else game.cursor={};
            return;
        }
        if(cell>=0) {
            if(entry&&game.cursor.empty()){game.cursor=input.shift?maximum(entry->item):entry->item;}
            else if(entry&&!game.cursor.empty()&&game.cursor.id==entry->item.id&&game.cursor.damage==entry->item.damage) {
                int count=game.cursor.count;
                count=button==0?(input.shift?controller.maxStackSize(game.cursor):std::min(count+1,controller.maxStackSize(game.cursor))):count-1;
                game.cursor.count=static_cast<std::int8_t>(count);if(count<=0)game.cursor={};
            } else game.cursor={};
            placing_=false;dragSlots_.clear();return;
        }
        if(slot==-999) {
            if(!game.cursor.empty()) {
                auto dropped=game.cursor;if(button==1)dropped.count=1;
                if(sendCreative(game,-1,dropped)){game.cursor.count=static_cast<std::int8_t>(game.cursor.count-dropped.count);if(game.cursor.count<=0)game.cursor={};}
            }
            return;
        }
        if(slot>=0) {
            bool doubleClick=button==0&&slot==lastClickSlot_&&lastClickButton_==0&&input.timeSeconds-lastClickTime_<=.25&&!game.cursor.empty();
            lastClickSlot_=slot;lastClickButton_=button;lastClickTime_=input.timeSeconds;
            if(doubleClick&&data.selected==11){prediction(slot,button,6);placing_=false;return;}
            if(input.shift) {
                if(data.selected==11)prediction(slot,button,1);else sendCreative(game,slot,{});
            } else if(game.cursor.empty())prediction(slot,button,0);
            else {placing_=true;pressedButton_=button;pressedSlot_=slot;dragSlots_.clear();dragSlots_.insert(slot);}
        }
    }
    if(placing_&&game.cursor.empty()){placing_=false;dragSlots_.clear();}
    if(placing_&&slot>=0&&((pressedButton_==0&&input.leftDown)||(pressedButton_==1&&input.rightDown)))dragSlots_.insert(slot);
    if(placing_&&((pressedButton_==0&&input.leftReleased)||(pressedButton_==1&&input.rightReleased))) {
        if(dragSlots_.size()>1) {
            int mode=pressedButton_==1?1:0;
            prediction(-999,mode<<2,5);for(int selected:dragSlots_)prediction(selected,(mode<<2)|1,5);prediction(-999,(mode<<2)|2,5);
        } else if(slot>=0)prediction(slot,pressedButton_,0);
        placing_=false;pressedButton_=-1;dragSlots_.clear();
    }
}

bool InventoryScreen::drawCreative(Renderer& renderer,Game& game,Controller& controller,Window& window,I18n& tr,float scale) {
    if(!creative_)return false;
    auto& data=*creative_;scale=std::max(.5f,scale);
    const float left=(window.width()-195*scale)*.5f,top=(window.height()-136*scale)*.5f;
    InventoryInput input;input.x=(window.mouseX-left)/scale;input.y=(window.mouseY-top)/scale;
    input.leftPressed=window.pressed(VK_LBUTTON);input.rightPressed=window.pressed(VK_RBUTTON);input.middlePressed=window.pressed(bindings_?bindings_->key("pickItem"):VK_MBUTTON);
    input.leftDown=window.down(VK_LBUTTON);input.rightDown=window.down(VK_RBUTTON);input.leftReleased=window.released(VK_LBUTTON);input.rightReleased=window.released(VK_RBUTTON);
    input.shift=window.down(VK_SHIFT);input.control=window.down(VK_CONTROL);input.dropPressed=window.pressed(bindings_?bindings_->key("drop"):'Q');
    input.timeSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    int hoveredTab=-1;
    for(const auto& tab:data.tabs) {
        int column=tab.id%6,x=column==5?169:column*29,y=tab.id<6?-32:136;
        if(input.x>=x&&input.x<=x+28&&input.y>=y&&input.y<=y+32) {
            hoveredTab=tab.id;if(input.leftPressed)selectCreativeTab(tab.id);
            input.leftPressed=input.rightPressed=input.middlePressed=false;
        }
    }
    if(data.selected==5&&windowId_>=0) {
        if(window.pressed(VK_BACK))eraseUtf8(data.search);
        bool overItems=input.x>=8&&input.x<170&&((input.y>=17&&input.y<107)||(input.y>=111&&input.y<129));
        if(overItems)for(int i=0;i<9;++i)if(window.pressed(bindings_?bindings_->key("hotbar."+std::to_string(i+1)):'1'+i))input.numberSlot=i;
        for(char32_t code:window.textInput)if(code>=32&&code!=127&&!(input.numberSlot>=0&&code==char32_t('1'+input.numberSlot))) {
            int count=0;for(unsigned char byte:data.search)if((byte&0xc0)!=0x80)++count;
            if(count<15)appendUtf8(data.search,code);
        }
        input.dropPressed=false;
    } else for(int i=0;i<9;++i)if(window.pressed(bindings_?bindings_->key("hotbar."+std::to_string(i+1)):'1'+i))input.numberSlot=i;
    data.rebuild(*this,tr);auto tab=data.tab();if(!tab)return false;
    int maxRow=std::max(0,int((data.filtered.size()+8)/9)-5);
    if(window.wheel&&tab->scrollbar)data.row=std::clamp(data.row-window.wheel,0,maxRow);
    if(input.leftPressed&&tab->scrollbar&&input.x>=175&&input.x<187&&input.y>=18&&input.y<130){data.scrolling=true;input.leftPressed=false;}
    if(!input.leftDown)data.scrolling=false;
    if(data.scrolling)data.row=int(std::clamp((input.y-25.5f)/95.f,0.f,1.f)*maxRow+.5f);
    interact(game,controller,input);
    renderer.rect(0,0,float(window.width()),float(window.height()),0x00000099u);
    auto drawTab=[&](const CreativeData::Tab& value) {
        int column=value.id%6,x=column==5?167:column*29,y=value.id<6?-28:132;
        int sourceY=(value.id==data.selected?32:0)+(value.id>=6?64:0);
        renderer.sprite("creative_tabs",left+x*scale,top+y*scale,28*scale,32*scale,float(column*28),float(sourceY),28,32);
        stack(renderer,value.icon,left+(x+6)*scale,top+(y+(value.id<6?9:7))*scale,scale);
    };
    for(const auto& value:data.tabs)if(value.id!=data.selected)drawTab(value);
    std::string background=tab->background;auto dot=background.rfind('.');if(dot!=std::string::npos)background.resize(dot);
    renderer.sprite("creative_tab_"+background,left,top,195*scale,136*scale,0,0,195,136);drawTab(*tab);
    if(tab->title)renderer.text(tr(tab->key),left+8*scale,top+6*scale,scale,0x404040ffu,false);
    if(data.selected==5) {
        std::string visible=data.search;
        while(!visible.empty()&&renderer.textWidth(visible,scale)>86*scale) {
            std::size_t n=1;while(n<visible.size()&&(static_cast<unsigned char>(visible[n])&0xc0)==0x80)++n;visible.erase(0,n);
        }
        renderer.text(visible,left+82*scale,top+6*scale,scale,0xffffffffu,false);
        if((int(input.timeSeconds*2)&1)==0)renderer.rect(left+82*scale+renderer.textWidth(visible,scale),top+6*scale,scale,9*scale,0xffffffffu);
    }
    if(tab->scrollbar)renderer.sprite("creative_tabs",left+175*scale,top+(18+(maxRow?95.f*data.row/maxRow:0))*scale,12*scale,15*scale,maxRow?232.f:244.f,0,12,15);
    int hovered=-1;const ItemStack* hoveredStack=nullptr;
    if(data.selected==11) {
        InventoryLayout view;view.width=195;view.height=136;
        for(int i=0;i<4;++i)view.slots.push_back({5+i,9+(i/2)*54,6+(i%2)*27});
        addPlayer(view,9,9,54,112);
        hovered=view.hit(input.x,input.y);
        const auto rotation=visual::inventoryPreviewRotation(43-input.x,15-input.y);
        renderer.previewEntity(left+43*scale,top+45*scale,36*scale,rotation.yaw,rotation.pitch);
        for(const auto& position:view.slots) {
            stack(renderer,game.inventory[position.index],left+position.x*scale,top+position.y*scale,scale);
            if(position.index==hovered){renderer.rect(left+position.x*scale,top+position.y*scale,16*scale,16*scale,0xffffff80u);hoveredStack=&game.inventory[position.index];}
        }
    } else {
        for(int cell=0;cell<45;++cell) {
            std::size_t index=static_cast<std::size_t>(data.row*9+cell);if(index>=data.filtered.size())break;
            const auto& entry=tab->entries[data.filtered[index]];int x=9+cell%9*18,y=18+cell/9*18;
            stack(renderer,entry.item,left+x*scale,top+y*scale,scale);
            if(input.x>=x-1&&input.x<x+17&&input.y>=y-1&&input.y<y+17){renderer.rect(left+x*scale,top+y*scale,16*scale,16*scale,0xffffff80u);hoveredStack=&entry.item;}
        }
        for(int i=0;i<9;++i){int x=9+i*18;stack(renderer,game.inventory[36+i],left+x*scale,top+112*scale,scale);
            if(input.x>=x-1&&input.x<x+17&&input.y>=111&&input.y<129){renderer.rect(left+x*scale,top+112*scale,16*scale,16*scale,0xffffff80u);hoveredStack=&game.inventory[36+i];}}
    }
    if(!game.cursor.empty())stack(renderer,game.cursor,float(window.mouseX)-8*scale,float(window.mouseY)-8*scale,scale);
    else if(hoveredTab>=0) {
        for(const auto& value:data.tabs)if(value.id==hoveredTab)tooltip(renderer,{tr(value.key)},float(window.mouseX),float(window.mouseY),scale,window.width(),window.height());
    }
    else if(hoveredStack&&!hoveredStack->empty()) {
        const auto lines=itemTooltip(*hoveredStack,tr,advancedTooltips_);
        tooltip(renderer,lines,float(window.mouseX),float(window.mouseY),scale,window.width(),window.height());
    }
    return false;
}

bool InventoryScreen::draw(Renderer& renderer,Game& game,Controller& controller,Window& window,I18n& tr,float scale) {
    if(windowId_>0&&!game.window.open){reset();return true;}
    bool searching=(creativeAllowed(game)&&creative_&&creative_->selected==5)||renameFocused_;
    if(windowId_>=0&&(window.pressed(VK_ESCAPE)||(!searching&&window.pressed(bindings_?bindings_->key("inventory"):'E')))){reset();return true;}
    if(!texturesRegistered_&&!assetsDirectory_.empty()) {
        for(const std::string name:{"inventory","chest","crafting","furnace","brewing","hopper","dispenser","anvil","enchanting","beacon","villager","horse"})
            renderer.registerUITexture(name,assetsDirectory_+"/runtime/gui/"+name+".rgba");
        for(const std::string name:{"tabs","tab_items","tab_item_search","tab_inventory"})
            renderer.registerUITexture("creative_"+name,assetsDirectory_+"/runtime/gui/container/creative_inventory/"+name+".rgba");
        if(galacticFont_)renderer.registerUITexture("enchant_font",assetsDirectory_+"/runtime/gui/enchant_font.rgba");
        texturesRegistered_=true;
    }
    if(creativeAllowed(game)&&creativeAvailable())return drawCreative(renderer,game,controller,window,tr,scale);
    int currentWindow=game.window.open?game.window.id:0;if(windowId_!=currentWindow){reset();windowId_=currentWindow;}
    syncAnvil(game,controller,&tr);
    scale=std::max(.5f,scale);const auto view=layout(game);
    const float left=(window.width()-view.width*scale)*.5f,top=(window.height()-view.height*scale)*.5f;
    InventoryInput input;input.x=(window.mouseX-left)/scale;input.y=(window.mouseY-top)/scale;
    input.leftPressed=window.pressed(VK_LBUTTON);input.rightPressed=window.pressed(VK_RBUTTON);input.middlePressed=window.pressed(bindings_?bindings_->key("pickItem"):VK_MBUTTON);
    input.leftDown=window.down(VK_LBUTTON);input.rightDown=window.down(VK_RBUTTON);input.leftReleased=window.released(VK_LBUTTON);input.rightReleased=window.released(VK_RBUTTON);
    input.shift=window.down(VK_SHIFT);input.control=window.down(VK_CONTROL);input.dropPressed=window.pressed(bindings_?bindings_->key("drop"):'Q');
    input.textInput=window.textInput;input.backspace=window.pressed(VK_BACK)||std::find(window.textInput.begin(),window.textInput.end(),U'\b')!=window.textInput.end();
    input.deletePressed=window.pressed(VK_DELETE);input.selectAll=input.control&&window.pressed('A');
    input.leftKey=window.pressed(VK_LEFT);input.rightKey=window.pressed(VK_RIGHT);input.homeKey=window.pressed(VK_HOME);input.endKey=window.pressed(VK_END);
    for(int i=0;i<9;++i)if(window.pressed(bindings_?bindings_->key("hotbar."+std::to_string(i+1)):'1'+i))input.numberSlot=i;
    input.timeSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    interact(game,controller,input);
    if(closeRequested_){reset();return true;}
    syncAnvil(game,controller,&tr);
    renderer.rect(0,0,float(window.width()),float(window.height()),0x00000099u);
    if(view.chestRows) {
        int first=view.chestRows*18+17;
        renderer.sprite("chest",left,top,176*scale,first*scale,0,0,176,float(first));
        renderer.sprite("chest",left,top+first*scale,176*scale,96*scale,0,126,176,96);
    } else renderer.sprite(view.texture,left,top,view.width*scale,view.height*scale,0,0,float(view.width),float(view.height));
    if(view.player) {
        renderer.text(tr("container.crafting"),left+86*scale,top+16*scale,scale,0x404040ffu,false);
        const auto rotation=visual::inventoryPreviewRotation(51-input.x,25-input.y);
        renderer.previewEntity(left+51*scale,top+75*scale,54*scale,rotation.yaw,rotation.pitch);
    } else {
        auto title=game.window.title;
        if(title.empty())title=tr(titleKey(game.window.type));
        else if(title.front()=='{'||title.front()=='['||title.front()=='"')title=Game::plainChat(title);
        title=translated(tr,title,title);
        const auto& type=game.window.type;
        if(type=="minecraft:beacon") {
            auto primary=tr("tile.beacon.primary"),secondary=tr("tile.beacon.secondary");
            renderer.text(primary,left+62*scale-renderer.textWidth(primary,scale)/2,top+10*scale,scale,0xe0e0e0ffu);
            renderer.text(secondary,left+169*scale-renderer.textWidth(secondary,scale)/2,top+10*scale,scale,0xe0e0e0ffu);
        } else {
            float tx=8,ty=6;
            if(type=="minecraft:anvil")tx=60;
            else if(type=="minecraft:crafting_table")tx=28;
            else if(type=="minecraft:enchanting_table"){tx=12;ty=5;}
            else if(type=="minecraft:villager"||type=="minecraft:furnace"||type=="minecraft:brewing_stand"||type=="minecraft:dispenser"||type=="minecraft:dropper")tx=(view.width-renderer.textWidth(title,scale)/scale)/2;
            renderer.text(title,left+tx*scale,top+ty*scale,scale,0x404040ffu,false);
            if(type!="minecraft:anvil")renderer.text(tr("container.inventory"),left+view.inventoryLabelX*scale,top+view.inventoryLabelY*scale,scale,0x404040ffu,false);
        }
    }
    if(game.window.open&&game.window.type=="minecraft:furnace") {
        int burn=property(game,0),total=property(game,1,200);if(total<=0)total=200;int flame=std::clamp(burn*13/total,0,13);
        if(burn>0)renderer.sprite("furnace",left+56*scale,top+(48-flame)*scale,14*scale,(flame+1)*scale,176,float(12-flame),14,float(flame+1));
        int cookTotal=property(game,3,200);int cooked=cookTotal>0?std::clamp(property(game,2)*24/cookTotal,0,24):0;
        renderer.sprite("furnace",left+79*scale,top+34*scale,(cooked+1)*scale,16*scale,176,14,float(cooked+1),16);
    }
    if(game.window.open&&game.window.type=="minecraft:brewing_stand") {
        int remaining=property(game,0);
        if(remaining>0) {
            int progress=std::clamp(int(28.f*(1-float(remaining)/400)),0,28);const int bubbles[]={29,24,20,16,11,6,0};int bubble=bubbles[(remaining/2)%7];
            if(progress)renderer.sprite("brewing",left+97*scale,top+16*scale,9*scale,progress*scale,176,0,9,float(progress));
            if(bubble)renderer.sprite("brewing",left+65*scale,top+(43-bubble)*scale,12*scale,bubble*scale,185,float(29-bubble),12,float(bubble));
        }
    }
    const auto widgets=controls(game);std::array<std::string,3> words;
    if(game.window.open&&game.window.type=="minecraft:enchanting_table")words=enchantmentWords(property(game,3));
    for(const auto& widget:widgets) {
        bool hover=widget.hit(input.x,input.y);float x=left+widget.x*scale,y=top+widget.y*scale;
        if(widget.action==InventoryAction::PreviousTrade||widget.action==InventoryAction::NextTrade) {
            renderer.sprite("villager",x,y,12*scale,19*scale,widget.enabled?(hover?188.f:176.f):200.f,widget.action==InventoryAction::NextTrade?0.f:19.f,12,19);
        } else if(widget.action==InventoryAction::Enchant) {
            int cost=property(game,widget.value);renderer.sprite("enchanting",x,y,108*scale,19*scale,0,widget.enabled?(hover?204.f:166.f):185.f,108,19);
            if(cost>0) {
                renderer.sprite("enchanting",x+scale,y+scale,16*scale,16*scale,float(widget.value*16),widget.enabled?223.f:239.f,16,16);
                unsigned runeColor=widget.enabled?(hover?16777088u:6839882u):((6839882u&16711422u)>>1);
                if(galacticFont_)galactic(renderer,words[widget.value],galacticWidths_,x+20*scale,y+2*scale,scale,(runeColor<<8)|255u);
                auto level=std::to_string(cost);renderer.text(level,x+106*scale-renderer.textWidth(level,scale),y+9*scale,scale,widget.enabled?0x80ff20ffu:0x408010ffu);
            }
        } else if(widget.action==InventoryAction::BeaconPrimary||widget.action==InventoryAction::BeaconSecondary||widget.action==InventoryAction::BeaconConfirm||widget.action==InventoryAction::BeaconCancel) {
            int u=!widget.enabled?44:widget.selected?22:hover?66:0;
            renderer.sprite("beacon",x,y,22*scale,22*scale,float(u),219,22,22);
            if(widget.action==InventoryAction::BeaconConfirm||widget.action==InventoryAction::BeaconCancel)
                renderer.sprite("beacon",x+2*scale,y+2*scale,18*scale,18*scale,widget.action==InventoryAction::BeaconConfirm?90.f:112.f,220,18,18);
            else {
                int icon=widget.value==1?0:widget.value==3?2:widget.value==5?4:widget.value==8?10:widget.value==10?7:14;
                renderer.sprite("inventory",x+2*scale,y+2*scale,18*scale,18*scale,float(icon%8*18),float(198+icon/8*18),18,18);
            }
        }
    }
    if(game.window.open&&game.window.type=="minecraft:anvil") {
        bool enabled=!stackAt(game,0).empty();renderer.sprite("anvil",left+59*scale,top+20*scale,110*scale,16*scale,0,enabled?166.f:182.f,110,16);
        std::size_t start=0;while(start<renameCursor_&&renderer.textWidth(renameText_.substr(start,renameCursor_-start),scale)>99*scale)start=nextUtf8(renameText_,start);
        std::string visible=renameText_.substr(start);while(!visible.empty()&&renderer.textWidth(visible,scale)>103*scale)eraseUtf8(visible);
        if(renameSelectedAll_&&renameFocused_)renderer.rect(left+62*scale,top+23*scale,renderer.textWidth(visible,scale),10*scale,0x3333aa80u);
        renderer.text(visible,left+62*scale,top+24*scale,scale,0xffffffffu);
        if(renameFocused_&&(int(input.timeSeconds*2)&1)==0)renderer.rect(left+62*scale+renderer.textWidth(renameText_.substr(start,renameCursor_-start),scale),top+24*scale,scale,9*scale,0xffffffffu);
        if((!stackAt(game,0).empty()||!stackAt(game,1).empty())&&stackAt(game,2).empty())renderer.sprite("anvil",left+99*scale,top+45*scale,28*scale,21*scale,176,0,28,21);
        int cost=property(game,0);if(cost>0) {
            bool expensive=cost>=40&&!game.creativeMode;
            if(expensive||!stackAt(game,2).empty()) {
                auto text=expensive?tr("container.repair.expensive"):formatOne(tr("container.repair.cost"),std::to_string(cost));
                renderer.text(text,left+168*scale-renderer.textWidth(text,scale),top+67*scale,scale,(expensive||(!game.creativeMode&&game.experienceLevel<cost))?0xff6060ffu:0x80ff20ffu);
            }
        }
    }
    const TradeOffer* trade=nullptr;
    if(game.window.open&&game.window.type=="minecraft:villager"&&game.window.selectedTrade>=0&&game.window.selectedTrade<int(game.window.trades.size())) {
        trade=&game.window.trades[game.window.selectedTrade];stack(renderer,trade->first,left+36*scale,top+24*scale,scale);stack(renderer,trade->second,left+62*scale,top+24*scale,scale);stack(renderer,trade->result,left+120*scale,top+24*scale,scale);
        if(trade->disabled||trade->uses>=trade->maxUses)for(int y:{21,51})renderer.sprite("villager",left+83*scale,top+y*scale,28*scale,21*scale,212,0,28,21);
    }
    if(game.window.open&&game.window.type=="minecraft:beacon") {
        const int ids[]={388,264,266,265};for(int i=0;i<4;++i)renderer.itemIcon(ids[i],0,left+(42+22*i)*scale,top+109*scale,16*scale);
    }
    int hovered=view.hit(input.x,input.y);
    for(const auto& slot:view.slots) {
        stack(renderer,stackAt(game,slot.index),left+slot.x*scale,top+slot.y*scale,scale);
        if(slot.index==hovered)renderer.rect(left+slot.x*scale,top+slot.y*scale,16*scale,16*scale,0xffffff80u);
    }
    if(!game.cursor.empty())stack(renderer,game.cursor,float(window.mouseX)-8*scale,float(window.mouseY)-8*scale,scale);
    else if(hovered>=0&&!stackAt(game,hovered).empty()) {
        const auto& item=stackAt(game,hovered);const auto lines=itemTooltip(item,tr,advancedTooltips_);
        tooltip(renderer,lines,float(window.mouseX),float(window.mouseY),scale,window.width(),window.height());
    }
    if(game.cursor.empty()) {
        if(trade) {
            const ItemStack* offer=nullptr;
            if(input.y>=23&&input.y<41){if(input.x>=35&&input.x<53)offer=&trade->first;else if(input.x>=61&&input.x<79)offer=&trade->second;else if(input.x>=119&&input.x<137)offer=&trade->result;}
            if(offer&&!offer->empty())tooltip(renderer,itemTooltip(*offer,tr,advancedTooltips_),float(window.mouseX),float(window.mouseY),scale,window.width(),window.height());
            else if((trade->disabled||trade->uses>=trade->maxUses)&&input.x>=82&&input.x<112&&((input.y>=20&&input.y<43)||(input.y>=50&&input.y<73)))tooltip(renderer,{tr("merchant.deprecated")},float(window.mouseX),float(window.mouseY),scale,window.width(),window.height());
        }
        for(const auto& widget:widgets)if(widget.hit(input.x,input.y)) {
            std::vector<std::string> lines;
            if(widget.action==InventoryAction::Enchant) {
                int clue=property(game,4+widget.value,-1),cost=property(game,widget.value);
                if(cost>0&&clue>=0) {
                    auto name=enchantmentName(clue&255,(clue>>8)&255,tr);if(!name.empty())lines.push_back(formatOne(tr("container.enchant.clue"),name));
                    if(!game.creativeMode){int n=widget.value+1;lines.push_back(formatOne(tr(n==1?"container.enchant.lapis.one":"container.enchant.lapis.many"),std::to_string(n)));lines.push_back(formatOne(tr(n==1?"container.enchant.level.one":"container.enchant.level.many"),std::to_string(n)));}
                }
            } else if(widget.action==InventoryAction::BeaconConfirm)lines.push_back(tr("gui.done"));
            else if(widget.action==InventoryAction::BeaconCancel)lines.push_back(tr("gui.cancel"));
            else if(widget.action==InventoryAction::BeaconPrimary||widget.action==InventoryAction::BeaconSecondary) {
                const char* name=widget.value==1?"potion.moveSpeed":widget.value==3?"potion.digSpeed":widget.value==5?"potion.damageBoost":widget.value==8?"potion.jump":widget.value==10?"potion.regeneration":"potion.resistance";
                lines.push_back(tr(name)+(widget.action==InventoryAction::BeaconSecondary&&widget.value!=10?" II":""));
            }
            if(!lines.empty())tooltip(renderer,lines,float(window.mouseX),float(window.mouseY),scale,window.width(),window.height());
        }
    }
    return false;
}
} // namespace mc
