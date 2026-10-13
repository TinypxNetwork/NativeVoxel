#include "mc/render_assets.hpp"
#include "mc/registry.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace mc {
namespace {
class File {
    std::ifstream input_;
public:
    explicit File(const std::string& path):input_(path,std::ios::binary){if(!input_)throw std::runtime_error("Cannot open render asset: "+path);}
    void bytes(void* target,std::size_t count){if(count&&!input_.read(static_cast<char*>(target),static_cast<std::streamsize>(count)))throw std::runtime_error("Truncated render asset");}
    std::uint8_t u8(){std::uint8_t value;bytes(&value,1);return value;}
    std::uint16_t u16(){auto a=u8(),b=u8();return std::uint16_t(a|(b<<8));}
    std::uint32_t u32(){auto a=u16(),b=u16();return std::uint32_t(a)|(std::uint32_t(b)<<16);}
    float f32(){auto bits=u32();float value;std::memcpy(&value,&bits,4);if(!std::isfinite(value))throw std::runtime_error("Non-finite render asset value");return value;}
    std::string string(){auto count=u32();if(count>65536)throw std::runtime_error("Render asset string too long");std::string value(count,'\0');if(count)bytes(&value[0],count);return value;}
    void magic(const char* expected){char actual[8];bytes(actual,8);if(std::memcmp(actual,expected,8))throw std::runtime_error("Unknown render asset format");}
    void end(){if(input_.peek()!=std::ifstream::traits_type::eof())throw std::runtime_error("Unexpected trailing render asset data");}
};
std::size_t imageBytes(unsigned width,unsigned height) {
    auto bytes=std::uint64_t(width)*height*4;
    if(!width||!height||width>16384||height>16384||bytes>256u*1024u*1024u)throw std::runtime_error("Invalid render image dimensions");
    return static_cast<std::size_t>(bytes);
}
std::uint32_t keyFor(int id,int metadata){return (std::uint32_t(id)<<16)|std::uint16_t(metadata);}
} // namespace

bool UnicodeFont::load(const std::string& directory,std::string& error) {
    try {
        UnicodeFont next;
        File sizes(directory+"/runtime/font/glyph_sizes.bin");sizes.bytes(next.glyphSizes_.data(),next.glyphSizes_.size());sizes.end();
        File map(directory+"/runtime/font/ascii-map.bin");for(auto& entry:next.asciiMap_)entry=map.u16();map.end();
        File widths(directory+"/runtime/font/ascii-widths.bin");widths.bytes(next.asciiWidths_.data(),next.asciiWidths_.size());widths.end();
        next.asciiIndices_.fill(-1);
        for(unsigned i=0;i<256;++i)if(next.asciiIndices_[next.asciiMap_[i]]<0)next.asciiIndices_[next.asciiMap_[i]]=static_cast<std::int16_t>(i);
        std::ifstream pages(directory+"/runtime/font/index.txt");if(!pages)throw std::runtime_error("Missing Unicode font page index");
        std::string line;
        while(std::getline(pages,line)) {
            if(line.empty())continue;
            auto tab=line.find('\t');if(tab==std::string::npos)throw std::runtime_error("Malformed Unicode font page index");
            auto number=line.substr(0,tab);if(number.empty()||number.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Invalid Unicode page number");
            auto page=std::stoul(number);if(page>=256)throw std::runtime_error("Unicode page out of bounds");next.pages_[page]=true;
        }
        next.loaded_=true;*this=std::move(next);error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}

int UnicodeFont::charWidth(std::uint16_t code,bool unicode) const noexcept {
    if(!loaded_)return 0;
    if(code==167)return -1;
    if(code==32)return 4;
    int ascii=asciiIndices_[code];
    if(code>0&&ascii>=0&&!unicode)return asciiWidths_[static_cast<unsigned>(ascii)];
    unsigned bounds=glyphSizes_[code];if(!bounds)return 0;
    int left=bounds>>4,right=bounds&15;
    // This unusual branch is in the actual 1.8.9 getCharWidth, but not in its
    // rendering advance calculation. Keep both results separately in the API.
    if(right>7){right=15;left=0;}
    return (right+1-left)/2+1;
}

GlyphMetrics UnicodeFont::glyph(std::uint16_t code,bool unicode) const noexcept {
    GlyphMetrics result;result.measuredWidth=charWidth(code,unicode);
    if(!loaded_||code==167)return result;
    if(code==32){result.advance=4;return result;}
    int ascii=asciiIndices_[code];
    if(ascii>=0&&!unicode) {
        float x=float(ascii%16*8),y=float(ascii/16*8);
        result.page=-1;result.advance=float(asciiWidths_[static_cast<unsigned>(ascii)]);
        result.width=std::max(0.f,result.advance-1.01f);
        result.u0=x/128;result.v0=y/128;result.u1=(x+result.width)/128;result.v1=(y+7.99f)/128;
        return result;
    }
    unsigned bounds=glyphSizes_[code];if(!bounds||!pages_[code>>8])return result;
    result.correctedLegacyBounds=bounds>=128;
    float left=float(bounds>>4),right=float((bounds&15)+1);
    float x=float(code%16*16)+left,y=float((code&255)/16*16);
    result.page=code>>8;result.width=(right-left-.02f)/2;result.advance=(right-left)/2+1;
    result.u0=x/256;result.v0=y/256;result.u1=(x+right-left-.02f)/256;result.v1=(y+15.98f)/256;
    return result;
}

bool ItemIconAtlas::load(const std::string& directory,std::string& error) {
    try {
        ItemIconAtlas next;File file(directory+"/runtime/items/icons.bin");file.magic("NVITEM01");
        next.width_=file.u32();next.height_=file.u32();auto bytes=imageBytes(next.width_,next.height_);
        auto icons=file.u32(),entries=file.u32(),defaults=file.u32(),frames=file.u32();
        if(!icons||icons>100000||entries>1000000||defaults>65536||frames>1000000)throw std::runtime_error("Invalid item icon table counts");
        next.icons_.resize(icons);
        for(auto& icon:next.icons_) {
            icon.model=file.string();unsigned flags=file.u8(),layers=file.u8();file.u16();
            if(flags>3||layers>5)throw std::runtime_error("Invalid icon flags/layer count");
            icon.valid=(flags&1)!=0;icon.approximate=(flags&2)!=0;icon.layers.resize(layers);
            for(auto& layer:icon.layers) {
                layer.u0=file.f32();layer.v0=file.f32();layer.u1=file.f32();layer.v1=file.f32();
                layer.color=file.u32();auto tint=file.u32();layer.tintIndex=tint==0xffffffffu?-1:static_cast<int>(tint);
                if(layer.tintIndex<-1||layer.tintIndex>4||layer.u0<0||layer.u1>1||layer.v0<0||layer.v1>1||
                   layer.u0>=layer.u1||layer.v0>=layer.v1)throw std::runtime_error("Invalid icon UV/tint");
            }
        }
        next.entries_.reserve(entries);
        for(unsigned i=0;i<entries;++i) {
            Entry entry;entry.id=file.u16();entry.metadata=file.u16();entry.icon=file.u32();for(auto& color:entry.colors)color=file.u32();
            if(entry.icon>=icons||!next.entries_.emplace(keyFor(entry.id,entry.metadata),entry).second)throw std::runtime_error("Invalid/duplicate item icon mapping");
        }
        for(unsigned i=0;i<defaults;++i) {auto id=file.u32(),icon=file.u32();if(id>65535||icon>=icons)throw std::runtime_error("Invalid default item icon");next.defaults_[int(id)]=icon;}
        for(unsigned i=0;i<frames;++i) {auto key=file.u32(),icon=file.u32();if(icon>=icons)throw std::runtime_error("Invalid frame item icon");next.frames_[key]=icon;}
        file.end();File pixels(directory+"/runtime/items/atlas.rgba");next.pixels_.resize(bytes);pixels.bytes(next.pixels_.data(),bytes);pixels.end();
        *this=std::move(next);error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}

ItemIcon ItemIconAtlas::icon(int itemId,int metadata,const ItemAppearance& appearance) const {
    if(icons_.empty())return {};
    if(itemId<0||itemId>65535||!registry::item(itemId).valid)return icons_[0];
    const auto& info=registry::item(itemId);
    int actualMetadata=info.maxDamage>0?0:(metadata&65535);
    auto entry=entries_.find(keyFor(itemId,actualMetadata));
    const Entry* colorEntry=entry==entries_.end()?nullptr:&entry->second;
    std::uint32_t selected=0;
    if(entry!=entries_.end())selected=entry->second.icon;
    else if(auto fallback=defaults_.find(itemId);fallback!=defaults_.end()) {
        selected=fallback->second;auto initial=entries_.find(keyFor(itemId,0));if(initial!=entries_.end())colorEntry=&initial->second;
    }
    if(appearance.frame>=0) {
        auto frame=frames_.find(keyFor(itemId,appearance.frame));if(frame!=frames_.end())selected=frame->second;
    }
    ItemIcon result=icons_[selected];
    for(auto& layer:result.layers) {
        if(layer.tintIndex>=0&&colorEntry)layer.color=colorEntry->colors[static_cast<unsigned>(layer.tintIndex)];
        if(layer.tintIndex==0&&appearance.color>=0)layer.color=0xff000000u|(std::uint32_t(appearance.color)&0xffffffu);
    }
    return result;
}

bool AnimationAtlasCache::load(const std::string& directory,std::string& error,const std::string& target) {
    try {
        if(target.empty()||target.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid animation cache target");
        AnimationAtlasCache next;File file(directory+"/runtime/animations/"+target+".bin");file.magic("NVANIM01");
        auto atlasWidth=file.u32(),atlasHeight=file.u32(),count=file.u32();imageBytes(atlasWidth,atlasHeight);
        if(count>4096)throw std::runtime_error("Too many animated sprites");
        next.animations_.resize(count);std::uint64_t totalBytes=0;
        for(auto& animation:next.animations_) {
            animation.texture=file.string();animation.x=file.u32();animation.y=file.u32();animation.width=file.u32();animation.height=file.u32();
            auto frames=file.u32(),sequence=file.u32();animation.interpolate=file.u8()!=0;file.u8();file.u16();
            auto bytes=imageBytes(animation.width,animation.height);
            if(!frames||frames>65536||!sequence||sequence>131072||std::uint64_t(animation.x)+animation.width>atlasWidth||
               std::uint64_t(animation.y)+animation.height>atlasHeight)throw std::runtime_error("Invalid animation dimensions/frame count");
            totalBytes+=std::uint64_t(bytes)*frames;if(totalBytes>256u*1024u*1024u)throw std::runtime_error("Animation frame data exceeds 256 MiB");
            animation.sequence.resize(sequence);
            for(auto& step:animation.sequence) {
                step.frame=file.u32();step.duration=file.u32();
                if(step.frame>=frames||!step.duration)throw std::runtime_error("Invalid animation frame index/duration");
                animation.duration+=step.duration;
            }
            animation.frames.resize(frames);
            for(auto& pixels:animation.frames){pixels.resize(bytes);file.bytes(pixels.data(),bytes);}
        }
        file.end();*this=std::move(next);error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}

const std::vector<AtlasUpdate>& AnimationAtlasCache::sample(std::uint64_t tick) {
    updates_.clear();
    for(auto& animation:animations_) {
        auto time=tick%animation.duration;std::size_t position=0;
        while(time>=animation.sequence[position].duration){time-=animation.sequence[position].duration;++position;}
        const auto& step=animation.sequence[position];auto next=animation.sequence[(position+1)%animation.sequence.size()].frame;
        std::vector<std::uint8_t> pixels=animation.frames[step.frame];
        if(animation.interpolate&&time>0&&step.frame!=next) {
            double currentWeight=1.0-double(time)/step.duration;const auto& other=animation.frames[next];
            for(std::size_t i=0;i<pixels.size();i+=4) {
                for(unsigned channel=0;channel<3;++channel)pixels[i+channel]=static_cast<std::uint8_t>(
                    double(pixels[i+channel])*currentWeight+double(other[i+channel])*(1.0-currentWeight));
                // TextureAtlasSprite keeps the current frame's alpha unchanged.
            }
        }
        if(pixels!=animation.previous) {
            animation.previous=pixels;
            updates_.push_back({animation.texture,animation.x,animation.y,animation.width,animation.height,std::move(pixels)});
        }
    }
    return updates_;
}

} // namespace mc
