#pragma once

#include "mc/protocol.hpp"
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace mc::nbt {

// Strings retain Java modified-UTF-8 bytes. Compound ordering is immaterial to
// equality; list ordering and tag types remain significant.
struct Tag {
    int type{},subtype{};
    std::int64_t integer{};
    double number{};
    std::string string;
    std::vector<std::uint8_t> bytes;
    std::vector<Tag> list;
    std::map<std::string,Tag> compound;
    const Tag* get(const std::string& key) const { const auto i=compound.find(key);return i==compound.end()?nullptr:&i->second; }
    bool operator==(const Tag& other) const {
        if(type!=other.type)return false;
        switch(type) {
        case 0:return true;
        case 1:case 2:case 3:case 4:return integer==other.integer;
        case 5:case 6:return number==other.number;
        case 7:case 11:return bytes==other.bytes;
        case 8:return string==other.string;
        case 9:return subtype==other.subtype&&list==other.list;
        case 10:return compound==other.compound;
        default:return false;
        }
    }
};
inline std::string readString(net::Reader& reader) {
    const auto value=reader.bytes(reader.u16());return std::string(value.begin(),value.end());
}
inline Tag readPayload(net::Reader& reader,int type,int depth,std::size_t& nodes) {
    if(depth>512||++nodes>65536)throw net::ProtocolError("NBT tree limit exceeded");
    Tag result;result.type=type;
    switch(type) {
    case 0:break;
    case 1:result.integer=reader.i8();break;
    case 2:result.integer=reader.i16();break;
    case 3:result.integer=reader.i32();break;
    case 4:result.integer=reader.i64();break;
    case 5:result.number=reader.f32();break;
    case 6:result.number=reader.f64();break;
    case 7:case 11: {
        const int count=reader.i32();const std::size_t width=type==7?1:4;
        if(count<0||static_cast<std::size_t>(count)>reader.remaining()/width)throw net::ProtocolError("Invalid NBT array length");
        result.bytes=reader.bytes(static_cast<std::size_t>(count)*width);break;
    }
    case 8:result.string=readString(reader);break;
    case 9: {
        result.subtype=reader.u8();const int count=reader.i32();
        if(count<0||count>65536||result.subtype>11||(count&&!result.subtype))throw net::ProtocolError("Invalid NBT list");
        result.list.reserve(static_cast<std::size_t>(count));
        for(int i=0;i<count;++i)result.list.push_back(readPayload(reader,result.subtype,depth+1,nodes));
        break;
    }
    case 10:for(;;){const int child=reader.u8();if(!child)break;const auto name=readString(reader);result.compound[name]=readPayload(reader,child,depth+1,nodes);}break;
    default:throw net::ProtocolError("Unknown 1.8.9 NBT tag type");
    }
    return result;
}
inline Tag parse(const std::vector<std::uint8_t>& bytes) {
    if(bytes.empty())return {};
    net::Reader reader(bytes);const int type=reader.u8();
    if(!type){reader.requireEnd();return {};}
    if(type!=10)throw net::ProtocolError("Expected NBT compound root");
    readString(reader);std::size_t nodes=0;auto result=readPayload(reader,type,0,nodes);reader.requireEnd();return result;
}
inline void writeString(net::Writer& writer,const std::string& value) {
    if(value.size()>65535)throw net::ProtocolError("Modified UTF-8 string exceeds NBT limit");
    writer.u16(static_cast<std::uint16_t>(value.size())).bytes(reinterpret_cast<const std::uint8_t*>(value.data()),value.size());
}
inline void writePayload(net::Writer& writer,const Tag& tag,int depth,std::size_t& nodes) {
    if(depth>512||++nodes>65536)throw net::ProtocolError("NBT tree limit exceeded");
    switch(tag.type) {
    case 0:break;
    case 1:writer.i8(static_cast<std::int8_t>(tag.integer));break;
    case 2:writer.i16(static_cast<std::int16_t>(tag.integer));break;
    case 3:writer.i32(static_cast<std::int32_t>(tag.integer));break;
    case 4:writer.i64(tag.integer);break;
    case 5:writer.f32(static_cast<float>(tag.number));break;
    case 6:writer.f64(tag.number);break;
    case 7:writer.i32(static_cast<std::int32_t>(tag.bytes.size())).bytes(tag.bytes);break;
    case 8:writeString(writer,tag.string);break;
    case 9:
        if(tag.list.size()>65536)throw net::ProtocolError("NBT list limit exceeded");
        writer.u8(static_cast<std::uint8_t>(tag.subtype)).i32(static_cast<std::int32_t>(tag.list.size()));
        for(const auto& child:tag.list){if(child.type!=tag.subtype)throw net::ProtocolError("NBT list type mismatch");writePayload(writer,child,depth+1,nodes);}break;
    case 10:
        for(const auto& pair:tag.compound){if(pair.second.type<=0||pair.second.type>11)throw net::ProtocolError("Invalid compound child type");writer.u8(static_cast<std::uint8_t>(pair.second.type));writeString(writer,pair.first);writePayload(writer,pair.second,depth+1,nodes);}writer.u8(0);break;
    case 11:
        if(tag.bytes.size()%4)throw net::ProtocolError("Invalid NBT int-array byte count");
        writer.i32(static_cast<std::int32_t>(tag.bytes.size()/4)).bytes(tag.bytes);break;
    default:throw net::ProtocolError("Unknown NBT tag type");
    }
}
inline std::vector<std::uint8_t> encode(const Tag& root) {
    net::Writer writer;
    if(root.type==0)return {0};
    if(root.type!=10)throw net::ProtocolError("Expected NBT compound root");
    writer.u8(10).u16(0);std::size_t nodes=0;writePayload(writer,root,0,nodes);return std::move(writer.data);
}
inline Tag integer(int type,std::int64_t value){Tag result;result.type=type;result.integer=value;return result;}
inline Tag text(const std::string& modifiedUtf8){Tag result;result.type=8;result.string=modifiedUtf8;return result;}
inline bool subset(const Tag& expected,const Tag& actual) {
    if(expected.type==0)return true;
    if(expected.type!=actual.type)return false;
    if(expected.type!=10)return expected==actual;
    for(const auto& field:expected.compound){const auto* value=actual.get(field.first);if(!value||!subset(field.second,*value))return false;}return true;
}
inline std::string modifiedUtf8(const std::string& utf8) {
    std::string result;
    auto unit=[&](unsigned value){if(value>0&&value<128)result.push_back(static_cast<char>(value));else if(value<2048){result.push_back(static_cast<char>(0xc0|(value>>6)));result.push_back(static_cast<char>(0x80|(value&63)));}else{result.push_back(static_cast<char>(0xe0|(value>>12)));result.push_back(static_cast<char>(0x80|((value>>6)&63)));result.push_back(static_cast<char>(0x80|(value&63)));}};
    // The network Writer validates UTF-8 and Java UTF-16 length first.
    net::Writer check;check.string(utf8);
    for(std::size_t i=0;i<utf8.size();) {
        unsigned cp=static_cast<unsigned char>(utf8[i++]);int extra=0;
        if(cp>=0xf0){cp&=7;extra=3;}else if(cp>=0xe0){cp&=15;extra=2;}else if(cp>=0xc0){cp&=31;extra=1;}
        for(int j=0;j<extra;++j)cp=(cp<<6)|(static_cast<unsigned char>(utf8[i++])&63);
        if(cp<=65535)unit(cp);else{cp-=65536;unit(0xd800+(cp>>10));unit(0xdc00+(cp&1023));}
    }
    return result;
}

} // namespace mc::nbt
