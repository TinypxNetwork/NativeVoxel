#include "mc/audio.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

// The pinned upstream decoder is compiled once, inside this translation unit.
// No extra import library, decoder DLL, or Java runtime is needed.
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "../third_party/stb_vorbis.c"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
// stb's internal channel-mask macros otherwise collide with Win32 declarations.
#undef C
#undef L
#undef R
#undef TRUE
#undef FALSE

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace mc {
namespace {
struct AudioError : std::runtime_error { using std::runtime_error::runtime_error; };

struct Json {
    enum Type { Null, Boolean, Number, String, Array, Object } type=Null;
    bool boolean=false;
    double number=0;
    std::string string;
    std::vector<Json> array;
    std::map<std::string,Json> object;
    const Json* find(const std::string& key) const {
        auto it=object.find(key);return it==object.end()?nullptr:&it->second;
    }
    const Json& required(const std::string& key) const {
        auto value=find(key);if(!value)throw AudioError("Missing sound JSON field: "+key);return *value;
    }
    std::string text() const { if(type!=String)throw AudioError("Expected JSON string");return string; }
    bool flag() const { if(type!=Boolean)throw AudioError("Expected JSON boolean");return boolean; }
    double numeric() const { if(type!=Number)throw AudioError("Expected JSON number");return number; }
};

class JsonParser {
    const std::string& text_;
    std::size_t cursor_=0,nodes_=0;
    [[noreturn]] void fail(const char* message) const {
        throw AudioError(std::string(message)+" at JSON byte "+std::to_string(cursor_));
    }
    void space(){while(cursor_<text_.size()&&(text_[cursor_]==' '||text_[cursor_]=='\t'||text_[cursor_]=='\r'||text_[cursor_]=='\n'))++cursor_;}
    bool take(char c){space();if(cursor_<text_.size()&&text_[cursor_]==c){++cursor_;return true;}return false;}
    unsigned hex4() {
        unsigned code=0;
        for(int i=0;i<4;++i) {
            if(cursor_==text_.size())fail("Truncated Unicode escape");
            char c=text_[cursor_++];unsigned digit;
            if(c>='0'&&c<='9')digit=c-'0';else if(c>='A'&&c<='F')digit=c-'A'+10;
            else if(c>='a'&&c<='f')digit=c-'a'+10;else fail("Invalid Unicode escape");
            code=code*16+digit;
        }
        return code;
    }
    static void utf8(std::string& out,unsigned code) {
        if(code<0x80)out+=char(code);
        else if(code<0x800){out+=char(0xc0|(code>>6));out+=char(0x80|(code&63));}
        else if(code<0x10000){out+=char(0xe0|(code>>12));out+=char(0x80|((code>>6)&63));out+=char(0x80|(code&63));}
        else {out+=char(0xf0|(code>>18));out+=char(0x80|((code>>12)&63));out+=char(0x80|((code>>6)&63));out+=char(0x80|(code&63));}
    }
    std::string string() {
        if(!take('"'))fail("Expected string");
        std::string result;
        while(cursor_<text_.size()) {
            if(result.size()>1024*1024)fail("JSON string too long");
            unsigned char c=static_cast<unsigned char>(text_[cursor_++]);
            if(c=='"')return result;
            if(c<32)fail("Unescaped string control character");
            if(c!='\\'){result+=char(c);continue;}
            if(cursor_==text_.size())fail("Truncated string escape");
            switch(text_[cursor_++]) {
            case '"':result+='"';break;case '\\':result+='\\';break;case '/':result+='/';break;
            case 'b':result+='\b';break;case 'f':result+='\f';break;case 'n':result+='\n';break;case 'r':result+='\r';break;case 't':result+='\t';break;
            case 'u': {
                unsigned code=hex4();
                if(code>=0xd800&&code<=0xdbff) {
                    if(cursor_+2>text_.size()||text_[cursor_]!='\\'||text_[cursor_+1]!='u')fail("Missing low Unicode surrogate");
                    cursor_+=2;unsigned low=hex4();if(low<0xdc00||low>0xdfff)fail("Invalid low Unicode surrogate");
                    code=0x10000+((code-0xd800)<<10)+(low-0xdc00);
                } else if(code>=0xdc00&&code<=0xdfff)fail("Unexpected low Unicode surrogate");
                utf8(result,code);break;
            }
            default:fail("Invalid string escape");
            }
            if(result.size()>1024*1024)fail("JSON string too long");
        }
        fail("Unterminated string");
    }
    Json value(unsigned depth) {
        if(depth>64||++nodes_>200000)fail("JSON nesting/node limit exceeded");
        space();if(cursor_==text_.size())fail("Missing JSON value");Json result;
        char c=text_[cursor_];
        if(c=='{') {
            ++cursor_;result.type=Json::Object;if(take('}'))return result;
            do {auto key=string();if(!take(':'))fail("Expected colon");result.object[key]=value(depth+1);}while(take(','));
            if(!take('}'))fail("Expected closing object brace");
            return result;
        }
        if(c=='[') {
            ++cursor_;result.type=Json::Array;if(take(']'))return result;
            do {result.array.push_back(value(depth+1));}while(take(','));
            if(!take(']'))fail("Expected closing array bracket");
            return result;
        }
        if(c=='"'){result.type=Json::String;result.string=string();return result;}
        for(const auto& literal:std::array<std::pair<const char*,int>,3>{{{"true",1},{"false",0},{"null",-1}}}) {
            const std::size_t length=std::strlen(literal.first);
            if(text_.compare(cursor_,length,literal.first)==0) {
                cursor_+=length;result.type=literal.second<0?Json::Null:Json::Boolean;result.boolean=literal.second==1;return result;
            }
        }
        std::size_t begin=cursor_;if(c=='-')++cursor_;
        if(cursor_==text_.size())fail("Incomplete number");
        if(text_[cursor_]=='0')++cursor_;
        else {if(text_[cursor_]<'1'||text_[cursor_]>'9')fail("Invalid JSON token");while(cursor_<text_.size()&&text_[cursor_]>='0'&&text_[cursor_]<='9')++cursor_;}
        if(cursor_<text_.size()&&text_[cursor_]=='.') {
            ++cursor_;auto digits=cursor_;while(cursor_<text_.size()&&text_[cursor_]>='0'&&text_[cursor_]<='9')++cursor_;
            if(cursor_==digits)fail("Missing number fraction");
        }
        if(cursor_<text_.size()&&(text_[cursor_]=='e'||text_[cursor_]=='E')) {
            ++cursor_;if(cursor_<text_.size()&&(text_[cursor_]=='+'||text_[cursor_]=='-'))++cursor_;
            auto digits=cursor_;while(cursor_<text_.size()&&text_[cursor_]>='0'&&text_[cursor_]<='9')++cursor_;
            if(cursor_==digits)fail("Missing number exponent");
        }
        std::string token=text_.substr(begin,cursor_-begin);char* end=nullptr;
        result.type=Json::Number;result.number=std::strtod(token.c_str(),&end);
        if(!end||*end||!std::isfinite(result.number))fail("Invalid/non-finite number");
        return result;
    }
public:
    explicit JsonParser(const std::string& text):text_(text){}
    Json parse(){if(text_.size()>16*1024*1024)fail("Sound JSON exceeds 16 MiB");auto result=value(0);space();if(cursor_!=text_.size())fail("Trailing JSON content");return result;}
};

bool safeRelative(const std::string& path) {
    if(path.empty()||path.front()=='/'||path.find('\\')!=std::string::npos||path.find(':')!=std::string::npos)return false;
    for(unsigned char c:path)if(c<32)return false;
    std::size_t start=0;
    while(start<path.size()) {
        auto end=path.find('/',start);auto part=path.substr(start,end==std::string::npos?end:end-start);
        if(part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' ')return false;
        std::string device=part.substr(0,part.find('.'));
        for(char& c:device)if(c>='a'&&c<='z')c=char(c-'a'+'A');
        if(device=="CON"||device=="PRN"||device=="AUX"||device=="NUL"||
           (device.size()==4&&(device.compare(0,3,"COM")==0||device.compare(0,3,"LPT")==0)&&device[3]>='1'&&device[3]<='9'))return false;
        if(end==std::string::npos)break;
        start=end+1;
    }
    return path.back()!='/';
}
std::string domainName(std::string domain) {
    if(domain.empty())domain="minecraft";
    for(char& c:domain) {
        if(c>='A'&&c<='Z')c=char(c-'A'+'a');
        if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))throw AudioError("Invalid sound namespace");
    }
    if(domain=="."||domain=="..")throw AudioError("Invalid sound namespace");
    return domain;
}
std::pair<std::string,std::string> splitResource(const std::string& name,const std::string& domain="minecraft") {
    auto colon=name.find(':');
    if(colon==std::string::npos)return {domainName(domain),name};
    return {domainName(name.substr(0,colon)),name.substr(colon+1)};
}
std::string eventName(const std::string& name) {auto p=splitResource(name);return p.first+":"+p.second;}
std::string resourcePath(const std::string& directory,const std::string& resource) {
    auto p=splitResource(resource);if(!safeRelative(p.second))throw AudioError("Unsafe sound resource path");
    return directory+"/resources/assets/"+p.first+"/"+p.second;
}
std::vector<unsigned char> readBytes(const std::string& path,std::size_t limit) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input)throw AudioError("Cannot open sound resource: "+path);
    auto size=input.tellg();if(size<0||std::uint64_t(size)>limit)throw AudioError("Sound resource exceeds size limit: "+path);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));input.seekg(0);
    if(!bytes.empty()&&!input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))throw AudioError("Truncated sound resource: "+path);
    return bytes;
}
SoundCategory category(const std::string& name) {
    const char* names[]={"master","music","record","weather","block","hostile","neutral","player","ambient"};
    for(unsigned i=0;i<9;++i)if(name==names[i])return static_cast<SoundCategory>(i);
    throw AudioError("Invalid sound category: "+name);
}
std::uint32_t javaNext(std::uint64_t& seed,int bits) {
    seed=(seed*0x5deece66dull+11)&((1ull<<48)-1);return static_cast<std::uint32_t>(seed>>(48-bits));
}
std::uint32_t javaNextInt(std::uint64_t& seed,std::uint32_t bound) {
    if((bound&(bound-1))==0)return static_cast<std::uint32_t>((std::uint64_t(bound)*javaNext(seed,31))>>31);
    std::uint32_t bits,value;
    do{bits=javaNext(seed,31);value=bits%bound;}while(bits-value+(bound-1)>0x7fffffffu);
    return value;
}
} // namespace

struct SoundRegistry::Impl {
    struct Entry {std::string name;std::uint32_t weight=1;float volume=1,pitch=1;bool event=false,stream=false;};
    struct Event {SoundCategory category=SoundCategory::Master;std::vector<Entry> entries;};
    std::map<std::string,Event> events;
    std::vector<std::string> warnings;
    std::uint32_t weightOf(const std::string& name,std::vector<std::string>& stack) const {
        auto found=events.find(name);if(found==events.end())return 0;
        if(stack.size()>=64||std::find(stack.begin(),stack.end(),name)!=stack.end())throw AudioError("Cyclic/deep sound event alias: "+name);
        stack.push_back(name);std::uint64_t result=0;
        for(const auto& entry:found->second.entries) {
            result+=entry.event?weightOf(entry.name,stack):entry.weight;
            if(result>0x7fffffffu)throw AudioError("Sound event weight overflows Java int: "+name);
        }
        stack.pop_back();return static_cast<std::uint32_t>(result);
    }
    bool resolveOf(const std::string& name,std::uint64_t& seed,ResolvedSound& result,unsigned depth) const {
        if(depth>=64)throw AudioError("Sound alias depth exceeded");
        auto found=events.find(name);if(found==events.end())return false;
        std::vector<std::string> stack;auto total=weightOf(name,stack);if(!total)return false;
        auto ticket=javaNextInt(seed,total);
        for(const auto& entry:found->second.entries) {
            auto weight=entry.event?weightOf(entry.name,stack):entry.weight;
            if(ticket<weight) {
                if(entry.event)return resolveOf(entry.name,seed,result,depth+1);
                result.resource=entry.name;result.volume=entry.volume;result.pitch=entry.pitch;result.stream=entry.stream;return true;
            }
            ticket-=weight;
        }
        return false;
    }
};

SoundRegistry::SoundRegistry():impl_(new Impl){}
SoundRegistry::~SoundRegistry()=default;
SoundRegistry::SoundRegistry(SoundRegistry&&) noexcept=default;
SoundRegistry& SoundRegistry::operator=(SoundRegistry&&) noexcept=default;
std::uint64_t SoundRegistry::javaSeed(std::uint64_t seed) noexcept{return (seed^0x5deece66dull)&((1ull<<48)-1);}

bool SoundRegistry::mergeJson(const std::string& json,const std::string& nameSpace,std::string& error) {
    try {
        const auto domain=domainName(nameSpace);auto root=JsonParser(json).parse();
        if(root.type!=Json::Object)throw AudioError("sounds.json root must be an object");
        Impl next=*impl_;
        for(const auto& pair:root.object) {
            if(pair.first.empty()||pair.first.size()>1024)throw AudioError("Invalid sound event name");
            const auto& definition=pair.second;if(definition.type!=Json::Object)throw AudioError("Sound event must be an object");
            auto replacement=definition.find("replace");bool replace=replacement&&replacement->flag();
            auto categoryJson=definition.find("category");auto soundCategory=category(categoryJson?categoryJson->text():"master");
            std::string key=domain+":"+pair.first;auto found=next.events.find(key);
            if(found==next.events.end()||replace)next.events[key]=Impl::Event{soundCategory,{}};
            auto& target=next.events[key];auto sounds=definition.find("sounds");if(!sounds)continue;
            if(sounds->type!=Json::Array)throw AudioError("sounds must be an array");
            for(const auto& sound:sounds->array) {
                Impl::Entry entry;std::string rawName;
                if(sound.type==Json::String)rawName=sound.string;
                else if(sound.type==Json::Object) {
                    rawName=sound.required("name").text();
                    if(auto type=sound.find("type")) {
                        auto value=type->text();if(value!="file"&&value!="event")throw AudioError("Unknown sound entry type: "+value);
                        entry.event=value=="event";
                    }
                    if(auto volume=sound.find("volume"))entry.volume=static_cast<float>(volume->numeric());
                    if(auto pitch=sound.find("pitch"))entry.pitch=static_cast<float>(pitch->numeric());
                    if(auto stream=sound.find("stream"))entry.stream=stream->flag();
                    if(auto weight=sound.find("weight")) {
                        double value=weight->numeric();if(value<1||value>0x7fffffff||std::floor(value)!=value)throw AudioError("Sound weight must be a positive Java int");
                        entry.weight=static_cast<std::uint32_t>(value);
                    }
                } else throw AudioError("Sound entry must be string or object");
                if(!(entry.volume>0)||!(entry.pitch>0)||!std::isfinite(entry.volume)||!std::isfinite(entry.pitch))throw AudioError("Sound pitch/volume must be finite and positive");
                auto resource=splitResource(rawName,domain);
                if(entry.event) {
                    // 1.8.9 SoundHandler uses the raw name as the alias path, and
                    // ignores weight/pitch/volume/stream on SOUND_EVENT wrappers.
                    entry.name=resource.first+":"+rawName;
                } else {
                    if(!safeRelative(resource.second))throw AudioError("Unsafe sound file name: "+rawName);
                    entry.name=resource.first+":sounds/"+resource.second+".ogg";
                }
                target.entries.push_back(std::move(entry));
                if(target.entries.size()>65536)throw AudioError("Too many entries in sound event");
            }
            if(next.events.size()>65536)throw AudioError("Too many sound events");
        }
        *impl_=std::move(next);error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}

bool SoundRegistry::load(const std::string& directory,std::string& error) {
    try {
        SoundRegistry next;std::ifstream layers(directory+"/runtime/audio/sounds-layers.txt");
        std::vector<std::pair<std::string,std::string>> files;
        if(layers) {
            std::string line;
            while(std::getline(layers,line)) {
                if(!line.empty()&&line.back()=='\r')line.pop_back();
                if(line.empty())continue;
                auto tab=line.find('\t');if(tab==std::string::npos||line.size()>4096)throw AudioError("Invalid sound layer index");
                auto path=line.substr(tab+1);if(!safeRelative(path))throw AudioError("Unsafe sound layer path");
                files.emplace_back(domainName(line.substr(0,tab)),directory+"/"+path);
                if(files.size()>1024)throw AudioError("Too many sound definition layers");
            }
        } else files.emplace_back("minecraft",directory+"/resources/assets/minecraft/sounds.json");
        for(const auto& file:files) {
            auto bytes=readBytes(file.second,16*1024*1024);
            if(!next.mergeJson(std::string(bytes.begin(),bytes.end()),file.first,error))return false;
        }
        for(auto& event:next.impl_->events) {
            auto& entries=event.second.entries;
            entries.erase(std::remove_if(entries.begin(),entries.end(),[&](const Impl::Entry& entry){
                if(entry.event)return false;
                if(std::ifstream(resourcePath(directory,entry.name),std::ios::binary))return false;
                next.impl_->warnings.push_back("Missing sound file "+entry.name+" for "+event.first);return true;
            }),entries.end());
        }
        for(const auto& event:next.impl_->events){std::vector<std::string> stack;next.impl_->weightOf(event.first,stack);}
        if(next.impl_->events.empty())throw AudioError("No sound events loaded");
        *this=std::move(next);error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}

bool SoundRegistry::resolve(const std::string& event,std::uint64_t& seed,ResolvedSound& result,std::string& error) const {
    try {
        result={};auto name=eventName(event);auto found=impl_->events.find(name);
        if(found==impl_->events.end()){error="Unknown sound event: "+name;return false;}
        if(!impl_->resolveOf(name,seed,result,0)){error="Empty sound event: "+name;return false;}
        result.category=found->second.category;error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}
std::uint32_t SoundRegistry::weight(const std::string& event) const {
    try{std::vector<std::string> stack;return impl_->weightOf(eventName(event),stack);}catch(const std::exception&){return 0;}
}
std::vector<std::string> SoundRegistry::eventNames() const {
    std::vector<std::string> result;for(const auto& event:impl_->events)result.push_back(event.first);return result;
}
std::size_t SoundRegistry::eventCount() const noexcept{return impl_->events.size();}
const std::vector<std::string>& SoundRegistry::warnings() const noexcept{return impl_->warnings;}

namespace {
void validateOgg(const std::vector<unsigned char>& bytes) {
    // Validate complete page boundaries and CRCs before the decoder's seek probes.
    // This also makes corrupt/truncated resources an explicit error, not short audio.
    static const std::array<std::uint32_t,256> crcTable=[] {
        std::array<std::uint32_t,256> table{};
        for(unsigned i=0;i<256;++i) {
            std::uint32_t value=i<<24;
            for(int bit=0;bit<8;++bit)value=(value<<1)^((value&0x80000000u)?0x04c11db7u:0);
            table[i]=value;
        }
        return table;
    }();
    auto read32=[&](std::size_t p){return std::uint32_t(bytes[p])|(std::uint32_t(bytes[p+1])<<8)|
        (std::uint32_t(bytes[p+2])<<16)|(std::uint32_t(bytes[p+3])<<24);};
    std::size_t offset=0;std::uint32_t serial=0,sequence=0;bool ended=false,first=true;
    while(offset<bytes.size()) {
        if(bytes.size()-offset<27||std::memcmp(bytes.data()+offset,"OggS",4)||bytes[offset+4]!=0)throw AudioError("Invalid/truncated Ogg page header");
        if(ended)throw AudioError("Chained Ogg logical streams are not supported");
        unsigned flags=bytes[offset+5],segments=bytes[offset+26];
        if(flags&~7u)throw AudioError("Invalid Ogg page flags");
        if(first) {
            if(!(flags&2))throw AudioError("Ogg stream has no beginning-of-stream page");
            serial=read32(offset+14);sequence=read32(offset+18);first=false;
        } else if(read32(offset+14)!=serial||read32(offset+18)!=++sequence)throw AudioError("Multiplexed/out-of-order Ogg stream is not supported");
        if(bytes.size()-offset<27+segments)throw AudioError("Truncated Ogg segment table");
        std::size_t pageSize=27+segments;for(unsigned i=0;i<segments;++i)pageSize+=bytes[offset+27+i];
        if(bytes.size()-offset<pageSize)throw AudioError("Truncated Ogg page body");
        std::uint32_t crc=0;
        for(std::size_t i=0;i<pageSize;++i) {
            unsigned value=i>=22&&i<26?0:bytes[offset+i];crc=(crc<<8)^crcTable[(crc>>24)^value];
        }
        if(crc!=read32(offset+22))throw AudioError("Ogg page CRC mismatch");
        ended=(flags&4)!=0;offset+=pageSize;
    }
    if(first||!ended)throw AudioError("Ogg stream has no end-of-stream page");
}

class VorbisReader {
public:
    std::vector<unsigned char> encoded;
    stb_vorbis* decoder=nullptr;
    unsigned channels=0,sampleRate=0;
    std::uint32_t expectedFrames=0;
    bool eof=false;
    explicit VorbisReader(const std::string& path):encoded(readBytes(path,64u*1024u*1024u)) {
        if(encoded.size()<4||std::memcmp(encoded.data(),"OggS",4))throw AudioError("Not an Ogg stream: "+path);
        validateOgg(encoded);
        int code=0;decoder=stb_vorbis_open_memory(encoded.data(),static_cast<int>(encoded.size()),&code,nullptr);
        if(!decoder)throw AudioError("Vorbis open failed ("+std::to_string(code)+"): "+path);
        const auto info=stb_vorbis_get_info(decoder);channels=info.channels;sampleRate=info.sample_rate;
        if((channels!=1&&channels!=2)||sampleRate<1000||sampleRate>384000) {
            stb_vorbis_close(decoder);decoder=nullptr;throw AudioError("Unsupported Vorbis channels/sample rate");
        }
        expectedFrames=stb_vorbis_stream_length_in_samples(decoder);
    }
    ~VorbisReader(){if(decoder)stb_vorbis_close(decoder);}
    VorbisReader(const VorbisReader&)=delete;
    VorbisReader& operator=(const VorbisReader&)=delete;
    std::size_t read(std::vector<std::int16_t>& samples,std::size_t frames=4096) {
        samples.resize(frames*channels);
        int result=stb_vorbis_get_samples_short_interleaved(decoder,static_cast<int>(channels),samples.data(),static_cast<int>(samples.size()));
        if(result<0)throw AudioError("Vorbis decoder returned invalid frame count");
        if(result==0){int code=stb_vorbis_get_error(decoder);if(code)throw AudioError("Vorbis decode error: "+std::to_string(code));eof=true;}
        samples.resize(static_cast<std::size_t>(result)*channels);return static_cast<std::size_t>(result);
    }
};
} // namespace

bool Audio::decodeVorbisFile(const std::string& path,DecodedSound& result,std::string& error,std::size_t maxFrames) {
    try {
        if(!maxFrames||maxFrames>384000u*60u*60u)throw AudioError("Invalid Vorbis frame limit");
        VorbisReader input(path);
        maxFrames=std::min(maxFrames,std::size_t(256u*1024u*1024u)/(input.channels*sizeof(std::int16_t)));
        if(input.expectedFrames>maxFrames)throw AudioError("Decoded Vorbis exceeds frame limit");
        DecodedSound next;next.channels=input.channels;next.sampleRate=input.sampleRate;
        next.samples.reserve(std::size_t(input.expectedFrames)*input.channels);
        std::vector<std::int16_t> block;
        while(!input.eof) {
            auto frames=input.read(block);if(next.frames()+frames>maxFrames)throw AudioError("Decoded Vorbis exceeds frame limit");
            next.samples.insert(next.samples.end(),block.begin(),block.end());
        }
        if(next.samples.empty())throw AudioError("Vorbis resource contains no audio samples");
        if(input.expectedFrames&&next.frames()!=input.expectedFrames)throw AudioError("Vorbis stream ended before its declared sample count");
        result=std::move(next);error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}

SoundProperties Audio::properties(const ResolvedSound& sound,float volume,float pitch,float categoryVolume,float masterVolume) noexcept {
    SoundProperties result;
    if(!std::isfinite(volume)||!std::isfinite(pitch)||!std::isfinite(sound.volume)||!std::isfinite(sound.pitch)||
       !std::isfinite(categoryVolume)||!std::isfinite(masterVolume)){result.gain=0;return result;}
    result.gain=static_cast<float>(std::clamp(double(volume)*sound.volume,0.0,1.0))*std::clamp(categoryVolume,0.f,1.f)*std::clamp(masterVolume,0.f,1.f);
    result.pitch=static_cast<float>(std::clamp(double(pitch)*sound.pitch,.5,2.));
    result.maxDistance=16*std::max(1.f,volume);return result;
}
float Audio::attenuation(double distance,float maxDistance,bool relative) noexcept {
    if(relative)return 1;
    if(!std::isfinite(distance)||!std::isfinite(maxDistance)||maxDistance<=0)return 0;
    return static_cast<float>(std::clamp(1-distance/maxDistance,0.,1.));
}
std::array<float,6> Audio::listenerOrientation(float yawDegrees,float pitchDegrees) noexcept {
    constexpr double rad=3.14159265358979323846/180.;double y=yawDegrees*rad,p=pitchDegrees*rad;
    double sy=std::sin(y),cy=std::cos(y),sp=std::sin(p),cp=std::cos(p);
    return {{float(-sy*cp),float(-sp),float(cy*cp),float(-sy*sp),float(cp),float(cy*sp)}};
}

namespace {
using ALuint=unsigned int;
using ALenum=int;
using ALsizei=int;
using ALint=int;
using ALfloat=float;
struct ALCdevice;
struct ALCcontext;
constexpr int AL_NO_ERROR=0,AL_NONE=0,AL_FALSE=0,AL_TRUE=1;
constexpr int AL_POSITION=0x1004,AL_VELOCITY=0x1006,AL_ORIENTATION=0x100f;
constexpr int AL_GAIN=0x100a,AL_PITCH=0x1003,AL_BUFFER=0x1009,AL_LOOPING=0x1007,AL_SOURCE_RELATIVE=0x202;
constexpr int AL_ROLLOFF_FACTOR=0x1021,AL_SOURCE_STATE=0x1010,AL_PLAYING=0x1012;
constexpr int AL_BUFFERS_QUEUED=0x1015,AL_BUFFERS_PROCESSED=0x1016;
constexpr int AL_FORMAT_MONO16=0x1101,AL_FORMAT_STEREO16=0x1103;

struct OpenAL {
#ifdef _WIN32
    HMODULE library=nullptr;
#endif
    ALCdevice* device=nullptr;
    ALCcontext* context=nullptr;
    ALCdevice* (*alcOpenDevice)(const char*)=nullptr;
    char (*alcCloseDevice)(ALCdevice*)=nullptr;
    ALCcontext* (*alcCreateContext)(ALCdevice*,const int*)=nullptr;
    void (*alcDestroyContext)(ALCcontext*)=nullptr;
    char (*alcMakeContextCurrent)(ALCcontext*)=nullptr;
    void (*alGenSources)(ALsizei,ALuint*)=nullptr;
    void (*alDeleteSources)(ALsizei,const ALuint*)=nullptr;
    void (*alSourcePlay)(ALuint)=nullptr;
    void (*alSourceStop)(ALuint)=nullptr;
    void (*alSourcef)(ALuint,ALenum,ALfloat)=nullptr;
    void (*alSource3f)(ALuint,ALenum,ALfloat,ALfloat,ALfloat)=nullptr;
    void (*alSourcei)(ALuint,ALenum,ALint)=nullptr;
    void (*alGetSourcei)(ALuint,ALenum,ALint*)=nullptr;
    void (*alGenBuffers)(ALsizei,ALuint*)=nullptr;
    void (*alDeleteBuffers)(ALsizei,const ALuint*)=nullptr;
    void (*alBufferData)(ALuint,ALenum,const void*,ALsizei,ALsizei)=nullptr;
    void (*alSourceQueueBuffers)(ALuint,ALsizei,const ALuint*)=nullptr;
    void (*alSourceUnqueueBuffers)(ALuint,ALsizei,ALuint*)=nullptr;
    void (*alListener3f)(ALenum,ALfloat,ALfloat,ALfloat)=nullptr;
    void (*alListenerfv)(ALenum,const ALfloat*)=nullptr;
    void (*alListenerf)(ALenum,ALfloat)=nullptr;
    void (*alDistanceModel)(ALenum)=nullptr;
    void (*alDopplerFactor)(ALfloat)=nullptr;
    ALenum (*alGetError)()=nullptr;

    ~OpenAL(){close();}
    OpenAL()=default;
    OpenAL(const OpenAL&)=delete;
    OpenAL& operator=(const OpenAL&)=delete;
    void close() noexcept {
        if(context&&alcMakeContextCurrent)alcMakeContextCurrent(nullptr);
        if(context&&alcDestroyContext)alcDestroyContext(context);
        context=nullptr;
        if(device&&alcCloseDevice)alcCloseDevice(device);
        device=nullptr;
#ifdef _WIN32
        if(library)FreeLibrary(library);
        library=nullptr;
#endif
    }
    void current() {if(context&&!alcMakeContextCurrent(context))throw AudioError("Cannot make OpenAL context current");}
    void check(const char* operation) {
        const auto error=alGetError();if(error!=AL_NO_ERROR)throw AudioError(std::string(operation)+" failed, OpenAL error "+std::to_string(error));
    }
#ifdef _WIN32
    template<class Function> void symbol(Function& function,const char* name) {
        FARPROC proc=GetProcAddress(library,name);if(!proc)throw AudioError(std::string("OpenAL DLL is missing ")+name);
        static_assert(sizeof(function)==sizeof(proc),"Function-pointer size mismatch");
        std::memcpy(&function,&proc,sizeof(function));
    }
#endif
    void open(const std::string& directory) {
#ifdef _WIN32
        std::string utf8=directory+"/runtime/OpenAL64.dll";
        int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.c_str(),-1,nullptr,0);
        if(size<=0)throw AudioError("Invalid UTF-8 audio library path");
        std::wstring path(static_cast<std::size_t>(size),L'\0');
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.c_str(),-1,&path[0],size);
        std::vector<wchar_t> absolute(32768);
        DWORD length=GetFullPathNameW(path.c_str(),static_cast<DWORD>(absolute.size()),absolute.data(),nullptr);
        if(!length||length>=absolute.size())throw AudioError("Cannot resolve audio library path");
        library=LoadLibraryW(absolute.data());
        if(!library)throw AudioError("Cannot load bundled OpenAL64.dll (Windows error "+std::to_string(GetLastError())+")");
#define MC_AL_LOAD(name) symbol(name,#name)
        MC_AL_LOAD(alcOpenDevice);MC_AL_LOAD(alcCloseDevice);MC_AL_LOAD(alcCreateContext);
        MC_AL_LOAD(alcDestroyContext);MC_AL_LOAD(alcMakeContextCurrent);
        MC_AL_LOAD(alGenSources);MC_AL_LOAD(alDeleteSources);MC_AL_LOAD(alSourcePlay);MC_AL_LOAD(alSourceStop);
        MC_AL_LOAD(alSourcef);MC_AL_LOAD(alSource3f);MC_AL_LOAD(alSourcei);MC_AL_LOAD(alGetSourcei);
        MC_AL_LOAD(alGenBuffers);MC_AL_LOAD(alDeleteBuffers);MC_AL_LOAD(alBufferData);
        MC_AL_LOAD(alSourceQueueBuffers);MC_AL_LOAD(alSourceUnqueueBuffers);
        MC_AL_LOAD(alListener3f);MC_AL_LOAD(alListenerfv);MC_AL_LOAD(alListenerf);
        MC_AL_LOAD(alDistanceModel);MC_AL_LOAD(alDopplerFactor);MC_AL_LOAD(alGetError);
#undef MC_AL_LOAD
        device=alcOpenDevice(nullptr);if(!device)throw AudioError("No OpenAL output device is available");
        context=alcCreateContext(device,nullptr);if(!context)throw AudioError("Cannot create OpenAL context");
        current();alDistanceModel(AL_NONE);alDopplerFactor(0);
        alListenerf(AL_GAIN,1);alListener3f(AL_VELOCITY,0,0,0);check("OpenAL initialization");
#else
        (void)directory;throw AudioError("The audible backend currently requires Windows; nullOutput is portable");
#endif
    }
};
} // namespace

struct Audio::Impl {
    struct Buffer {
        ALuint id=0;
        unsigned channels=0,sampleRate=0;
        std::size_t frames=0,bytes=0;
        std::uint64_t lastUsed=0;
    };
    struct Voice {
        ALuint source=0;
        std::shared_ptr<Buffer> buffer;
        std::unique_ptr<VorbisReader> stream;
        std::array<ALuint,4> streamBuffers{{0,0,0,0}};
        ResolvedSound sound;
        double x=0,y=0,z=0;
        float volume=1,pitch=1;
        bool relative=false;
        double nullSeconds=0;
    };
    SoundRegistry registry;
    std::string directory,error;
    bool ready=false,null=false;
    OpenAL al;
    std::vector<Voice> voices;
    std::unordered_map<std::string,std::shared_ptr<Buffer>> cache;
    std::unordered_map<std::string,std::uint64_t> randomSeeds;
    std::array<float,9> categories{{1,1,1,1,1,1,1,1,1}};
    float master=1;
    double listenerX=0,listenerY=0,listenerZ=0;
    float listenerYaw=0,listenerPitch=0;
    std::uint64_t sequence=0;
    std::size_t cacheBytes=0;
    std::chrono::steady_clock::time_point lastTick=std::chrono::steady_clock::now();
    ~Impl() {
        try {
            if(al.context)al.current();
            stop();
            if(al.context)for(auto& pair:cache)if(pair.second->id)al.alDeleteBuffers(1,&pair.second->id);
        } catch(...){}
    }
    SoundProperties propertiesOf(const Voice& voice) const {
        auto c=static_cast<unsigned>(voice.sound.category);
        return Audio::properties(voice.sound,voice.volume,voice.pitch,c==0?1:categories[c],master);
    }
    void gain(Voice& voice) {
        if(null)return;
        auto props=propertiesOf(voice);
        double dx=voice.x-listenerX,dy=voice.y-listenerY,dz=voice.z-listenerZ;
        float gain=props.gain*Audio::attenuation(std::sqrt(dx*dx+dy*dy+dz*dz),props.maxDistance,voice.relative);
        al.alSourcef(voice.source,AL_GAIN,gain);al.alSourcef(voice.source,AL_PITCH,props.pitch);
    }
    void destroy(Voice& voice) noexcept {
        if(!null&&voice.source) {
            al.alSourceStop(voice.source);
            // Deleting the source releases queued-buffer references before deletion.
            al.alDeleteSources(1,&voice.source);voice.source=0;
            if(voice.stream)for(ALuint id:voice.streamBuffers)if(id)al.alDeleteBuffers(1,&id);
        }
        voice.stream.reset();voice.buffer.reset();
    }
    void stop() noexcept {for(auto& voice:voices)destroy(voice);voices.clear();}
    void evict(std::size_t incoming) {
        constexpr std::size_t limit=64u*1024u*1024u;
        while(cacheBytes+incoming>limit&&!cache.empty()) {
            auto candidate=cache.end();
            for(auto it=cache.begin();it!=cache.end();++it) {
                if(it->second.use_count()!=1)continue;
                if(candidate==cache.end()||it->second->lastUsed<candidate->second->lastUsed)candidate=it;
            }
            if(candidate==cache.end())break;
            if(!null&&candidate->second->id)al.alDeleteBuffers(1,&candidate->second->id);
            cacheBytes-=candidate->second->bytes;cache.erase(candidate);
        }
        if(cacheBytes+incoming>limit)throw AudioError("Decoded sound cache is full (64 MiB)");
    }
    std::shared_ptr<Buffer> staticBuffer(const std::string& resource) {
        auto found=cache.find(resource);if(found!=cache.end()){found->second->lastUsed=++sequence;return found->second;}
        DecodedSound decoded;std::string decodeError;
        // 16 million mono/stereo frames bounds each non-streaming allocation to 64 MiB.
        if(!Audio::decodeVorbisFile(resourcePath(directory,resource),decoded,decodeError,16u*1024u*1024u))throw AudioError(decodeError);
        auto buffer=std::make_shared<Buffer>();buffer->channels=decoded.channels;buffer->sampleRate=decoded.sampleRate;
        buffer->frames=decoded.frames();buffer->bytes=decoded.samples.size()*sizeof(std::int16_t);buffer->lastUsed=++sequence;
        evict(buffer->bytes);
        if(!null) {
            al.alGenBuffers(1,&buffer->id);
            try {
                al.check("Create sound buffer");
                al.alBufferData(buffer->id,buffer->channels==1?AL_FORMAT_MONO16:AL_FORMAT_STEREO16,
                                decoded.samples.data(),static_cast<ALsizei>(buffer->bytes),static_cast<ALsizei>(buffer->sampleRate));
                al.check("Upload sound buffer");
            } catch(...) {if(buffer->id)al.alDeleteBuffers(1,&buffer->id);throw;}
        }
        cacheBytes+=buffer->bytes;cache.emplace(resource,buffer);return buffer;
    }
    bool refill(Voice& voice,ALuint buffer) {
        if(voice.stream->eof)return false;
        std::vector<std::int16_t> samples;auto frames=voice.stream->read(samples);
        if(!frames)return false;
        auto props=propertiesOf(voice);
        if(null)voice.nullSeconds+=double(frames)/voice.stream->sampleRate/props.pitch;
        else {
            al.alBufferData(buffer,voice.stream->channels==1?AL_FORMAT_MONO16:AL_FORMAT_STEREO16,samples.data(),
                            static_cast<ALsizei>(samples.size()*sizeof(std::int16_t)),static_cast<ALsizei>(voice.stream->sampleRate));
            al.alSourceQueueBuffers(voice.source,1,&buffer);al.check("Queue streaming audio");
        }
        return true;
    }
    void listener() {
        if(null||!ready)return;
        auto orientation=Audio::listenerOrientation(listenerYaw,listenerPitch);
        al.alListener3f(AL_POSITION,float(listenerX),float(listenerY),float(listenerZ));
        al.alListenerfv(AL_ORIENTATION,orientation.data());
        for(auto& voice:voices)gain(voice);
    }
};

Audio::Audio():impl_(new Impl){}
Audio::~Audio()=default;

bool Audio::initialize(const std::string& directory,std::string& error,bool nullOutput) {
    try {
        std::unique_ptr<Impl> next(new Impl);next->directory=directory;next->null=nullOutput;
        if(!next->registry.load(directory,error)){impl_->error=error;return false;}
        if(!nullOutput)next->al.open(directory);
        next->ready=true;
        impl_.swap(next);next.reset();
        if(!nullOutput)impl_->al.current();
        impl_->listener();error.clear();return true;
    } catch(const std::exception& e){error=e.what();impl_->error=error;return false;}
}

bool Audio::play(const std::string& name,double x,double y,double z,float volume,float pitch,bool relative) {
    auto& state=*impl_;
    if(!state.ready)return false;
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||!std::isfinite(volume)||!std::isfinite(pitch)) {
        state.error="Non-finite sound position/volume/pitch";return false;
    }
    if(volume<=0||state.master<=0)return false;
    Impl::Voice voice;
    try {
        if(!state.null)state.al.current();
        auto seed=state.randomSeeds.find(name);
        if(seed==state.randomSeeds.end()) {
            std::uint64_t hash=1469598103934665603ull;for(unsigned char c:name){hash^=c;hash*=1099511628211ull;}
            hash^=std::uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
            seed=state.randomSeeds.emplace(name,SoundRegistry::javaSeed(hash)).first;
        }
        if(!state.registry.resolve(name,seed->second,voice.sound,state.error))return false;
        voice.x=x;voice.y=y;voice.z=z;voice.volume=volume;voice.pitch=pitch;voice.relative=relative;
        auto props=state.propertiesOf(voice);if(props.gain<=0)return false;
        const bool stream=voice.sound.stream;
        const auto count=std::count_if(state.voices.begin(),state.voices.end(),[&](const Impl::Voice& v){return bool(v.stream)==stream;});
        if(count>=(stream?4:32)) {
            auto oldest=std::find_if(state.voices.begin(),state.voices.end(),[&](const Impl::Voice& v){return bool(v.stream)==stream;});
            state.destroy(*oldest);state.voices.erase(oldest);
        }
        if(stream)voice.stream.reset(new VorbisReader(resourcePath(state.directory,voice.sound.resource)));
        else voice.buffer=state.staticBuffer(voice.sound.resource);
        if(!state.null) {
            state.al.alGenSources(1,&voice.source);state.al.check("Create sound source");
            state.al.alSourcei(voice.source,AL_LOOPING,AL_FALSE);
            state.al.alSourcei(voice.source,AL_SOURCE_RELATIVE,relative?AL_TRUE:AL_FALSE);
            state.al.alSourcef(voice.source,AL_ROLLOFF_FACTOR,0);
            state.al.alSource3f(voice.source,AL_POSITION,relative?0.f:float(x),relative?0.f:float(y),relative?0.f:float(z));
            state.al.alSource3f(voice.source,AL_VELOCITY,0,0,0);state.gain(voice);
        }
        if(stream) {
            if(!state.null){state.al.alGenBuffers(4,voice.streamBuffers.data());state.al.check("Create stream buffers");}
            bool any=false;for(ALuint buffer:voice.streamBuffers)any=state.refill(voice,buffer)||any;
            if(!any)throw AudioError("Streaming sound contains no samples");
        } else if(state.null)voice.nullSeconds=double(voice.buffer->frames)/voice.buffer->sampleRate/props.pitch;
        else state.al.alSourcei(voice.source,AL_BUFFER,static_cast<ALint>(voice.buffer->id));
        if(!state.null){state.al.alSourcePlay(voice.source);state.al.check("Start sound");}
        state.voices.push_back(std::move(voice));state.error.clear();return true;
    } catch(const std::exception& e){state.destroy(voice);state.error=e.what();return false;}
}

void Audio::updateListener(double x,double y,double z,float yaw,float pitch) {
    auto& state=*impl_;
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||!std::isfinite(yaw)||!std::isfinite(pitch))return;
    state.listenerX=x;state.listenerY=y;state.listenerZ=z;state.listenerYaw=yaw;state.listenerPitch=pitch;
    if(!state.ready)return;
    try {if(!state.null)state.al.current();state.listener();}catch(const std::exception& e){state.error=e.what();}
}

void Audio::tick() {
    auto& state=*impl_;if(!state.ready)return;
    auto now=std::chrono::steady_clock::now();double elapsed=std::chrono::duration<double>(now-state.lastTick).count();state.lastTick=now;
    try {
        if(!state.null)state.al.current();
        for(std::size_t i=0;i<state.voices.size();) {
            auto& voice=state.voices[i];bool finished=false;
            if(state.null) {
                voice.nullSeconds-=elapsed;
                if(voice.stream)while(voice.nullSeconds<.2&&!voice.stream->eof)state.refill(voice,0);
                finished=voice.nullSeconds<=0&&(!voice.stream||voice.stream->eof);
            } else {
                if(voice.stream) {
                    ALint processed=0;state.al.alGetSourcei(voice.source,AL_BUFFERS_PROCESSED,&processed);
                    while(processed-->0){ALuint buffer=0;state.al.alSourceUnqueueBuffers(voice.source,1,&buffer);state.refill(voice,buffer);}
                    ALint queued=0,status=0;state.al.alGetSourcei(voice.source,AL_BUFFERS_QUEUED,&queued);
                    state.al.alGetSourcei(voice.source,AL_SOURCE_STATE,&status);
                    if(queued>0&&status!=AL_PLAYING)state.al.alSourcePlay(voice.source);
                    finished=queued==0&&voice.stream->eof;
                } else {ALint status=0;state.al.alGetSourcei(voice.source,AL_SOURCE_STATE,&status);finished=status!=AL_PLAYING;}
            }
            if(finished){state.destroy(voice);state.voices.erase(state.voices.begin()+static_cast<std::ptrdiff_t>(i));}
            else ++i;
        }
    } catch(const std::exception& e){state.error=e.what();state.stop();}
}

void Audio::stopAll() {
    try {if(!impl_->null&&impl_->al.context)impl_->al.current();impl_->stop();}
    catch(const std::exception& e){impl_->error=e.what();}
}
void Audio::setMasterVolume(float volume) {
    if(!std::isfinite(volume))return;
    impl_->master=std::clamp(volume,0.f,1.f);
    if(impl_->master==0)stopAll();
    else updateListener(impl_->listenerX,impl_->listenerY,impl_->listenerZ,impl_->listenerYaw,impl_->listenerPitch);
}
void Audio::setCategoryVolume(SoundCategory category,float volume) {
    if(!std::isfinite(volume)||static_cast<unsigned>(category)>=9)return;
    if(category==SoundCategory::Master){setMasterVolume(volume);return;}
    impl_->categories[static_cast<unsigned>(category)]=std::clamp(volume,0.f,1.f);
    updateListener(impl_->listenerX,impl_->listenerY,impl_->listenerZ,impl_->listenerYaw,impl_->listenerPitch);
    if(volume<=0) {
        try {
            if(!impl_->null&&impl_->al.context)impl_->al.current();
            for(std::size_t i=0;i<impl_->voices.size();) {
                if(impl_->voices[i].sound.category==category) {
                    impl_->destroy(impl_->voices[i]);impl_->voices.erase(impl_->voices.begin()+static_cast<std::ptrdiff_t>(i));
                } else ++i;
            }
        } catch(const std::exception& e){impl_->error=e.what();}
    }
}
bool Audio::enabled() const noexcept{return impl_->ready;}
bool Audio::nullOutput() const noexcept{return impl_->null;}
const std::string& Audio::lastError() const noexcept{return impl_->error;}
std::size_t Audio::activeVoices() const noexcept{return impl_->voices.size();}
std::size_t Audio::decodedBufferCount() const noexcept{return impl_->cache.size();}
const SoundRegistry& Audio::registry() const noexcept{return impl_->registry;}

} // namespace mc
