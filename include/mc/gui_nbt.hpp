#pragma once
#include "mc/nbt.hpp"
#include "mc/gui_json.hpp"
#include <cctype>
#include <cmath>
#include <limits>

namespace mc::gui_nbt {
// NBT uses Java modified UTF-8. GUI strings use normal UTF-8, including
// supplementary characters represented in NBT by a pair of UTF-16 surrogates.
inline std::string utf8(const std::string& bytes) {
    std::vector<unsigned> units;
    for(std::size_t i=0;i<bytes.size();) {
        unsigned cp=static_cast<unsigned char>(bytes[i++]);int count=0;
        if(cp>=0xe0&&cp<0xf0){cp&=15;count=2;}
        else if(cp>=0xc0&&cp<0xe0){cp&=31;count=1;}
        else if(cp>=128)throw net::ProtocolError("Invalid modified UTF-8");
        for(int n=0;n<count;++n){if(i>=bytes.size()||(static_cast<unsigned char>(bytes[i])&0xc0)!=0x80)throw net::ProtocolError("Invalid modified UTF-8");cp=(cp<<6)|(static_cast<unsigned char>(bytes[i++])&63);}
        units.push_back(cp);
    }
    std::string out;
    for(std::size_t i=0;i<units.size();++i){unsigned cp=units[i];if(cp>=0xd800&&cp<=0xdbff&&i+1<units.size()&&units[i+1]>=0xdc00&&units[i+1]<=0xdfff)cp=0x10000+((cp-0xd800)<<10)+units[++i]-0xdc00;else if(cp>=0xd800&&cp<=0xdfff)cp=0xfffd;gui_json::appendUtf8(out,cp);}
    return out;
}
inline std::string string(const nbt::Tag& root,const char* key){const auto* field=root.get(key);return field&&field->type==8?utf8(field->string):std::string{};}

// Minecraft 1.8.9 JsonToNBT syntax used by show_item/show_entity. These values
// are data only. Bound the parser independently of the chat JSON parser.
class Parser {
public:
    explicit Parser(const std::string& text):text_(text){}
    nbt::Tag parse(){if(text_.size()>131068)fail("Hover NBT length limit");auto root=value(0);space();if(pos_!=text_.size()||root.type!=10)fail("Expected hover NBT compound");return root;}
private:
    const std::string& text_;std::size_t pos_{},nodes_{};
    [[noreturn]] static void fail(const char* why){throw net::ProtocolError(why);}
    void space(){while(pos_<text_.size()&&static_cast<unsigned char>(text_[pos_])<=32)++pos_;}
    bool take(char c){space();if(pos_<text_.size()&&text_[pos_]==c){++pos_;return true;}return false;}
    std::string quoted(){if(!take('"'))fail("Expected NBT quoted string");std::string out;while(pos_<text_.size()){char c=text_[pos_++];if(c=='"')return out;if(c=='\\'&&pos_<text_.size()&&(text_[pos_]=='"'||text_[pos_]=='\\'))c=text_[pos_++];out+=c;}fail("Unterminated NBT string");}
    std::string token(bool key=false){space();const auto begin=pos_;while(pos_<text_.size()&&text_[pos_]!=','&&text_[pos_]!='}'&&text_[pos_]!=']'&&(!key||text_[pos_]!=':'))++pos_;auto end=pos_;while(end>begin&&static_cast<unsigned char>(text_[end-1])<=32)--end;if(end==begin)fail("Empty NBT value");return text_.substr(begin,end-begin);}
    nbt::Tag primitive(const std::string& text){
        std::string lower=text;for(char& c:lower)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if(lower=="true"||lower=="false")return nbt::integer(1,lower=="true");
        std::string number=text;char suffix=lower.empty()?0:lower.back();int type=3;
        if(suffix=='b'||suffix=='s'||suffix=='l'||suffix=='f'||suffix=='d'){number.pop_back();type=suffix=='b'?1:suffix=='s'?2:suffix=='l'?4:suffix=='f'?5:6;}
        else if(number.find('.')!=std::string::npos)type=6;
        bool numeric=!number.empty(),digit=false;int dots=0;
        for(std::size_t i=0;i<number.size();++i){const char c=number[i];if(c>='0'&&c<='9')digit=true;else if(c=='.'&&type>=5&&++dots<=1){}else if(i==0&&(c=='-'||c=='+')){}else numeric=false;}
        try{if(numeric&&digit){std::size_t end=0;if(type>=5){const double x=std::stod(number,&end);if(end==number.size()){nbt::Tag out;out.type=type;out.number=type==5?static_cast<float>(x):x;return out;}}
            else{const auto x=std::stoll(number,&end);const auto lo=type==1?-128LL:type==2?-32768LL:type==3?static_cast<long long>(std::numeric_limits<int>::min()):std::numeric_limits<long long>::min();const auto hi=type==1?127LL:type==2?32767LL:type==3?static_cast<long long>(std::numeric_limits<int>::max()):std::numeric_limits<long long>::max();if(end==number.size()&&x>=lo&&x<=hi)return nbt::integer(type,x);}}}catch(const std::exception&){}
        return nbt::text(nbt::modifiedUtf8(text));
    }
    nbt::Tag value(unsigned depth){
        if(depth>64||++nodes_>32768)fail("Hover NBT tree limit");space();if(pos_==text_.size())fail("Missing NBT value");
        if(text_[pos_]=='"')return nbt::text(nbt::modifiedUtf8(quoted()));
        if(take('{')){nbt::Tag out;out.type=10;if(take('}'))return out;for(;;){space();const auto key=text_[pos_]=='"'?quoted():token(true);if(!take(':'))fail("Expected NBT member separator");out.compound[nbt::modifiedUtf8(key)]=value(depth+1);if(take('}'))break;if(!take(','))fail("Expected NBT compound separator");if(take('}'))break;}return out;}
        if(take('[')){nbt::Tag out;out.type=9;if(take(']'))return out;bool indexed=false;for(;;){space();const auto start=pos_;while(pos_<text_.size()&&std::isdigit(static_cast<unsigned char>(text_[pos_])))++pos_;if(pos_>start&&take(':'))indexed=true;else pos_=start;auto child=value(depth+1);if(!out.subtype)out.subtype=child.type;if(child.type==out.subtype)out.list.push_back(std::move(child));if(take(']'))break;if(!take(','))fail("Expected NBT list separator");if(take(']'))break;}
            if(!indexed&&out.subtype==3){nbt::Tag array;array.type=11;net::Writer bytes;for(const auto& child:out.list)bytes.i32(static_cast<std::int32_t>(child.integer));array.bytes=std::move(bytes.data);return array;}return out;}
        return primitive(token());
    }
};
}
