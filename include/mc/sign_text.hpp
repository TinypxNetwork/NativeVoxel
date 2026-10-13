#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mc::visual {
struct SignFormat {
    std::uint32_t color=0;
    bool bold=false,italic=false,underline=false,strike=false,obfuscated=false;
    void code(char32_t value){
        if(value>='A'&&value<='Z')value+=32;
        const int colorIndex=value>='0'&&value<='9'?int(value-'0'):value>='a'&&value<='f'?int(value-'a'+10):-1;
        if(colorIndex>=0){
            static const std::uint32_t colors[]={0x000000,0x0000aa,0x00aa00,0x00aaaa,0xaa0000,0xaa00aa,0xffaa00,0xaaaaaa,0x555555,0x5555ff,0x55ff55,0x55ffff,0xff5555,0xff55ff,0xffff55,0xffffff};
            *this={};color=colors[colorIndex];
        }else if(value=='r')*this={};
        else if(value=='l')bold=true;else if(value=='o')italic=true;else if(value=='n')underline=true;
        else if(value=='m')strike=true;else if(value=='k')obfuscated=true;
        else {*this={};color=0xffffff;}
    }
};
struct SignGlyph {char32_t codepoint{};float x{},advance{};SignFormat format;};
struct SignLine {float width{};std::vector<SignGlyph> glyphs;};
inline SignLine signLine(const std::string& formatted,const std::function<int(char32_t)>& charWidth,float maximum=90) {
    auto next=[&](std::size_t& at){
        const auto byte=static_cast<unsigned char>(formatted[at++]);char32_t cp=byte;int count=0;
        if(byte>=0xf0){cp=byte&7;count=3;}else if(byte>=0xe0){cp=byte&15;count=2;}else if(byte>=0xc0){cp=byte&31;count=1;}
        while(count--&&at<formatted.size())cp=(cp<<6)|(static_cast<unsigned char>(formatted[at++])&63);return cp;
    };
    SignLine result;SignFormat style;std::size_t lastSpace=0;bool hasSpace=false;
    for(std::size_t i=0;i<formatted.size();) {
        auto cp=next(i);if(cp==0xa7&&i<formatted.size()){style.code(next(i));continue;}
        if(cp=='\n')break;
        if(cp>0xffff)continue;
        const float advance=float(charWidth(cp)+(style.bold?1:0));
        if(result.width+advance>maximum){
            if(hasSpace&&lastSpace>0){result.width=result.glyphs[lastSpace].x;result.glyphs.resize(lastSpace);}break;
        }
        if(cp==' '){lastSpace=result.glyphs.size();hasSpace=true;}
        result.glyphs.push_back({cp,result.width,advance,style});result.width+=advance;
    }
    return result;
}
} // namespace mc::visual
