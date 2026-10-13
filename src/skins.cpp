#include "mc/skins.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <wincodec.h>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace mc {
namespace {
constexpr std::size_t maximumPng=1024u*1024u;
struct SkinError:std::runtime_error{using std::runtime_error::runtime_error;};
std::string lower(std::string s){for(char& c:s)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return s;}
int hex(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
std::string uuidKey(const std::string& uuid) {
    std::string key;for(char c:uuid){if(c=='-')continue;if(hex(c)<0)return {};key+=char(c>='A'&&c<='F'?c-'A'+'a':c);}
    return key.size()==32?key:std::string();
}
std::string digest(const std::string& value) {
    std::array<unsigned char,SHA256_DIGEST_LENGTH> bytes{};SHA256(reinterpret_cast<const unsigned char*>(value.data()),value.size(),bytes.data());
    const char* digits="0123456789abcdef";std::string result;result.reserve(64);
    for(auto byte:bytes){result+=digits[byte>>4];result+=digits[byte&15];}return result;
}
std::wstring widePath(const std::string& value) {
    int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(count<=0)throw SkinError("Invalid UTF-8 path");
    std::wstring result(static_cast<std::size_t>(count),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),&result[0],count);return result;
}
std::vector<std::uint8_t> readFile(const std::filesystem::path& path,std::size_t maximum) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input)throw SkinError("Skin file is unavailable");
    auto size=input.tellg();if(size<0||std::uint64_t(size)>maximum)throw SkinError("Skin file exceeds size limit");
    std::vector<std::uint8_t> result(static_cast<std::size_t>(size));input.seekg(0);
    if(!result.empty()&&!input.read(reinterpret_cast<char*>(result.data()),static_cast<std::streamsize>(result.size())))throw SkinError("Truncated skin file");
    return result;
}
std::vector<std::uint8_t> base64(const std::string& value) {
    if(value.size()>98304)throw SkinError("Texture property exceeds size limit");
    std::vector<std::uint8_t> out;std::uint32_t bits=0;int available=0,padding=0;std::size_t digits=0;bool ended=false;
    for(unsigned char c:value) {
        if(c==' '||c=='\r'||c=='\n'||c=='\t')continue;
        if(c=='='){ended=true;if(++padding>2)throw SkinError("Invalid texture property base64 padding");continue;}
        if(ended)throw SkinError("Data after texture property base64 padding");
        int x=c>='A'&&c<='Z'?c-'A':c>='a'&&c<='z'?c-'a'+26:c>='0'&&c<='9'?c-'0'+52:c=='+'||c=='-'?62:c=='/'||c=='_'?63:-1;
        if(x<0)throw SkinError("Invalid texture property base64");
        ++digits;bits=(bits<<6)|std::uint32_t(x);available+=6;
        if(available>=8){available-=8;out.push_back(std::uint8_t(bits>>available));bits&=(1u<<available)-1;}
        if(out.size()>65536)throw SkinError("Decoded texture property exceeds size limit");
    }
    if(available>=6||bits!=0||out.empty()||(padding&&((digits+padding)%4!=0||padding!=(available/2))))throw SkinError("Incomplete texture property base64");
    return out;
}
void utf8(std::string& out,unsigned cp) {
    if(cp<128)out+=char(cp);else if(cp<2048){out+=char(0xc0|(cp>>6));out+=char(0x80|(cp&63));}
    else if(cp<65536){out+=char(0xe0|(cp>>12));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}
    else {out+=char(0xf0|(cp>>18));out+=char(0x80|((cp>>12)&63));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}
}
struct Json {
    enum Type{Other,String,Object} type=Other;
    std::string string;
    std::map<std::string,Json> object;
    const Json* at(const char* name)const{auto it=object.find(name);return it==object.end()?nullptr:&it->second;}
};
class JsonReader {
    const std::string& input_;std::size_t at_=0,nodes_=0;
    void space(){while(at_<input_.size()&&(input_[at_]==' '||input_[at_]=='\r'||input_[at_]=='\n'||input_[at_]=='\t'))++at_;}
    bool take(char c){space();if(at_<input_.size()&&input_[at_]==c){++at_;return true;}return false;}
    [[noreturn]] void fail()const{throw SkinError("Invalid texture property JSON");}
    unsigned hex4(){unsigned n=0;for(int i=0;i<4;++i){if(at_>=input_.size()||hex(input_[at_])<0)fail();n=(n<<4)|unsigned(hex(input_[at_++]));}return n;}
    std::string string() {
        if(!take('"'))fail();
        std::string result;
        while(at_<input_.size()) {
            unsigned char c=static_cast<unsigned char>(input_[at_++]);if(c=='"')return result;if(c<32)fail();
            if(c!='\\'){result+=char(c);continue;}if(at_==input_.size())fail();
            switch(input_[at_++]) {
            case '"':result+='"';break;case '\\':result+='\\';break;case '/':result+='/';break;case 'b':result+='\b';break;
            case 'f':result+='\f';break;case 'n':result+='\n';break;case 'r':result+='\r';break;case 't':result+='\t';break;
            case 'u':{unsigned cp=hex4();if(cp>=0xd800&&cp<=0xdbff){if(at_+2>input_.size()||input_[at_]!='\\'||input_[at_+1]!='u')fail();at_+=2;unsigned low=hex4();if(low<0xdc00||low>0xdfff)fail();cp=0x10000+((cp-0xd800)<<10)+low-0xdc00;}else if(cp>=0xdc00&&cp<=0xdfff)fail();utf8(result,cp);break;}
            default:fail();
            }
        }
        fail();
    }
    Json value(unsigned depth) {
        if(depth>32||++nodes_>4096)throw SkinError("Texture JSON nesting/node limit exceeded");
        space();if(at_>=input_.size())fail();
        Json result;
        if(input_[at_]=='"'){result.type=Json::String;result.string=string();return result;}
        if(take('{')){result.type=Json::Object;if(take('}'))return result;do{auto name=string();if(!take(':'))fail();result.object[name]=value(depth+1);}while(take(','));if(!take('}'))fail();return result;}
        if(take('[')){if(take(']'))return result;do{value(depth+1);}while(take(','));if(!take(']'))fail();return result;}
        for(const char* literal:{"true","false","null"}){std::size_t n=std::strlen(literal);if(input_.compare(at_,n,literal)==0){at_+=n;return result;}}
        if(input_[at_]=='-')++at_;
        if(at_>=input_.size())fail();
        if(input_[at_]=='0')++at_;else{if(input_[at_]<'1'||input_[at_]>'9')fail();while(at_<input_.size()&&input_[at_]>='0'&&input_[at_]<='9')++at_;}
        if(at_<input_.size()&&input_[at_]=='.'){++at_;auto start=at_;while(at_<input_.size()&&input_[at_]>='0'&&input_[at_]<='9')++at_;if(at_==start)fail();}
        if(at_<input_.size()&&(input_[at_]=='e'||input_[at_]=='E')){++at_;if(at_<input_.size()&&(input_[at_]=='+'||input_[at_]=='-'))++at_;auto start=at_;while(at_<input_.size()&&input_[at_]>='0'&&input_[at_]<='9')++at_;if(at_==start)fail();}
        return result;
    }
public:
    explicit JsonReader(const std::string& s):input_(s){}
    Json parse(){auto result=value(0);space();if(at_!=input_.size())fail();return result;}
};
template<class T> struct ComPtr {
    T* value=nullptr;~ComPtr(){if(value)value->Release();}T** address(){return &value;}T* operator->()const{return value;}
};
struct ComInit {HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);~ComInit(){if(SUCCEEDED(result))CoUninitialize();}};
void checkHr(HRESULT result,const char* message){if(FAILED(result))throw SkinError(message);}
struct InternetHandle {
    HINTERNET value=nullptr;
    explicit InternetHandle(HINTERNET h=nullptr):value(h){}
    ~InternetHandle(){if(value)WinHttpCloseHandle(value);}
    InternetHandle(const InternetHandle&)=delete;InternetHandle& operator=(const InternetHandle&)=delete;
};
SkinImage defaultImage(const std::string& directory,const char* name) {
    auto data=readFile(std::filesystem::u8path(directory+"/runtime/entities/minecraft/"+name+".rgba"),64*64*4+12);
    auto u32=[&](std::size_t p){return std::uint32_t(data[p])|(std::uint32_t(data[p+1])<<8)|(std::uint32_t(data[p+2])<<16)|(std::uint32_t(data[p+3])<<24);};
    if(data.size()!=64*64*4+12||std::memcmp(data.data(),"NVTX",4)||u32(4)!=64||u32(8)!=64)throw SkinError("Default player skin must be64x64 RGBA");
    SkinImage result;result.pixels.assign(data.begin()+12,data.end());return result;
}
// Java2D's 8-bit SrcOver operation converts through fixed-point premultiplied
// channels even when the destination is transparent. The rounded reciprocal
// matters at half-integers; ordinary division rounded to nearest is different.
std::uint8_t javaDrawChannel(std::uint8_t channel,std::uint8_t alpha) {
    if(alpha==0)return 0;
    if(alpha==255)return channel;
    const std::uint32_t a=alpha;
    const std::uint32_t premultiplied=(channel*(a*65793u)+(1u<<23))>>24;
    const std::uint64_t reciprocal=((std::uint64_t(255)<<24)+a/2)/a;
    return std::uint8_t((premultiplied*reciprocal+(1u<<23))>>24);
}
} // namespace

bool SkinCache::canonicalTextureUrl(const std::string& input,std::string& output,std::string& error) {
    try {
        if(input.size()>512)throw SkinError("Skin URL exceeds size limit");
        auto value=lower(input);const std::string https="https://textures.minecraft.net/texture/",http="http://textures.minecraft.net/texture/";
        std::string hash;
        if(value.compare(0,https.size(),https)==0)hash=value.substr(https.size());
        else if(value.compare(0,http.size(),http)==0)hash=value.substr(http.size());
        else throw SkinError("Skin URL must use the exact Mojang texture host and texture path");
        if(hash.size()<16||hash.size()>128||std::any_of(hash.begin(),hash.end(),[](char c){return hex(c)<0;}))throw SkinError("Invalid Mojang texture identifier");
        output=https+hash;error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}

bool SkinCache::parseProperty(const std::string& encoded,SkinDescriptor& descriptor,std::string& error) {
    try {
        auto bytes=base64(encoded);std::string text(bytes.begin(),bytes.end());auto json=JsonReader(text).parse();
        auto textures=json.at("textures");auto skin=textures?textures->at("SKIN"):nullptr;auto url=skin?skin->at("url"):nullptr;
        if(!url||url->type!=Json::String)throw SkinError("Texture property has no skin URL");
        SkinDescriptor next;if(!canonicalTextureUrl(url->string,next.url,error))return false;
        auto metadata=skin->at("metadata");auto model=metadata?metadata->at("model"):nullptr;
        next.slim=model&&model->type==Json::String&&model->string=="slim";descriptor=std::move(next);error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}

bool SkinCache::normalize(unsigned width,unsigned height,const std::vector<std::uint8_t>& rgba,SkinImage& image,std::string& error) {
    try {
        if(width!=64||(height!=32&&height!=64)||rgba.size()!=std::size_t(width)*height*4)throw SkinError("Skin must be64x64 or legacy64x32 RGBA");
        SkinImage next;next.pixels.assign(64*64*4,0);
        std::copy(rgba.begin(),rgba.end(),next.pixels.begin());
        // Java draws the input over a transparent TYPE_INT_ARGB destination.
        for(std::size_t i=0;i<rgba.size();i+=4)for(int c=0;c<3;++c)next.pixels[i+c]=javaDrawChannel(next.pixels[i+c],next.pixels[i+3]);
        auto copy=[&](int dx1,int dy1,int dx2,int dy2,int sx1,int sy1,int sx2,int sy2) {
            int w=sx2-sx1,h=sy2-sy1;
            for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
                int dx=dx2<dx1?dx1-1-x:dx1+x,dy=dy2<dy1?dy1-1-y:dy1+y;
                const auto* source=next.pixels.data()+((sy1+y)*64+sx1+x)*4;auto* target=next.pixels.data()+(dy*64+dx)*4;
                for(int c=0;c<3;++c)target[c]=javaDrawChannel(source[c],source[3]);
                target[3]=source[3];
            }
        };
        if(height==32) {
            copy(24,48,20,52,4,16,8,20);copy(28,48,24,52,8,16,12,20);copy(20,52,16,64,8,20,12,32);
            copy(24,52,20,64,4,20,8,32);copy(28,52,24,64,0,20,4,32);copy(32,52,28,64,12,20,16,32);
            copy(40,48,36,52,44,16,48,20);copy(44,48,40,52,48,16,52,20);copy(36,52,32,64,48,20,52,32);
            copy(40,52,36,64,44,20,48,32);copy(44,52,40,64,40,20,44,32);copy(48,52,44,64,52,20,56,32);
        }
        auto opaque=[&](int x0,int y0,int x1,int y1){for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)next.pixels[(y*64+x)*4+3]=255;};
        auto transparent=[&](int x0,int y0,int x1,int y1) {
            for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)if(next.pixels[(y*64+x)*4+3]<128)return;
            for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)next.pixels[(y*64+x)*4+3]=0;
        };
        opaque(0,0,32,16);transparent(32,0,64,32);opaque(0,16,64,32);
        transparent(0,32,16,48);transparent(16,32,40,48);transparent(40,32,56,48);transparent(0,48,16,64);
        opaque(16,48,48,64);transparent(48,48,64,64);
        image=std::move(next);error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}

bool SkinCache::decodePng(const std::vector<std::uint8_t>& encoded,SkinImage& image,std::string& error) {
    try {
        const std::uint8_t signature[]={137,80,78,71,13,10,26,10};
        if(encoded.size()<8||encoded.size()>maximumPng||std::memcmp(encoded.data(),signature,8))throw SkinError("Invalid or oversized skin PNG");
        ComInit apartment;if(FAILED(apartment.result)&&apartment.result!=RPC_E_CHANGED_MODE)throw SkinError("Cannot initialize native image decoder");
        ComPtr<IWICImagingFactory> factory;checkHr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,reinterpret_cast<void**>(factory.address())),"Cannot create native PNG decoder");
        ComPtr<IWICStream> stream;checkHr(factory->CreateStream(stream.address()),"Cannot create skin image stream");
        checkHr(stream->InitializeFromMemory(const_cast<BYTE*>(encoded.data()),static_cast<DWORD>(encoded.size())),"Cannot read skin image memory");
        ComPtr<IWICBitmapDecoder> decoder;checkHr(factory->CreateDecoderFromStream(stream.value,nullptr,WICDecodeMetadataCacheOnLoad,decoder.address()),"Cannot decode skin PNG");
        GUID format{};checkHr(decoder->GetContainerFormat(&format),"Cannot identify skin image format");if(format!=GUID_ContainerFormatPng)throw SkinError("Skin image is not PNG");
        UINT frames=0;checkHr(decoder->GetFrameCount(&frames),"Cannot inspect skin PNG");if(frames!=1)throw SkinError("Animated skin images are unsupported");
        ComPtr<IWICBitmapFrameDecode> frame;checkHr(decoder->GetFrame(0,frame.address()),"Cannot get skin PNG frame");
        UINT width=0,height=0;checkHr(frame->GetSize(&width,&height),"Cannot inspect skin dimensions");
        if(width!=64||(height!=32&&height!=64))throw SkinError("Skin PNG dimensions must be64x64 or64x32");
        ComPtr<IWICFormatConverter> converter;checkHr(factory->CreateFormatConverter(converter.address()),"Cannot create skin pixel converter");
        checkHr(converter->Initialize(frame.value,GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Cannot convert skin pixels");
        std::vector<std::uint8_t> rgba(std::size_t(width)*height*4);checkHr(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(rgba.size()),rgba.data()),"Cannot copy skin pixels");
        return normalize(width,height,rgba,image,error);
    } catch(const std::exception& e){error=e.what();return false;}
}

bool SkinCache::defaultSlim(const std::string& uuid) noexcept {
    unsigned parity=0,count=0;
    for(char c:uuid){if(c=='-')continue;int n=hex(c);if(n<0)return false;if((count&7u)==7)parity^=unsigned(n)&1u;++count;}
    return count==32&&(parity&1u)!=0;
}

struct SkinCache::Impl {
    struct Entry {
        enum State {Ready,Queued,Invalid,Failed} state=Ready;
        std::string value,signature,error;std::uint64_t generation=0;std::shared_ptr<const PlayerSkin> skin;
        unsigned failures=0;std::chrono::steady_clock::time_point retryAfter{};
    };
    struct Job {std::string uuid;SkinDescriptor descriptor;std::uint64_t generation=0,epoch=0;};
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::unordered_map<std::string,Entry> entries;
    std::unordered_map<std::string,std::shared_ptr<const SkinImage>> images;
    std::deque<Job> jobs;
    std::shared_ptr<const PlayerSkin> steve,alex;
    std::thread worker;
    std::atomic<HINTERNET> activeRequest{nullptr};
    bool stop=false,network=true,busy=false;
    std::uint64_t epoch=0,revision=0;
    std::filesystem::path cacheDirectory;
    ~Impl(){shutdown();}
    void cancelRequest(){if(auto request=activeRequest.exchange(nullptr))WinHttpCloseHandle(request);}
    void shutdown() {
        {std::lock_guard<std::mutex> lock(mutex);stop=true;jobs.clear();cancelRequest();}
        changed.notify_all();if(worker.joinable())worker.join();
    }
    std::vector<std::uint8_t> fetch(const std::string& url,std::uint64_t requestEpoch) {
        InternetHandle session(WinHttpOpen(L"NativeVoxel/1.8.9",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
        if(!session.value)throw SkinError("Cannot open HTTPS skin session");
        if(!WinHttpSetTimeouts(session.value,3000,3000,5000,5000))throw SkinError("Cannot set bounded HTTPS timeouts");
        InternetHandle connection(WinHttpConnect(session.value,L"textures.minecraft.net",INTERNET_DEFAULT_HTTPS_PORT,0));
        if(!connection.value)throw SkinError("Cannot connect to Mojang texture host");
        std::wstring path=widePath(url.substr(std::string("https://textures.minecraft.net").size()));
        HINTERNET request=WinHttpOpenRequest(connection.value,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
        if(!request)throw SkinError("Cannot create HTTPS skin request");
        {std::lock_guard<std::mutex> lock(mutex);if(stop||requestEpoch!=epoch){WinHttpCloseHandle(request);throw SkinError("Skin request cancelled");}activeRequest.store(request);}
        struct Close {Impl& self;~Close(){self.cancelRequest();}} close{*this};
        DWORD disabled=WINHTTP_DISABLE_REDIRECTS;
        if(!WinHttpSetOption(request,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled)))throw SkinError("Cannot disable skin HTTP redirects");
        DWORD policy=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
        if(!WinHttpSetOption(request,WINHTTP_OPTION_AUTOLOGON_POLICY,&policy,sizeof(policy)))throw SkinError("Cannot disable automatic HTTP credentials");
        // Deliberately no certificate-error bypass flags and no HTTP redirects.
        if(!WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request,nullptr))throw SkinError("Verified HTTPS skin request failed");
        DWORD status=0,size=sizeof(status);if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX)||status!=200)throw SkinError("Mojang texture request did not return HTTP200");
        DWORD length=0;size=sizeof(length);
        if(WinHttpQueryHeaders(request,WINHTTP_QUERY_CONTENT_LENGTH|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&length,&size,WINHTTP_NO_HEADER_INDEX)&&length>maximumPng)throw SkinError("Downloaded skin exceeds size limit");
        std::vector<std::uint8_t> data;std::array<std::uint8_t,8192> block{};
        for(;;){DWORD got=0;if(!WinHttpReadData(request,block.data(),static_cast<DWORD>(block.size()),&got))throw SkinError("Cannot read HTTPS skin response");if(!got)break;if(data.size()+got>maximumPng)throw SkinError("Downloaded skin exceeds size limit");data.insert(data.end(),block.begin(),block.begin()+got);}
        return data;
    }
    void run() {
        for(;;) {
            Job job;
            {std::unique_lock<std::mutex> lock(mutex);changed.wait(lock,[&]{return stop||!jobs.empty();});if(stop)return;job=std::move(jobs.front());jobs.pop_front();busy=true;}
            std::shared_ptr<const SkinImage> image;std::string error;
            const std::string key=digest(job.descriptor.url);
            try {
                {std::lock_guard<std::mutex> lock(mutex);auto found=images.find(key);if(found!=images.end())image=found->second;}
                if(!image) {
                    SkinImage decoded;std::vector<std::uint8_t> bytes;const auto file=cacheDirectory/(key+".png");
                    try{bytes=readFile(file,maximumPng);if(!SkinCache::decodePng(bytes,decoded,error))bytes.clear();}catch(const std::exception&){bytes.clear();}
                    if(bytes.empty()) {
                        if(!network)throw SkinError("Skin not cached; networking is disabled");
                        bytes=fetch(job.descriptor.url,job.epoch);if(!SkinCache::decodePng(bytes,decoded,error))throw SkinError(error);
                        try {
                            std::filesystem::create_directories(cacheDirectory);
                            auto temporary=file;temporary+=L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
                            {std::ofstream output(temporary,std::ios::binary);output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));if(!output)throw SkinError("Skin cache write failed");}
                            MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
                        } catch(const std::exception&){} // Read-only asset folders still support in-memory skins.
                    }
                    image=std::make_shared<SkinImage>(std::move(decoded));
                }
            } catch(const std::exception& e){error=e.what();}
            {
                std::lock_guard<std::mutex> lock(mutex);busy=false;
                auto found=entries.find(job.uuid);
                if(!stop&&job.epoch==epoch&&found!=entries.end()&&found->second.generation==job.generation) {
                    auto& entry=found->second;
                    if(image){if(images.size()>=256)images.clear();images[key]=image;auto skin=std::make_shared<PlayerSkin>();skin->image=*image;skin->slim=job.descriptor.slim;skin->downloaded=true;skin->textureKey="mojang:"+key;skin->revision=++revision;entry.skin=std::move(skin);entry.error.clear();entry.state=Entry::Ready;entry.failures=0;}
                    else {entry.error=error;entry.state=Entry::Failed;entry.failures=std::min(6u,entry.failures+1);entry.retryAfter=std::chrono::steady_clock::now()+std::chrono::seconds(std::min(300u,5u<<entry.failures));}
                }
            }
        }
    }
};

SkinCache::SkinCache():impl_(new Impl){}
SkinCache::~SkinCache()=default;
bool SkinCache::initialize(const std::string& directory,std::string& error,bool network) {
    try {
        std::unique_ptr<Impl> next(new Impl);next->network=network;next->cacheDirectory=std::filesystem::u8path(directory+"/runtime/skin-cache");
        auto steve=std::make_shared<PlayerSkin>();steve->image=defaultImage(directory,"steve");steve->textureKey="minecraft:entity/steve";
        auto alex=std::make_shared<PlayerSkin>();alex->image=defaultImage(directory,"alex");alex->textureKey="minecraft:entity/alex";alex->slim=true;
        next->steve=std::move(steve);next->alex=std::move(alex);
        next->worker=std::thread([state=next.get()]{state->run();});impl_.swap(next);next.reset();error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}
bool SkinCache::request(const std::string& uuid,const std::string& value,const std::string& signature) {
    const std::string id=uuidKey(uuid);if(id.empty()||value.size()>98304||signature.size()>16384)return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if(!impl_->steve||impl_->stop)return false;
    auto found=impl_->entries.find(id);
    const bool same=found!=impl_->entries.end()&&found->second.value==value&&found->second.signature==signature;
    if(same){const auto& entry=found->second;if(entry.state!=Impl::Entry::Failed)return entry.state!=Impl::Entry::Invalid;if(std::chrono::steady_clock::now()<entry.retryAfter)return false;}
    if(found==impl_->entries.end()&&impl_->entries.size()>=4096)return false;
    SkinDescriptor descriptor;std::string error;bool valid=value.empty()||parseProperty(value,descriptor,error);
    auto& entry=impl_->entries[id];entry.value=value;entry.signature=signature;++entry.generation;
    if(!same){entry.failures=0;entry.skin.reset();}
    if(!valid){entry.error=error;entry.state=Impl::Entry::Invalid;return false;}
    entry.error.clear();
    if(value.empty()){entry.skin.reset();entry.state=Impl::Entry::Ready;return true;}
    if(impl_->jobs.size()>=256){entry.error="Skin queue is full";entry.state=Impl::Entry::Failed;entry.retryAfter=std::chrono::steady_clock::now()+std::chrono::seconds(1);return false;}
    entry.state=Impl::Entry::Queued;
    impl_->jobs.push_back({id,descriptor,entry.generation,impl_->epoch});impl_->changed.notify_one();return true;
}
std::shared_ptr<const PlayerSkin> SkinCache::get(const std::string& uuid) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);auto found=impl_->entries.find(uuidKey(uuid));
    if(found!=impl_->entries.end()&&found->second.skin)return found->second.skin;
    return defaultSlim(uuid)?impl_->alex:impl_->steve;
}
std::size_t SkinCache::pending()const noexcept{std::lock_guard<std::mutex> lock(impl_->mutex);return impl_->jobs.size()+(impl_->busy?1:0);}
std::string SkinCache::lastError(const std::string& uuid)const{std::lock_guard<std::mutex> lock(impl_->mutex);auto found=impl_->entries.find(uuidKey(uuid));return found==impl_->entries.end()?std::string():found->second.error;}
void SkinCache::clear(){std::lock_guard<std::mutex> lock(impl_->mutex);++impl_->epoch;impl_->entries.clear();impl_->images.clear();impl_->jobs.clear();impl_->cancelRequest();}

} // namespace mc
