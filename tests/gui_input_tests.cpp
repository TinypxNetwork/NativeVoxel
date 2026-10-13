#include "mc/gui_text_input.hpp"
#include "mc/sign_text.hpp"
#include "mc/item_tooltip.hpp"
#include "mc/game.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
bool contains(const std::vector<std::string>& rows,const std::string& text){return std::any_of(rows.begin(),rows.end(),[&](const auto& row){return row.find(text)!=std::string::npos;});}
void editing(){
    mc::gui::TextInput input;input.setText(u8"abc\u4f60\u597d\U0001f600Z");
    require(mc::gui::TextInput::units(input.text())==8,"Text length uses Java UTF-16 units");
    input.move(-1);input.erase(true);require(input.text()==u8"abc\u4f60\u597dZ","Backspace in the middle removes a whole supplementary character");
    input.move(-1);input.insert(std::string("!"));require(input.text()==u8"abc\u4f60!\u597dZ","Insertion follows the caret rather than the end");
    input.erase(false);require(input.text()==u8"abc\u4f60!Z","Delete removes the following Unicode character");
    input.home();input.move(1,false,true);input.move(1,false,true);require(input.selection()=="ab","Shift arrows extend a selection");
    input.insert(std::string("Q"));require(input.text()==u8"Qc\u4f60!Z"&&input.cursor()==1,"Typing replaces the selected region");
    input.selectAll();input.insert(std::u32string(80,U'\U0001f600'));require(mc::gui::TextInput::units(input.text())==100&&input.text().size()==200,"The original 100-unit chat limit never cuts a UTF-8 sequence");
    for(int i=0;i<12;++i)input.erase(true);require(mc::gui::TextInput::units(input.text())==76,"Repeated backspaces consume every key repeat");
    input.setText("one two three");input.move(-1,true);require(input.cursor()==8,"Ctrl-left moves to the previous word start");
    input.erase(true,true);require(input.text()=="one three"&&input.cursor()==4,"Ctrl-backspace deletes the previous word and separator");
    input.home();input.move(1,true);require(input.cursor()==4,"Ctrl-right skips the next separator");
    input.end(true);require(input.selection()=="three","Shift-End selects to the end");input.erase(false);require(input.text()=="one ","Delete with a selection removes exactly the selection");
    mc::gui::TextInput shortField(3);shortField.insert(std::u32string(U"\U0001f600ab"));require(shortField.text()==u8"\U0001f600a","Surrogate-aware limit reserves two units for emoji");
    shortField.clear();shortField.insert(std::u32string(U"\n\u00a7\x7f\ta"));require(shortField.text()=="a","Chat disallows control characters and section-sign formatting input");
    input.setText("0123456789");auto measure=[](const std::string& value){return float(value.size()*6);};auto view=input.view(24,measure);
    require(view.begin>0&&view.cursorX<24&&view.cursorAtEnd,"Long input scrolls horizontally to keep the caret visible");
    input.home();view=input.view(24,measure);require(view.begin==0&&view.text=="0123"&&view.cursorX==0,"Home restores the beginning of a scrolled input");
    input.click(view,13,measure);require(input.cursor()==2,"Mouse positioning uses the displayed glyph advances");
}
void completion(){
    mc::gui::TextInput input;mc::gui::ChatCompletion completion;input.setText("/te");auto request=completion.tab(input);
    require(request&&*request=="/te"&&completion.waiting(),"Tab requests the complete prefix to the caret");
    require(completion.response(input,{"/teleport","/tell"})&&input.text()=="/tel","A server common prefix is inserted first");
    request=completion.tab(input);require(request&&*request=="/tel","Tab asks again after inserting a common prefix");
    completion.response(input,{"/teleport","/tell"});require(input.text()=="/teleport"&&completion.hint()=="/teleport, /tell","An unchanged common prefix starts candidate cycling");
    require(!completion.tab(input)&&input.text()=="/tell","Second Tab cycles locally to the next candidate");
    completion.tab(input);require(input.text()=="/teleport","Candidate cycling wraps to the first result");
    input.setText("/tp A destination");input.home();for(int i=0;i<5;++i)input.move(1);request=completion.tab(input);require(request&&*request=="/tp A","Requests exclude the text after a middle caret");
    completion.response(input,{"Alice","Alex"});require(input.text()=="/tp Al destination","Completion preserves the remainder of the input");
    completion.tab(input);input.insert(std::string("x"));require(!completion.response(input,{"Alice"})&&input.text()=="/tp Alx destination","Stale replies cannot overwrite newly typed text");
    input.setText("/unknown");completion.tab(input);require(!completion.response(input,{})&&input.text()=="/unknown","An empty completion response does not delete input");
    completion.cancel();require(!completion.waiting()&&completion.hint().empty(),"Leaving or editing chat cancels pending suggestions");
}
void packets(){
    mc::Game game;std::vector<mc::net::Packet> sent;game.online=game.joined=true;game.outgoingObserver=[&](const mc::net::Packet& packet){sent.push_back(packet);};
    require(game.sendTabComplete("/tp Al")&&sent.back().id==0x14,"Tab completion uses C14");
    {mc::net::Reader body(sent.back().data);require(body.string()=="/tp Al"&&!body.boolean(),"C14 encodes a nullable target block");body.requireEnd();}
    require(game.sendTabComplete("/setblock ",std::array<int,3>{{-17,64,42}}),"Block-aware completion request accepted");
    {mc::net::Reader body(sent.back().data);body.string();require(body.boolean(),"C14 target flag is present");const auto packed=body.u64();require((packed>>38)==(std::uint64_t(std::uint32_t(-17)&0x3ffffff))&&((packed>>26)&4095)==64&&(packed&0x3ffffff)==42,"C14 preserves signed original BlockPos packing");body.requireEnd();}
    mc::net::Writer response;response.varInt(2).string("Alice").string("Alex");const auto revision=game.tabCompletionRevision;
    game.consumePacket({0x3a,response.data});require(game.tabCompletions==std::vector<std::string>({"Alice","Alex"})&&game.tabCompletionRevision==revision+1,"S3A stores actual server suggestions");
    require(game.unhandledPackets.count(0x3a)==0,"Handled completion packets no longer count as unimplemented");
    try{game.consumePacket({0x3a,mc::net::Writer().varInt(2).string("Only one").data});require(false,"Truncated completions must fail");}catch(const mc::net::ProtocolError&){}
    require(game.tabCompletions.size()==2&&game.tabCompletionRevision==revision+1,"Malformed responses cannot replace a valid completion list");
    sent.clear();require(game.updateClientSettings("en_US",16,1,false,64)&&sent.size()==1&&sent[0].id==0x15,"Changing client options sends one C15 packet");
    {mc::net::Reader body(sent[0].data);require(body.string(7)=="en_US"&&body.i8()==16&&body.u8()==1&&!body.boolean()&&body.u8()==64,"C15 sends locale, view distance, chat policy, colors and skin mask");body.requireEnd();}
    require(game.updateClientSettings("en_US",16,1,false,64)&&sent.size()==1,"Unchanged options do not flood C15 every frame");
    game.updateClientSettings("en_US",99,-3,true,255);require(game.viewDistance==32&&game.chatVisibility==0&&game.skinParts==127,"Client preferences obey the original protocol ranges");
}
void signs(){
    auto width=[](char32_t cp){return cp>127?8:6;};
    const auto line=mc::visual::signLine(u8"\u00a7cA\u00a7lB\u00a7aC\u00a7rD",width);
    require(line.glyphs.size()==4&&line.width==25,"Sign formatting codes occupy no space and bold adds one pixel");
    require(line.glyphs[0].format.color==0xff5555&&line.glyphs[1].format.bold,"Sign color and bold survive layout");
    require(line.glyphs[2].format.color==0x55ff55&&!line.glyphs[2].format.bold&&line.glyphs[3].format.color==0,"A color change clears styles; reset returns to original black");
    const auto decorated=mc::visual::signLine(u8"\u00a7o\u00a7n\u00a7m\u00a7k\u4f60",width);
    require(decorated.glyphs[0].format.italic&&decorated.glyphs[0].format.underline&&decorated.glyphs[0].format.strike&&decorated.glyphs[0].format.obfuscated,"Sign text retains every original formatting flag");
    const auto wrapped=mc::visual::signLine("hello world!",width,50);require(wrapped.width==30&&wrapped.glyphs.size()==5,"Sign text uses the first word-wrapped line at the board limit");
    require(mc::visual::signLine(std::string(20,'a'),width).glyphs.size()==15,"A single long sign word is clipped at 90 original pixels");
}
mc::nbt::Tag compound(){mc::nbt::Tag result;result.type=10;return result;}
mc::nbt::Tag list(std::vector<mc::nbt::Tag> values,int type){mc::nbt::Tag result;result.type=9;result.subtype=std::uint8_t(type);result.list=std::move(values);return result;}
mc::nbt::Tag enchant(int id,int level){auto result=compound();result.compound["id"]=mc::nbt::integer(2,id);result.compound["lvl"]=mc::nbt::integer(2,level);return result;}
void tooltips(const std::string& assets){
    mc::I18n english,chinese;require(english.load(assets,"en_US")&&chinese.load(assets,"zh_CN"),"Tooltip translations load from the original resources");
    auto name=[](const mc::ItemStack& stack){return stack.id==276?"Diamond Sword":stack.id==403?"Enchanted Book":stack.id==373?"Potion":stack.id==1?"Stone":mc::registry::item(stack.id).name;};
    mc::ItemStack sword{276,1,3,{}};auto tag=compound(),display=compound();
    display.compound["Name"]=mc::nbt::text(mc::nbt::modifiedUtf8(u8"Named \U0001f600 Sword"));
    display.compound["Lore"]=list({mc::nbt::text(mc::nbt::modifiedUtf8(u8"\u00a7aA legend")),mc::nbt::text("Second line")},8);tag.compound["display"]=display;
    tag.compound["ench"]=list({enchant(16,5),enchant(34,3),enchant(33,1)},10);sword.nbt=mc::nbt::encode(tag);
    auto rows=mc::item_tooltip::item(sword,english,name);
    require(contains(rows,u8"Named \U0001f600 Sword")&&rows.front().find(u8"\u00a7b\u00a7o")==0,"Custom names preserve Unicode, rarity color and italic style");
    require(contains(rows,"Sharpness V")&&contains(rows,"Unbreaking III")&&contains(rows,"Silk Touch")&&!contains(rows,"Silk Touch I"),"Tooltips show translated enchantment names and original max-level-one behavior");
    require(contains(rows,u8"\u00a75\u00a7o\u00a7aA legend")&&contains(rows,"Second line"),"Every Lore line retains explicit formatting over the original purple italic default");
    require(contains(rows,"+13.25 Attack Damage"),"Default attack tooltip includes Sharpness's original generic bonus");
    const auto translated=mc::item_tooltip::item(sword,chinese,name);require(contains(translated,chinese("enchantment.damage.all")),"Enchantment names follow the active language");
    rows=mc::item_tooltip::item(sword,english,name,true);require(contains(rows,"#0276")&&contains(rows,"Durability: 1558 / 1561")&&contains(rows,"minecraft:diamond_sword")&&contains(rows,"NBT: 2 tag(s)"),"F3+H shows source ID, remaining durability and actual NBT tag count");
    tag.compound["HideFlags"]=mc::nbt::integer(3,63);tag.compound["Unbreakable"]=mc::nbt::integer(1,1);sword.nbt=mc::nbt::encode(tag);rows=mc::item_tooltip::item(sword,english,name,true);
    require(!contains(rows,"Sharpness")&&!contains(rows,"Attack Damage")&&!contains(rows,"Unbreakable")&&!contains(rows,"Durability:"),"HideFlags and Unbreakable suppress their corresponding sections");
    require(contains(rows,"A legend")&&contains(rows,"Named"),"HideFlags does not suppress plugin display names or Lore");
    mc::ItemStack book{403,1,0,{}};tag=compound();tag.compound["StoredEnchantments"]=list({enchant(48,4)},10);tag.compound["ench"]=list({enchant(34,2)},10);tag.compound["HideFlags"]=mc::nbt::integer(3,1);book.nbt=mc::nbt::encode(tag);rows=mc::item_tooltip::item(book,english,name);
    require(rows.front().find(u8"\u00a7e")==0&&contains(rows,"Power IV")&&!contains(rows,"Unbreaking II"),"Stored enchantments use Uncommon rarity and the independent HideFlags bit 32");
    tag.compound["HideFlags"]=mc::nbt::integer(3,32);book.nbt=mc::nbt::encode(tag);rows=mc::item_tooltip::item(book,english,name);require(!contains(rows,"Power IV")&&contains(rows,"Unbreaking II"),"Book additional data and actual enchantments honor separate flags");
    mc::ItemStack potion{373,1,8194,{}};rows=mc::item_tooltip::item(potion,english,name);
    require(contains(rows,"Speed (3:00)")&&contains(rows,"When Applied:")&&contains(rows,"+20% Speed"),"Potion tooltip shows effect duration and its original attribute modifier");
    potion.damage=8226;rows=mc::item_tooltip::item(potion,english,name);require(contains(rows,"Speed II (1:30)")&&contains(rows,"+40% Speed"),"Potion potency changes both duration and attribute information");
    tag=compound();auto effect=compound();effect.compound["Id"]=mc::nbt::integer(1,2);effect.compound["Amplifier"]=mc::nbt::integer(1,2);effect.compound["Duration"]=mc::nbt::integer(3,100);tag.compound["CustomPotionEffects"]=list({effect},10);potion.nbt=mc::nbt::encode(tag);rows=mc::item_tooltip::item(potion,english,name);
    require(contains(rows,u8"\u00a7cSlowness III (0:05)")&&contains(rows,"-45% Speed"),"Custom harmful potions show red effects and the correct negative modifier");
    tag.compound["HideFlags"]=mc::nbt::integer(3,32);potion.nbt=mc::nbt::encode(tag);rows=mc::item_tooltip::item(potion,english,name);require(rows.size()==1,"HideFlags 32 removes item-specific potion information");
    require(mc::item_tooltip::format("%2$s / %1$s / %%",{"one","two"})=="two / one / %","Translation argument reordering and literal percent are preserved");
}
}
int main(int argc,char** argv){try{editing();completion();packets();signs();tooltips(argc>1?argv[1]:"assets");std::cout<<checks<<" GUI input, completion, sign-formatting and shared tooltip checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<"GUI input tests failed: "<<error.what()<<'\n';return 1;}}
