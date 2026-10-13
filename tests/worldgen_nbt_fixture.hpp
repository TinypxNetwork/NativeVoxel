#pragma once
#include "mc/nbt.hpp"
#include <cstring>
#include <ostream>
#include <stdexcept>
namespace mc::worldgen::test {
inline void hex(std::ostream& out,const std::string& value){static constexpr char digits[]="0123456789abcdef";for(unsigned char byte:value){out.put(digits[byte>>4]);out.put(digits[byte&15]);}}
inline void printTag(std::ostream& out,const nbt::Tag& tag){
    out<<tag.type<<':';switch(tag.type){
    case 0:break;
    case 1:case 2:case 3:case 4:out<<tag.integer;break;
    case 5:{const float number=static_cast<float>(tag.number);std::uint32_t bits;std::memcpy(&bits,&number,4);out<<bits;break;}
    case 6:{std::uint64_t bits;std::memcpy(&bits,&tag.number,8);out<<bits;break;}
    case 7:case 11:hex(out,std::string(tag.bytes.begin(),tag.bytes.end()));break;
    case 8:hex(out,tag.string);break;
    case 9:out<<tag.subtype<<'[';for(const auto& value:tag.list){printTag(out,value);out<<';';}out<<']';break;
    case 10:out<<'{';for(const auto& pair:tag.compound){hex(out,pair.first);out<<'=';printTag(out,pair.second);out<<';';}out<<'}';break;
    default:throw std::runtime_error("unsupported fixture NBT type");
    }
}
}
