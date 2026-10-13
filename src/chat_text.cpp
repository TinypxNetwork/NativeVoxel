#include "mc/chat_text.hpp"
#include "mc/gui_json.hpp"
#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>

namespace mc::chat {
namespace {
using gui_json::Json;
constexpr const char* Colors[]={"black","dark_blue","dark_green","dark_aqua","dark_red","dark_purple","gold","gray","dark_gray","blue","green","aqua","red","light_purple","yellow","white"};
constexpr std::uint32_t Palette[]={0x000000,0x0000aa,0x00aa00,0x00aaaa,0xaa0000,0xaa00aa,0xffaa00,0xaaaaaa,0x555555,0x5555ff,0x55ff55,0x55ffff,0xff5555,0xff55ff,0xffff55,0xffffff};
const std::string Section="\xc2\xa7";
struct Unit {std::uint16_t value;std::size_t begin,end;};
std::vector<Unit> units(const std::string& text){std::vector<Unit> result;for(std::size_t i=0;i<text.size();){const auto begin=i;unsigned cp=static_cast<unsigned char>(text[i++]);int extra=0;if(cp>=0xf0){cp&=7;extra=3;}else if(cp>=0xe0){cp&=15;extra=2;}else if(cp>=0xc0){cp&=31;extra=1;}for(int n=0;n<extra;++n){if(i>=text.size()||(static_cast<unsigned char>(text[i])&0xc0)!=0x80){cp=0xfffd;break;}cp=(cp<<6)|(static_cast<unsigned char>(text[i++])&63);}if(cp>0xffff){cp-=0x10000;result.push_back({static_cast<std::uint16_t>(0xd800+(cp>>10)),begin,i});result.push_back({static_cast<std::uint16_t>(0xdc00+(cp&1023)),begin,i});}else result.push_back({static_cast<std::uint16_t>(cp),begin,i});}return result;}
bool primitive(const Json& value){return value.kind==Json::Kind::String||value.kind==Json::Kind::Number||value.kind==Json::Kind::Boolean;}
std::string string(const Json& value){if(!primitive(value))throw net::ProtocolError("Expected primitive chat component field");return value.value;}
std::string lower(std::string value){for(char& c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return value;}
std::string translationFormat(const std::string& input){
    // LanguageMap replaces numeric Java format specifiers before components
    // parse their %s arguments: %(\d+\$)?[\d\.]*[df] -> %$1s.
    std::string out;for(std::size_t i=0;i<input.size();){if(input[i]!='%'){out+=input[i++];continue;}std::size_t at=i+1;while(at<input.size()&&input[at]>='0'&&input[at]<='9')++at;std::string index;if(at>i+1&&at<input.size()&&input[at]=='$'){index=input.substr(i+1,at-i);++at;}else at=i+1;while(at<input.size()&&((input[at]>='0'&&input[at]<='9')||input[at]=='.'))++at;if(at<input.size()&&(input[at]=='d'||input[at]=='f')){out+='%'+index+'s';i=at+1;}else out+=input[i++];}return out;
}
std::string serialize(const Json& value){if(value.kind==Json::Kind::String)return escapeJsonString(value.value);if(value.kind==Json::Kind::Null)return "null";if(primitive(value))return value.value;std::string out=value.kind==Json::Kind::Array?"[":"{";bool comma=false;if(value.kind==Json::Kind::Array){for(const auto& child:value.array){if(comma)out+=',';comma=true;out+=serialize(child);}}else for(const auto& child:value.object){if(comma)out+=',';comma=true;out+=escapeJsonString(child.first)+":"+serialize(child.second);}out+=value.kind==Json::Kind::Array?"]":"}";return out;}
Style style(const Json& object,Style parent){
    if(const auto* value=object.get("color")){if(value->kind!=Json::Kind::Null){const auto name=lower(string(*value));for(int i=0;i<16;++i)if(name==Colors[i])parent.color="0123456789abcdef"[i];if(name=="reset")parent.color='r';}}
    for(const auto& field:{std::pair<const char*,bool*>{"bold",&parent.bold},{"italic",&parent.italic},{"underlined",&parent.underlined},{"strikethrough",&parent.strikethrough},{"obfuscated",&parent.obfuscated}})if(const auto* value=object.get(field.first))*field.second=lower(string(*value))=="true";
    if(const auto* value=object.get("insertion"))parent.insertion=string(*value);
    if(const auto* event=object.get("clickEvent")){if(event->kind==Json::Kind::Object){const auto* action=event->get("action"),*value=event->get("value");if(action&&value&&primitive(*action)&&primitive(*value)){const auto& name=action->value;if(name=="open_url"||name=="run_command"||name=="suggest_command"||name=="change_page")parent.click=ClickEvent{name,value->value};}}}
    if(const auto* event=object.get("hoverEvent")){if(event->kind==Json::Kind::Object){const auto* action=event->get("action"),*value=event->get("value");if(action&&value&&primitive(*action)){const auto& name=action->value;if(name=="show_text"||name=="show_item"||name=="show_entity"||name=="show_achievement")parent.hover=HoverEvent{name,serialize(*value)};}}}
    return parent;
}
Style rootStyle(const Json& value,const Style& parent){if(value.kind==Json::Kind::Object)return style(value,parent);if(value.kind==Json::Kind::Array&&!value.array.empty())return rootStyle(value.array.front(),parent);return parent;}
class Builder {
public:
    Document result;const Translator& translate;const ScoreResolver& score;std::size_t nextComponent{};
    Builder(const Translator& t,const ScoreResolver& s):translate(t),score(s){}
    void append(std::string text,const Style& style){result.spans.push_back({std::move(text),style,nextComponent++});if(result.spans.size()>32768)throw net::ProtocolError("Chat component expansion limit");}
    void component(const Json& value,const Style& parent,unsigned depth=0){
        if(depth>64)throw net::ProtocolError("Chat component nesting limit");
        if(primitive(value)){append(value.value,parent);return;}
        if(value.kind==Json::Kind::Array){if(value.array.empty())return;component(value.array.front(),parent,depth+1);const auto inherited=rootStyle(value.array.front(),parent);for(std::size_t i=1;i<value.array.size();++i)component(value.array[i],inherited,depth+1);return;}
        if(value.kind!=Json::Kind::Object)throw net::ProtocolError("Invalid chat component");
        const auto inherited=style(value,parent);
        if(const auto* text=value.get("text"))append(string(*text),inherited);
        else if(const auto* key=value.get("translate")){
            const auto name=string(*key);const auto format=translationFormat(translate?translate(name):name);const auto* args=value.get("with");if(args&&args->kind!=Json::Kind::Array)throw net::ProtocolError("Translation arguments must be an array");std::size_t sequential=0,start=0;
            for(std::size_t at=0;at<format.size();){if(format[at]!='%'){++at;continue;}if(at>start)append(format.substr(start,at-start),inherited);const auto begin=at++;if(at<format.size()&&format[at]=='%'){append("%",inherited);start=++at;continue;}
                std::size_t index=0,digits=at;while(at<format.size()&&format[at]>='0'&&format[at]<='9'){index=index*10+static_cast<unsigned>(format[at++]-'0');if(index>32768)throw net::ProtocolError("Translation argument index limit");}
                if(at>digits){if(at>=format.size()||format[at]!='$'||index==0)throw net::ProtocolError("Invalid translation index");--index;++at;}else index=sequential++;
                if(at>=format.size()||format[at]!='s')throw net::ProtocolError("Unsupported translation format at "+std::to_string(begin));
                if(!args||index>=args->array.size())throw net::ProtocolError("Missing translation argument");component(args->array[index],inherited,depth+1);start=++at;
            }
            if(start<format.size())append(format.substr(start),inherited);
        }else if(const auto* scoreObject=value.get("score")){
            const auto* name=scoreObject->get("name"),*objective=scoreObject->get("objective");if(!name||!objective)throw net::ProtocolError("Score component requires name/objective");const auto* content=scoreObject->get("value");append(content?string(*content):(score?score(string(*name),string(*objective)):""),inherited);
        }else if(const auto* selector=value.get("selector"))append(string(*selector),inherited);
        else throw net::ProtocolError("Unknown chat component kind");
        if(const auto* extra=value.get("extra")){if(extra->kind!=Json::Kind::Array||extra->array.empty())throw net::ProtocolError("Chat extra must be a nonempty array");for(const auto& child:extra->array)component(child,inherited,depth+1);}
    }
};
}

std::string Style::formatting() const {std::string out;if(color){out+=Section;out+=color;}if(bold)out+=Section+'l';if(italic)out+=Section+'o';if(underlined)out+=Section+'n';if(obfuscated)out+=Section+'k';if(strikethrough)out+=Section+'m';return out;}
std::uint32_t Style::rgb(std::uint32_t fallback) const {const auto at=std::string("0123456789abcdef").find(color);return color&&at!=std::string::npos?Palette[at]:fallback;}
std::string Document::plain(bool removeFormatting) const {std::string out;for(const auto& span:spans)out+=span.text;return removeFormatting?stripFormatting(out):out;}
std::string Document::formatted() const {std::string out;for(const auto& span:spans)if(!span.text.empty())out+=span.style.formatting()+span.text+Section+'r';return out;}
std::string escapeJsonString(const std::string& text){std::string out="\"";static constexpr char hex[]="0123456789abcdef";for(unsigned char c:text){if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=static_cast<char>(c);}return out+'"';}
Document literal(const std::string& text,const Style& style){Document value;value.spans.push_back({text,style,0});return value;}
Document parse(const std::string& json,const Translator& translate,const ScoreResolver& score){try{Builder builder(translate,score);builder.component(gui_json::JsonParser(json).parse(),{});return std::move(builder.result);}catch(const std::exception& error){auto value=literal(json);value.valid=false;value.error=error.what();return value;}}
std::string stripFormatting(const std::string& text){std::string out;const auto chars=units(text);for(std::size_t i=0;i<chars.size();++i){if(chars[i].value==167&&i+1<chars.size()){const char c=static_cast<char>(std::tolower(chars[i+1].value&255));if((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='k'&&c<='o')||c=='r'){++i;continue;}}if(i==0||chars[i].begin!=chars[i-1].begin)out+=text.substr(chars[i].begin,chars[i].end-chars[i].begin);}return out;}
int Metrics::charWidth(std::uint16_t codepoint) const {if(font)return font->charWidth(codepoint,unicode);if(codepoint==167)return -1;if(codepoint==32)return 4;if(codepoint=='\n'||codepoint=='\r')return 0;return fallbackWidth;}
int Metrics::width(const std::string& text) const {const auto chars=units(text);int total=0;bool bold=false;for(std::size_t i=0;i<chars.size();++i){int value=charWidth(chars[i].value);if(value<0&&i+1<chars.size()){const auto c=chars[++i].value;if(c=='l'||c=='L')bold=true;else if(c=='r'||c=='R')bold=false;value=0;}total+=value;if(bold&&value>0)++total;}return total;}
std::string Metrics::trim(const std::string& text,int limit) const {const auto chars=units(text);int total=0;bool marker=false,bold=false;std::size_t bytes=0;for(const auto& unit:chars){if(total>=limit)break;const int width=charWidth(unit.value);if(marker){marker=false;if(unit.value=='l'||unit.value=='L')bold=true;else if(unit.value=='r'||unit.value=='R')bold=false;}else if(width<0)marker=true;else{total+=width;if(bold)++total;}if(total>limit)break;bytes=unit.end;}return text.substr(0,bytes);}
std::string Line::formatted()const{Document value;value.spans=spans;return value.formatted();}
std::string Line::plain()const{Document value;value.spans=spans;return value.plain();}
std::vector<std::string> wrapFormatted(const std::string& text,int maximum,const Metrics& metrics){
    // FontRenderer.sizeStringToWidth / getFormatFromString. GUI strings use a
    // different wrapping rule from GuiUtilRenderComponents.splitText: legacy
    // formatting survives each line break, and the separating space is eaten.
    maximum=std::max(1,maximum);std::vector<std::string> lines;std::string rest=text;
    for(;;){
        if(lines.size()>65536)throw net::ProtocolError("Wrapped text line limit");
        const auto chars=units(rest);std::size_t at=0,space=std::string::npos;int width=0;bool bold=false;
        for(;at<chars.size();++at){const auto cp=chars[at].value;
            if(cp=='\n'){space=at;break;}
            if(cp==167){if(at+1<chars.size()){const auto code=static_cast<char>(std::tolower(chars[++at].value&255));if(code=='l')bold=true;else if(code=='r'||(code>='0'&&code<='9')||(code>='a'&&code<='f'))bold=false;}continue;}
            if(cp==' ')space=at;
            width+=metrics.charWidth(cp)+(bold?1:0);
            if(width>maximum)break;
        }
        if(at==chars.size()){lines.push_back(rest);break;}
        if(space!=std::string::npos&&space<at)at=space;
        std::size_t split=chars[at].begin,skip=chars[at].value==' '||chars[at].value=='\n'?chars[at].end:split;
        // A character wider than the GUI must still make progress. Keep a UTF-8
        // supplementary character intact even though Minecraft measures UTF-16.
        if(split==0&&skip==0){split=skip=chars[at].end;}
        else if(skip==split&&stripFormatting(rest.substr(0,split)).empty())split=skip=chars[at].end;
        const auto prefix=rest.substr(0,split);std::string format;const auto prefixChars=units(prefix);
        for(std::size_t i=0;i+1<prefixChars.size();++i)if(prefixChars[i].value==167){const auto code=static_cast<char>(prefixChars[++i].value);const auto lowerCode=static_cast<char>(std::tolower(static_cast<unsigned char>(code)));if((lowerCode>='0'&&lowerCode<='9')||(lowerCode>='a'&&lowerCode<='f'))format=Section+code;else if((lowerCode>='k'&&lowerCode<='o')||lowerCode=='r')format+=Section+code;}
        lines.push_back(prefix);rest=format+rest.substr(skip);
    }
    // Java String.split drops trailing empty strings, but preserves the single
    // empty result when the input itself was empty.
    if(!text.empty())while(!lines.empty()&&lines.back().empty())lines.pop_back();
    return lines;
}
std::vector<Line> wrap(const Document& document,int maximum,const Metrics& metrics,bool trimSpace,bool colors){
    maximum=std::max(maximum,1);std::vector<Span> queue=document.spans;std::vector<Line> lines;Line line;
    for(std::size_t i=0;i<queue.size();++i){if(queue.size()>65536)throw net::ProtocolError("Wrapped chat line limit");auto current=queue[i];auto text=current.text;bool flush=false;
        const auto newline=text.find('\n');if(newline!=std::string::npos){auto rest=current;rest.text=text.substr(newline+1);text.resize(newline+1);queue.insert(queue.begin()+static_cast<std::ptrdiff_t>(i+1),std::move(rest));flush=true;}
        auto formatted=current.style.formatting()+text;if(!colors)formatted=stripFormatting(formatted);auto visible=formatted;if(!visible.empty()&&visible.back()=='\n')visible.pop_back();int width=metrics.width(visible);
        if(line.width+width>maximum){auto prefix=metrics.trim(formatted,maximum-line.width);auto rest=prefix.size()<formatted.size()?formatted.substr(prefix.size()):std::string{};
            if(!rest.empty()){const auto space=prefix.find_last_of(' ');if(space!=std::string::npos&&metrics.width(formatted.substr(0,space))>0){prefix=formatted.substr(0,space);rest=formatted.substr(space+(trimSpace?1:0));}else if(line.width>0&&formatted.find(' ')==std::string::npos){prefix.clear();rest=formatted;}
                if(line.width==0&&stripFormatting(prefix).empty()&&stripFormatting(rest)==stripFormatting(formatted)){const auto chars=units(rest);std::size_t end=0;bool marker=false;for(const auto& unit:chars){end=unit.end;if(marker){marker=false;continue;}if(unit.value==167){marker=true;continue;}break;}prefix+=rest.substr(0,end);rest.erase(0,end);}
                if(!rest.empty()){auto continuation=current;continuation.text=std::move(rest);queue.insert(queue.begin()+static_cast<std::ptrdiff_t>(i+1),std::move(continuation));}
            }
            visible=std::move(prefix);width=metrics.width(visible);flush=true;
        }
        if(line.width+width<=maximum||line.width==0){current.text=std::move(visible);if(!colors){current.style.color=0;current.style.bold=current.style.italic=current.style.underlined=current.style.strikethrough=current.style.obfuscated=false;}line.spans.push_back(std::move(current));line.width+=width;}else flush=true;
        if(flush){lines.push_back(std::move(line));line=Line{};}
    }
    lines.push_back(std::move(line));return lines;
}
ActionRequest click(const Style& style,bool shift,bool allowLinks){
    if(shift)return style.insertion?ActionRequest{ActionRequest::Kind::InsertText,*style.insertion,{}}:ActionRequest{};
    if(!style.click)return {};
    const auto& event=*style.click;
    if(event.action=="run_command")return {ActionRequest::Kind::RunCommand,event.value,{}};
    if(event.action=="suggest_command")return {ActionRequest::Kind::SuggestCommand,event.value,{}};
    if(event.action=="change_page")return {ActionRequest::Kind::ChangePage,event.value,{}};
    if(event.action=="open_url"&&allowLinks){const auto colon=event.value.find(':');const auto scheme=lower(event.value.substr(0,colon));if(colon==std::string::npos||(scheme!="http"&&scheme!="https"))return {ActionRequest::Kind::None,{},"Unsupported URL protocol"};for(unsigned char c:event.value)if(c<=32||c==127)return {ActionRequest::Kind::None,{},"Invalid URL"};return {ActionRequest::Kind::OpenUrl,event.value,{}};}
    return {};
}

} // namespace mc::chat
