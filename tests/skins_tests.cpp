#include "mc/skins.hpp"
#include <openssl/sha.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
unsigned checks=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
std::vector<std::uint8_t> read(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot read "+path.u8string());
    return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
void copyBinary(const std::filesystem::path& source,const std::filesystem::path& target) {
    auto bytes=read(source);std::filesystem::create_directories(target.parent_path());
    std::ofstream out(target,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if(!out)throw std::runtime_error("Cannot write test fixture "+target.u8string());
}
std::uint32_t be32(const std::vector<std::uint8_t>& data,std::size_t& at) {
    if(at+4>data.size())throw std::runtime_error("Truncated Java skin fixture");
    std::uint32_t result=0;for(int i=0;i<4;++i)result=(result<<8)|data[at++];return result;
}
std::string b64(const std::string& value) {
    const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;unsigned bits=0;int count=0;
    for(unsigned char c:value){bits=(bits<<8)|c;count+=8;while(count>=6){count-=6;out+=alphabet[(bits>>count)&63];}}
    if(count)out+=alphabet[(bits<<(6-count))&63];
    while(out.size()%4)out+='=';
    return out;
}
std::string sha256(const std::string& value) {
    std::array<unsigned char,32> bytes{};SHA256(reinterpret_cast<const unsigned char*>(value.data()),value.size(),bytes.data());
    const char* h="0123456789abcdef";std::string out;for(auto b:bytes){out+=h[b>>4];out+=h[b&15];}return out;
}
void drain(mc::SkinCache& cache) {
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(cache.pending()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    require(cache.pending()==0,"Offline skin worker did not drain");
}
void bytesEqual(const std::vector<std::uint8_t>& actual,const std::vector<std::uint8_t>& expected,const std::string& label) {
    require(actual.size()==expected.size(),label+" size");std::size_t differences=0,first=0;
    for(std::size_t i=0;i<actual.size();++i){++checks;if(actual[i]!=expected[i]){if(!differences)first=i;++differences;}}
    if(differences)throw std::runtime_error(label+": "+std::to_string(differences)+" byte differences; first="+std::to_string(first)+" actual="+std::to_string(actual[first])+" Java="+std::to_string(expected[first]));
}
}

int main(int argc,char** argv) {
    try {
        auto root=std::filesystem::absolute(argc>1?argv[1]:".");std::string error;
        auto fixture=read(root/"tools/reference/skin_reference.bin");require(fixture.size()>12,"Missing Java fixture");
        require(std::string(fixture.begin(),fixture.begin()+8)=="NVSKREF1","Java fixture magic");
        std::size_t at=8;auto count=be32(fixture,at);require(count==20,"Java fixture count");
        for(unsigned c=0;c<count;++c) {
            unsigned w=be32(fixture,at),h=be32(fixture,at);std::size_t n=std::size_t(w)*h*4;
            require(at+n+16384<=fixture.size(),"Truncated case");
            std::vector<std::uint8_t> input(fixture.begin()+at,fixture.begin()+at+n);at+=n;
            std::vector<std::uint8_t> expected(fixture.begin()+at,fixture.begin()+at+16384);at+=16384;
            mc::SkinImage image;require(mc::SkinCache::normalize(w,h,input,image,error),error);
            require(image.width==64&&image.height==64,"Normalized dimensions");
            bytesEqual(image.pixels,expected,"Java normalize case "+std::to_string(c));
            if(c<4) {
                auto encoded=read(root/("tools/reference/skin_input_"+std::to_string(c)+".png"));
                require(mc::SkinCache::decodePng(encoded,image,error),error);
                bytesEqual(image.pixels,expected,"WIC/Java PNG case "+std::to_string(c));
            }
        }
        require(at==fixture.size(),"Unexpected Java fixture suffix");
        mc::SkinImage invalid;
        require(!mc::SkinCache::normalize(63,64,std::vector<std::uint8_t>(63*64*4),invalid,error),"Reject wrong width");
        require(!mc::SkinCache::normalize(64,33,std::vector<std::uint8_t>(64*33*4),invalid,error),"Reject wrong height");
        require(!mc::SkinCache::normalize(64,64,{1,2,3},invalid,error),"Reject wrong byte size");
        require(!mc::SkinCache::decodePng({1,2,3},invalid,error),"Reject non-PNG");
        auto truncated=read(root/"tools/reference/skin_input_0.png");truncated.resize(30);
        require(!mc::SkinCache::decodePng(truncated,invalid,error),"Reject truncated PNG");
        require(!mc::SkinCache::decodePng(std::vector<std::uint8_t>(1024*1024+1),invalid,error),"Reject oversized PNG");
        for(const auto& name:{"steve","alex"}) {
            auto png=read(root/(std::string("assets/resources/assets/minecraft/textures/entity/")+name+".png"));
            require(mc::SkinCache::decodePng(png,invalid,error),std::string(name)+": "+error);
        }

        const std::string url="https://textures.minecraft.net/texture/0123456789abcdef0123456789abcdef";std::string canonical;
        require(mc::SkinCache::canonicalTextureUrl("HTTP://TEXTURES.MINECRAFT.NET/texture/0123456789ABCDEF0123456789ABCDEF",canonical,error),error);
        require(canonical==url,"HTTPS canonicalization");
        for(const auto& bad:{"file:///C:/skin.png","https://example.com/texture/0123456789abcdef","http://textures.minecraft.net.example.com/texture/0123456789abcdef","https://textures.minecraft.net:443/texture/0123456789abcdef","https://user@textures.minecraft.net/texture/0123456789abcdef","https://textures.minecraft.net/texture/0123456789abcdef?x=1","https://textures.minecraft.net/texture/0123456789abcdef#x","https://textures.minecraft.net/texture/../../file","https://textures.minecraft.net/texture/abcd","https://textures.minecraft.net/texture/0123456789abcdef/other","https://textures.minecraft.net/texture/0123456789abcdef%00"})
            require(!mc::SkinCache::canonicalTextureUrl(bad,canonical,error),std::string("Reject URL ")+bad);
        const auto property=b64("{\"textures\":{\"SKIN\":{\"url\":\""+url+"\",\"metadata\":{\"model\":\"slim\"}}}}");
        mc::SkinDescriptor descriptor;require(mc::SkinCache::parseProperty(property,descriptor,error),error);
        require(descriptor.url==url&&descriptor.slim,"Skin URL and slim property");
        for(const auto& bad:{"{}","[]","{\"textures\":null}","{\"textures\":{\"SKIN\":{\"url\":123}}}","{\"textures\":{\"SKIN\":{\"url\":\"file:///test\"}}}","{\"x\":01}","{\"x\":\"\\ud800\"}","{\"x\":\"\\udc00\"}","{\"x\":true}x"})
            require(!mc::SkinCache::parseProperty(b64(bad),descriptor,error),std::string("Reject JSON ")+bad);
        require(!mc::SkinCache::parseProperty(std::string(98305,'A'),descriptor,error),"Reject encoded size");
        require(!mc::SkinCache::parseProperty(b64(std::string(33,'[')+"0"+std::string(33,']')),descriptor,error),"Reject JSON depth");
        for(const auto& bad:{"@","A","e30=A","e30===","e30==","e31=","===="})require(!mc::SkinCache::parseProperty(bad,descriptor,error),"Reject malformed base64");
        require(!mc::SkinCache::defaultSlim("00000000-0000-0000-0000-000000000000"),"Steve UUID");
        require(mc::SkinCache::defaultSlim("00000000-0000-0000-0000-000000000001"),"Alex UUID");
        require(!mc::SkinCache::defaultSlim("00000000-0000-0001-0000-000000000001"),"UUID word XOR");
        require(!mc::SkinCache::defaultSlim("invalid"),"Invalid UUID fallback");
        for(unsigned bit=0;bit<128;++bit){std::string id(32,'0');id[31-bit/4]="1248"[bit%4];require(mc::SkinCache::defaultSlim(id)==(bit%32==0),"Java UUID.hashCode parity");}

        const auto assets=root/"build/skin_tests_assets";
        for(const auto& name:{"steve","alex"}){auto target=assets/(std::string("runtime/entities/minecraft/")+name+".rgba");copyBinary(root/(std::string("assets/runtime/entities/minecraft/")+name+".rgba"),target);}
        auto cachePng=assets/("runtime/skin-cache/"+sha256(url)+".png");copyBinary(root/"tools/reference/skin_input_1.png",cachePng);
        mc::SkinCache cache;const std::string uuid="00000000-0000-0000-0000-000000000001";
        require(!cache.get(uuid),"Uninitialized cache");require(!cache.request(uuid,property),"Reject before initialization");
        require(cache.initialize(assets.u8string(),error,false),error);
        require(cache.get(uuid)&&cache.get(uuid)->slim&&!cache.get(uuid)->downloaded,"Default Alex fallback");
        require(cache.request(uuid,property,"test-signature"),"Queue disk-cached skin");drain(cache);
        require(cache.get(uuid)->downloaded&&cache.get(uuid)->slim,"Loaded skin model");require(cache.lastError(uuid).empty(),"Loaded skin no error");
        auto revision=cache.get(uuid)->revision;require(revision>0,"Skin revision");
        require(cache.request(uuid,property,"test-signature")&&cache.pending()==0&&cache.get(uuid)->revision==revision,"Unchanged request does not decode again");
        require(cache.request(uuid,""),"Remove property");require(!cache.get(uuid)->downloaded,"Removed property reverts to default");
        require(!cache.request(uuid,b64("{}")),"Malformed property fails");require(!cache.lastError(uuid).empty(),"Malformed property diagnostic");
        const auto missing=b64("{\"textures\":{\"SKIN\":{\"url\":\"https://textures.minecraft.net/texture/ffffffffffffffff\"}}}");
        require(cache.request(uuid,missing),"Queue uncached offline skin");drain(cache);require(!cache.lastError(uuid).empty()&&!cache.get(uuid)->downloaded,"Offline failure keeps default");
        require(!cache.request(uuid,missing)&&cache.pending()==0,"Failed download has retry backoff");
        for(unsigned i=0;i<100;++i){auto id=std::string(28,'0')+"abcd";require(cache.request(id,property)||!cache.lastError(id).empty(),"Repeated enqueue");cache.clear();}
        drain(cache);require(!cache.get(uuid)->downloaded,"Clear discards obsolete results");
        require(cache.initialize(assets.u8string(),error,false),error);require(cache.request(uuid,property),"Reinitialize worker");drain(cache);require(cache.get(uuid)->downloaded,"Reinitialized cache loads");
        require(!cache.initialize((assets/"missing").u8string(),error,false),"Reject missing default textures");require(cache.get(uuid)->downloaded,"Failed initialize preserves current cache");
        std::cout<<checks<<" skin checks passed; 20 Java image fixtures, 4 native PNG decodes, offline worker lifecycle\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Skin tests failed: "<<e.what()<<'\n';return 1;}
}
