#include "mc/resource_packs.hpp"
#include "mc/assets.hpp"
#include "mc/render_assets.hpp"
#include "mc/skins.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <zlib.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincodec.h>
#endif

namespace mc {
namespace {
namespace fs=std::filesystem;
using Bytes=std::vector<std::uint8_t>;
[[noreturn]] void fail(const std::string& message){throw std::runtime_error(message);}
void validUtf8(const std::string& value){
    for(std::size_t i=0;i<value.size();){unsigned c=static_cast<unsigned char>(value[i++]),cp=c,extra=0,minimum=0;if(c<128)continue;
        if(c>=0xc2&&c<=0xdf){cp=c&31;extra=1;minimum=0x80;}else if(c>=0xe0&&c<=0xef){cp=c&15;extra=2;minimum=0x800;}else if(c>=0xf0&&c<=0xf4){cp=c&7;extra=3;minimum=0x10000;}else fail("Resource path is not valid UTF-8");
        if(i+extra>value.size())fail("Truncated UTF-8 resource path");for(unsigned j=0;j<extra;++j){unsigned next=static_cast<unsigned char>(value[i++]);if((next&0xc0)!=0x80)fail("Invalid UTF-8 resource path");cp=(cp<<6)|(next&63);}
        if(cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))fail("Non-canonical UTF-8 resource path");
    }
}
std::string safeResourcePath(const std::string& name){
    validUtf8(name);
    if(name.empty()||name.size()>1024||name.front()=='/'||name.find('\\')!=std::string::npos||name.find(':')!=std::string::npos)fail("Unsafe resource path: "+name);
    std::size_t begin=0;
    while(begin<name.size()){
        const auto end=name.find('/',begin);const auto part=name.substr(begin,end==std::string::npos?end:end-begin);
        if(part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' ')fail("Unsafe resource path component");
        for(unsigned char c:part)if(c<32||c==127||c=='<'||c=='>'||c=='"'||c=='|'||c=='?'||c=='*')fail("Invalid resource path character");
        std::string device=part.substr(0,part.find('.'));for(char& c:device)if(c>='a'&&c<='z')c-=32;
        if(device=="CON"||device=="PRN"||device=="AUX"||device=="NUL"||(device.size()==4&&(device.substr(0,3)=="COM"||device.substr(0,3)=="LPT")&&device[3]>='1'&&device[3]<='9'))fail("Reserved device name in resource path");
        if(end==std::string::npos)break;begin=end+1;if(begin==name.size())fail("Resource path must name a file");
    }
    return name;
}
#ifdef _WIN32
std::wstring widePath(const fs::path& path){
    auto value=fs::absolute(path).wstring();if(value.rfind(L"\\\\?\\",0)==0)return value;
    if(value.rfind(L"\\\\",0)==0)return L"\\\\?\\UNC\\"+value.substr(2);return L"\\\\?\\"+value;
}
class Handle {
public:
    HANDLE value{INVALID_HANDLE_VALUE};explicit Handle(HANDLE h):value(h){}
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
#endif
Bytes readFile(const fs::path& path,std::size_t limit=64u*1024*1024){
#ifdef _WIN32
    Handle file(CreateFileW(widePath(path).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(file.value==INVALID_HANDLE_VALUE)fail("Cannot open resource: "+path.u8string());LARGE_INTEGER size;
    if(!GetFileSizeEx(file.value,&size)||size.QuadPart<0||std::uint64_t(size.QuadPart)>limit)fail("Resource exceeds size limit: "+path.u8string());Bytes bytes(static_cast<std::size_t>(size.QuadPart));
    std::size_t offset=0;while(offset<bytes.size()){DWORD got=0,requested=static_cast<DWORD>(std::min<std::size_t>(1024*1024,bytes.size()-offset));if(!ReadFile(file.value,bytes.data()+offset,requested,&got,nullptr)||got==0)fail("Cannot read resource: "+path.u8string());offset+=got;}return bytes;
#else
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)fail("Cannot open resource: "+path.u8string());const auto size=file.tellg();if(size<0||std::uint64_t(size)>limit)fail("Resource exceeds size limit");Bytes bytes(static_cast<std::size_t>(size));file.seekg(0);if(!bytes.empty()&&!file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))fail("Cannot read resource");return bytes;
#endif
}
#ifdef _WIN32
template<class T>struct ComPtr {T* value{};~ComPtr(){if(value)value->Release();}T** address(){return &value;}};
void hresult(HRESULT result,const char* message){if(FAILED(result)){std::ostringstream out;out<<message<<" (0x"<<std::hex<<std::uint32_t(result)<<')';fail(out.str());}}
struct ComApartment {bool release{};ComApartment(){const auto result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(result!=RPC_E_CHANGED_MODE)hresult(result,"Cannot initialize PNG decoder");release=SUCCEEDED(result);}~ComApartment(){if(release)CoUninitialize();}};
struct PngDecoder {ComApartment apartment;ComPtr<IWICImagingFactory> factory;PngDecoder(){hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,reinterpret_cast<void**>(factory.address())),"Cannot create PNG decoder");}};
#endif
ResourceImage decodePng(const Bytes& bytes,bool packIcon=false){
    static constexpr unsigned char signature[]={137,80,78,71,13,10,26,10};
    if(bytes.size()<8||(!packIcon&&std::memcmp(bytes.data(),signature,8)))fail("Resource is not a PNG image");
    if(bytes.size()>64u*1024*1024)fail("PNG file exceeds size limit");
#ifdef _WIN32
    thread_local PngDecoder decoderContext;
    auto* factory=decoderContext.factory.value;
    ComPtr<IWICStream> stream;hresult(factory->CreateStream(stream.address()),"Cannot create PNG stream");
    hresult(stream.value->InitializeFromMemory(const_cast<BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size())),"Cannot initialize PNG stream");
    ComPtr<IWICBitmapDecoder> decoder;hresult(factory->CreateDecoderFromStream(stream.value,nullptr,WICDecodeMetadataCacheOnLoad,decoder.address()),"Cannot decode PNG");
    ComPtr<IWICBitmapFrameDecode> frame;hresult(decoder.value->GetFrame(0,frame.address()),"Cannot read PNG frame");ResourceImage image;
    hresult(frame.value->GetSize(&image.width,&image.height),"Cannot read PNG dimensions");
    const auto size=std::uint64_t(image.width)*image.height*4;
    if(!image.width||!image.height||image.width>16384||image.height>65536||size>256u*1024*1024)fail("PNG dimensions exceed resource limits");
    ComPtr<IWICFormatConverter> converter;hresult(factory->CreateFormatConverter(converter.address()),"Cannot create PNG color converter");
    hresult(converter.value->Initialize(frame.value,GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Cannot convert PNG to RGBA");
    image.pixels.resize(static_cast<std::size_t>(size));hresult(converter.value->CopyPixels(nullptr,image.width*4,static_cast<UINT>(size),image.pixels.data()),"Cannot read PNG pixels");return image;
#else
    fail("Native resource PNG decoding currently requires Windows WIC");
#endif
}
bool starts(const std::string& value,const std::string& prefix){return value.compare(0,prefix.size(),prefix)==0;}
bool ends(const std::string& value,const std::string& suffix){return value.size()>=suffix.size()&&value.compare(value.size()-suffix.size(),suffix.size(),suffix)==0;}
std::string lower(std::string value){for(char& c:value)if(c>='A'&&c<='Z')c+=32;return value;}
bool within(const fs::path& candidate,const fs::path& parent){const auto p=lower(parent.generic_u8string()),c=lower(candidate.generic_u8string());return c==p||starts(c,p+"/");}
bool reparse(const fs::path& path){
    if(fs::is_symlink(fs::symlink_status(path)))return true;
#ifdef _WIN32
    const auto flags=GetFileAttributesW(widePath(path).c_str());return flags!=INVALID_FILE_ATTRIBUTES&&(flags&FILE_ATTRIBUTE_REPARSE_POINT)!=0;
#else
    return false;
#endif
}
void writeFile(const fs::path& path,const Bytes& bytes){
    fs::create_directories(path.parent_path());
#ifdef _WIN32
    Handle file(CreateFileW(widePath(path).c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));if(file.value==INVALID_HANDLE_VALUE)fail("Cannot write resource cache: "+path.u8string());
    std::size_t offset=0;while(offset<bytes.size()){DWORD written=0,requested=static_cast<DWORD>(std::min<std::size_t>(1024*1024,bytes.size()-offset));if(!WriteFile(file.value,bytes.data()+offset,requested,&written,nullptr)||written==0)fail("Cannot finish resource cache file");offset+=written;}
#else
    std::ofstream file(path,std::ios::binary|std::ios::trunc);if(!file||(!bytes.empty()&&!file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size())))fail("Cannot write resource cache");file.close();if(!file)fail("Cannot finish resource cache");
#endif
}
void writeText(const fs::path& path,const std::string& value){writeFile(path,Bytes(value.begin(),value.end()));}
void copyBinary(const fs::path& source,const fs::path& target){
    fs::create_directories(target.parent_path());
#ifdef _WIN32
    if(CopyFileW(widePath(source).c_str(),widePath(target).c_str(),FALSE))return;
#endif
    writeFile(target,readFile(source,512u*1024*1024));
}
struct Json {
    enum class Type {Null,Boolean,Number,String,Array,Object};Type type{Type::Null};bool boolean{};double number{};std::string string;std::vector<Json> array;std::map<std::string,Json> object;
    const Json& operator[](const std::string& key) const {static const Json nil;const auto found=object.find(key);return found==object.end()?nil:found->second;}
    bool has(const std::string& key) const {return object.count(key)!=0;}
    std::string text(const std::string& fallback={}) const {return type==Type::String?string:fallback;}
    int integer(int fallback=0) const {if(type==Type::Null)return fallback;if(type!=Type::Number||number!=std::floor(number)||number<std::numeric_limits<int>::min()||number>std::numeric_limits<int>::max())fail("Expected JSON integer");return int(number);}
};
void utf8(std::string& out,unsigned cp){if(cp<128)out.push_back(char(cp));else if(cp<2048){out.push_back(char(0xc0|(cp>>6)));out.push_back(char(0x80|(cp&63)));}else if(cp<65536){out.push_back(char(0xe0|(cp>>12)));out.push_back(char(0x80|((cp>>6)&63)));out.push_back(char(0x80|(cp&63)));}else{out.push_back(char(0xf0|(cp>>18)));out.push_back(char(0x80|((cp>>12)&63)));out.push_back(char(0x80|((cp>>6)&63)));out.push_back(char(0x80|(cp&63)));}}
class JsonReader {
    const std::string& input;std::size_t at{},nodes{};
    void spaces(){while(at<input.size()&&(input[at]==' '||input[at]=='\t'||input[at]=='\n'||input[at]=='\r'))++at;}
    char take(){if(at==input.size())fail("Truncated JSON");return input[at++];}
    unsigned hex4(){unsigned n=0;for(int i=0;i<4;++i){char c=take();int digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;if(digit<0)fail("Invalid JSON Unicode escape");n=n*16+unsigned(digit);}return n;}
    std::string string(){if(take()!='"')fail("Expected JSON string");std::string out;for(;;){unsigned char c=take();if(c=='"')break;if(c<32)fail("Control character in JSON string");if(c!='\\'){out.push_back(char(c));continue;}char escape=take();switch(escape){case '"':case '\\':case '/':out.push_back(escape);break;case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;case 'u':{unsigned cp=hex4();if(cp>=0xd800&&cp<=0xdbff){if(take()!='\\'||take()!='u')fail("Unpaired JSON surrogate");const unsigned low=hex4();if(low<0xdc00||low>0xdfff)fail("Unpaired JSON surrogate");cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);}else if(cp>=0xdc00&&cp<=0xdfff)fail("Unpaired JSON surrogate");utf8(out,cp);break;}default:fail("Invalid JSON escape");}}return out;}
    Json value(unsigned depth){
        if(depth>64||++nodes>1000000)fail("Resource JSON exceeds nesting/node limits");spaces();if(at==input.size())fail("Missing JSON value");Json out;const char c=input[at];
        if(c=='"'){out.type=Json::Type::String;out.string=string();}
        else if(c=='{'){++at;out.type=Json::Type::Object;spaces();if(at<input.size()&&input[at]=='}'){++at;return out;}for(;;){spaces();auto key=string();spaces();if(take()!=':')fail("Expected JSON colon");out.object[key]=value(depth+1);spaces();const char next=take();if(next=='}')break;if(next!=',')fail("Expected JSON object separator");}}
        else if(c=='['){++at;out.type=Json::Type::Array;spaces();if(at<input.size()&&input[at]==']'){++at;return out;}for(;;){out.array.push_back(value(depth+1));spaces();const char next=take();if(next==']')break;if(next!=',')fail("Expected JSON array separator");}}
        else if(c=='t'||c=='f'||c=='n'){const std::string word=c=='t'?"true":c=='f'?"false":"null";if(input.compare(at,word.size(),word)!=0)fail("Invalid JSON literal");at+=word.size();if(c!='n'){out.type=Json::Type::Boolean;out.boolean=c=='t';}}
        else{const auto begin=at;if(input[at]=='-')++at;if(at==input.size())fail("Invalid JSON number");if(input[at]=='0')++at;else{if(input[at]<'1'||input[at]>'9')fail("Invalid JSON number");while(at<input.size()&&input[at]>='0'&&input[at]<='9')++at;}if(at<input.size()&&input[at]=='.'){++at;const auto digits=at;while(at<input.size()&&input[at]>='0'&&input[at]<='9')++at;if(at==digits)fail("Invalid JSON fraction");}if(at<input.size()&&(input[at]=='e'||input[at]=='E')){++at;if(at<input.size()&&(input[at]=='+'||input[at]=='-'))++at;const auto digits=at;while(at<input.size()&&input[at]>='0'&&input[at]<='9')++at;if(at==digits)fail("Invalid JSON exponent");}std::istringstream stream(input.substr(begin,at-begin));stream.imbue(std::locale::classic());stream>>out.number;if(!stream||!std::isfinite(out.number))fail("Non-finite JSON number");out.type=Json::Type::Number;}
        return out;
    }
public:
    explicit JsonReader(const std::string& text):input(text){}
    Json parse(){if(input.size()>16u*1024*1024)fail("Resource JSON is too large");validUtf8(input);if(input.size()>=3&&std::uint8_t(input[0])==0xef&&std::uint8_t(input[1])==0xbb&&std::uint8_t(input[2])==0xbf)at=3;auto out=value(0);spaces();if(at!=input.size())fail("Trailing JSON data");return out;}
};
Json json(const Bytes& bytes){const std::string text(bytes.begin(),bytes.end());return JsonReader(text).parse();}
std::string quote(const std::string& value){std::string out="\"";static const char digits[]="0123456789abcdef";for(unsigned char c:value){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){out+="\\u00";out+=digits[c>>4];out+=digits[c&15];}else out+=char(c);}return out+'"';}
std::string jsonText(const Json& value){switch(value.type){case Json::Type::Null:return "null";case Json::Type::Boolean:return value.boolean?"true":"false";case Json::Type::Number:{std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17)<<value.number;return out.str();}case Json::Type::String:return quote(value.string);case Json::Type::Array:{std::string out="[";bool first=true;for(const auto& item:value.array){if(!first)out+=',';first=false;out+=jsonText(item);}return out+']';}case Json::Type::Object:{std::string out="{";bool first=true;for(const auto& item:value.object){if(!first)out+=',';first=false;out+=quote(item.first)+':'+jsonText(item.second);}return out+'}';}}return "null";}
Json number(double value){Json result;result.type=Json::Type::Number;result.number=value;return result;}
Json text(const std::string& value){Json result;result.type=Json::Type::String;result.string=value;return result;}
std::string description(const Json& value){if(value.type==Json::Type::String)return value.string;if(value.type==Json::Type::Array){std::string result;for(const auto& item:value.array)result+=description(item);return result;}if(value.type==Json::Type::Object)return value["text"].text(value["translate"].text())+description(value["extra"]);return {};}
std::uint16_t u16(const Bytes& b,std::size_t p){if(p>b.size()||b.size()-p<2)fail("Truncated binary resource");return std::uint16_t(b[p]|(unsigned(b[p+1])<<8));}
std::uint32_t u32(const Bytes& b,std::size_t p){if(p>b.size()||b.size()-p<4)fail("Truncated binary resource");return std::uint32_t(u16(b,p))|(std::uint32_t(u16(b,p+2))<<16);}
void put32(Bytes& b,std::size_t p,std::uint32_t value){if(p>b.size()||b.size()-p<4)fail("Invalid binary patch offset");for(int i=0;i<4;++i)b[p+i]=std::uint8_t(value>>(i*8));}
void add32(Bytes& b,std::uint32_t value){for(int i=0;i<4;++i)b.push_back(std::uint8_t(value>>(i*8)));}
void addString(Bytes& b,const std::string& value){add32(b,static_cast<std::uint32_t>(value.size()));b.insert(b.end(),value.begin(),value.end());}
struct Source {
    fs::path file;std::shared_ptr<Bytes> archive;std::size_t offset{},compressed{},size{};unsigned method{};std::uint32_t checksum{};
    Bytes read() const {
        if(!archive)return readFile(file);
        Bytes output(std::max<std::size_t>(1,size));
        if(method==0){if(compressed!=size)fail("Stored ZIP entry has inconsistent size");std::copy_n(archive->data()+offset,size,output.data());}
        else{z_stream stream{};if(inflateInit2(&stream,-MAX_WBITS)!=Z_OK)fail("Cannot initialize ZIP inflater");stream.next_in=archive->data()+offset;stream.avail_in=static_cast<uInt>(compressed);stream.next_out=output.data();stream.avail_out=static_cast<uInt>(output.size());const int result=inflate(&stream,Z_FINISH);const bool valid=result==Z_STREAM_END&&stream.total_in==compressed&&stream.total_out==size;inflateEnd(&stream);if(!valid)fail("Corrupt ZIP entry payload");}
        output.resize(size);if(static_cast<std::uint32_t>(crc32(0,output.data(),static_cast<uInt>(output.size())))!=checksum)fail("Resource pack ZIP CRC mismatch");return output;
    }
};
void stageResource(const Source& source,const fs::path& target){
    if(source.archive){writeFile(target,source.read());return;}
    copyBinary(source.file,target);
}
using Sources=std::map<std::string,Source>;
Sources folderFiles(const fs::path& root){
    Sources result;std::set<std::string> folded;std::uint64_t total=0;
    for(fs::recursive_directory_iterator it(root),end;it!=end;++it){if(reparse(it->path()))fail("Resource pack/cache contains a symbolic link or junction");if(it->is_directory())continue;if(!it->is_regular_file())fail("Resource pack contains a non-regular file");const auto name=safeResourcePath(fs::relative(it->path(),root).generic_u8string());const auto leaf=name.substr(name.find_last_of('/')==std::string::npos?0:name.find_last_of('/')+1);if(starts(name,"__MACOSX/")||starts(leaf,"._"))continue;if(!folded.insert(lower(name)).second)fail("Case-conflicting resource paths");const auto size=it->file_size();if(size>64u*1024*1024||total+size>1024u*1024*1024)fail("Resource pack exceeds file/total size limit");total+=size;if(result.size()>=100000)fail("Too many resource pack files");result.emplace(name,Source{it->path(),{},0,0,std::size_t(size),0,0});}
    return result;
}
Sources zipFiles(const fs::path& path){
    auto archive=std::make_shared<Bytes>(readFile(path,512u*1024*1024));const auto& bytes=*archive;if(bytes.size()<22)fail("Truncated resource pack ZIP");std::size_t end=bytes.size();
    const auto first=bytes.size()>65557?bytes.size()-65557:0;
    for(std::size_t p=bytes.size()-22;;--p){if(u32(bytes,p)==0x06054b50&&p+22+u16(bytes,p+20)==bytes.size()){end=p;break;}if(p==first)break;}
    if(end==bytes.size())fail("ZIP end record not found");const auto count=u16(bytes,end+10);const auto start=u32(bytes,end+16),length=u32(bytes,end+12);
    if(u16(bytes,end+4)||u16(bytes,end+6)||u16(bytes,end+8)!=count)fail("Multi-disk resource pack ZIP is unsupported");
    if(count==65535||start==0xffffffffu||length==0xffffffffu)fail("ZIP64 resource packs are unsupported");
    if(std::uint64_t(start)+length>end)fail("Invalid ZIP central directory range");Sources result;std::set<std::string> folded;std::vector<std::pair<std::size_t,std::size_t>> spans;std::uint64_t total=0;std::size_t p=start;
    for(unsigned entry=0;entry<count;++entry){
        if(p+46>std::size_t(start)+length||u32(bytes,p)!=0x02014b50)fail("Invalid ZIP directory entry");const auto flags=u16(bytes,p+8),method=u16(bytes,p+10);const auto compressed=u32(bytes,p+20),size=u32(bytes,p+24),local=u32(bytes,p+42);const auto nameLength=u16(bytes,p+28),extra=u16(bytes,p+30),comment=u16(bytes,p+32);
        if(p+46+nameLength+extra+comment>std::size_t(start)+length)fail("Truncated ZIP entry name");std::string name(reinterpret_cast<const char*>(bytes.data()+p+46),nameLength);const bool directory=!name.empty()&&name.back()=='/';if(directory)name.pop_back();safeResourcePath(name);
        if(!folded.insert(lower(name)).second)fail("Duplicate/case-conflicting ZIP entry");const auto attributes=u32(bytes,p+38);if(((attributes>>16)&0170000u)==0120000u||(attributes&0x400u))fail("Symbolic link in resource pack ZIP");
        if((flags&1)||u16(bytes,p+34)||method!=0&&method!=8)fail("Encrypted or unsupported resource pack ZIP entry");if(size>64u*1024*1024||compressed>512u*1024*1024||total+size>1024u*1024*1024)fail("ZIP entry exceeds resource size limits");total+=size;
        if(std::uint64_t(local)+30>start||u32(bytes,local)!=0x04034b50||u16(bytes,local+8)!=method||u16(bytes,local+6)!=flags)fail("Invalid ZIP local header");const auto localName=u16(bytes,local+26),localExtra=u16(bytes,local+28);const std::size_t data=std::size_t(local)+30+localName+localExtra;
        if(std::uint64_t(data)+compressed>start||localName!=nameLength||std::memcmp(bytes.data()+local+30,bytes.data()+p+46,nameLength))fail("ZIP local/central paths or ranges disagree");spans.emplace_back(local,data+compressed);
        const auto leaf=name.substr(name.find_last_of('/')==std::string::npos?0:name.find_last_of('/')+1);
        if(!directory&&!starts(name,"__MACOSX/")&&!starts(leaf,"._"))result.emplace(name,Source{{},archive,data,compressed,size,method,u32(bytes,p+16)});else if(directory&&size)fail("ZIP directory entry contains data");p+=46+nameLength+extra+comment;
    }
    if(p!=std::size_t(start)+length)fail("ZIP central directory size mismatch");std::sort(spans.begin(),spans.end());for(std::size_t i=1;i<spans.size();++i)if(spans[i].first<spans[i-1].second)fail("Overlapping ZIP entries");return result;
}
struct Pack {ResourcePackInfo info;Sources files;};
std::string selectionKey(const fs::path& base,const std::vector<Pack>& packs){
    std::uint64_t hash=14695981039346656037ull;
    const auto add=[&](const std::string& value){for(unsigned char byte:value){hash^=byte;hash*=1099511628211ull;}hash^=0xff;hash*=1099511628211ull;};
    add("native-resource-cache-v5");
    const auto stamp=[&](const fs::path& path){add(path.u8string());add(std::to_string(fs::file_size(path)));add(std::to_string(fs::last_write_time(path).time_since_epoch().count()));};
    stamp(base/"assets.bin");stamp(base/"atlas.rgba");stamp(base/"runtime/items/icons.bin");
    for(const auto& pack:packs){add(pack.info.path);if(!pack.info.directory)stamp(fs::u8path(pack.info.path));
        else for(const auto& entry:pack.files){add(entry.first);stamp(entry.second.file);}}
    std::ostringstream value;value<<std::hex<<std::setw(16)<<std::setfill('0')<<hash;return value.str();
}
Pack openPack(const std::string& path){
    Pack result;const auto resolved=fs::weakly_canonical(fs::absolute(fs::u8path(path)));result.info.path=resolved.u8string();result.info.name=resolved.filename().u8string();result.info.directory=fs::is_directory(resolved);
    if(!result.info.directory&&!fs::is_regular_file(resolved))fail("Resource pack is not a folder or ZIP file");result.files=result.info.directory?folderFiles(resolved):zipFiles(resolved);
    const auto found=result.files.find("pack.mcmeta");if(found==result.files.end())fail("Resource pack has no pack.mcmeta at its root");const auto meta=json(found->second.read());const auto& pack=meta["pack"];
    if(pack.type!=Json::Type::Object||!pack.has("pack_format"))fail("Invalid resource pack metadata");result.info.format=pack["pack_format"].integer();if(result.info.format<=0)fail("Invalid resource pack format number");result.info.description=description(pack["description"]);
    result.info.files=result.files.size();for(const auto& source:result.files)result.info.bytes+=source.second.size;return result;
}
Bytes resource(const Sources& sources,const std::string& name){const auto found=sources.find(safeResourcePath(name));if(found==sources.end())fail("Missing resource: "+name);return found->second.read();}
std::string texturePath(const std::string& name){const auto colon=name.find(':');const auto domain=colon==std::string::npos?"minecraft":name.substr(0,colon);const auto path=colon==std::string::npos?name:name.substr(colon+1);return safeResourcePath("assets/"+domain+"/textures/"+path+".png");}
ResourceImage crop(const ResourceImage& source,unsigned x,unsigned y,unsigned width,unsigned height){
    if(!width||!height||std::uint64_t(x)+width>source.width||std::uint64_t(y)+height>source.height)fail("Invalid image crop");ResourceImage out;out.width=width;out.height=height;out.pixels.resize(std::size_t(width)*height*4);
    for(unsigned row=0;row<height;++row)std::copy_n(source.pixels.data()+(std::size_t(y+row)*source.width+x)*4,std::size_t(width)*4,out.pixels.data()+std::size_t(row)*width*4);return out;
}
Bytes nvtx(const ResourceImage& image){Bytes out={'N','V','T','X'};add32(out,image.width);add32(out,image.height);out.insert(out.end(),image.pixels.begin(),image.pixels.end());return out;}
struct Rect {unsigned x{},y{},width{},height{};double u0{},v0{},u1{},v1{};};
struct Sprite {ResourceImage image,sheet;std::vector<std::pair<unsigned,unsigned>> sequence;bool interpolate{};};
Sprite sprite(const Sources& sources,const std::string& name,int explicitFrame=-1){
    const auto path=texturePath(name);Sprite out;out.image=decodePng(resource(sources,path));Json animation;
    const auto metadata=sources.find(path+".mcmeta");if(metadata!=sources.end())animation=json(metadata->second.read())["animation"];
    if(animation.type!=Json::Type::Null){
        if(animation.type!=Json::Type::Object||out.image.height%out.image.width)fail("Invalid animation metadata/aspect ratio: "+name);const unsigned frames=out.image.height/out.image.width;const int duration=animation["frametime"].integer(1);if(duration<=0)fail("Animation frame time must be positive");
        if(animation.has("frames")&&animation["frames"].type!=Json::Type::Array)fail("Animation frames must be an array");
        for(const auto& frame:animation["frames"].array){const int index=frame.type==Json::Type::Number?frame.integer():frame["index"].integer(-1);const int time=frame.type==Json::Type::Number?duration:frame["time"].integer(duration);if(index<0||unsigned(index)>=frames||time<=0)fail("Invalid animation frame index/time");out.sequence.emplace_back(unsigned(index),unsigned(time));}
        if(out.sequence.empty())for(unsigned i=0;i<frames;++i)out.sequence.emplace_back(i,unsigned(duration));if(out.sequence.size()>131072)fail("Too many animation sequence entries");
        out.interpolate=animation["interpolate"].type==Json::Type::Boolean&&animation["interpolate"].boolean;out.sheet=std::move(out.image);const unsigned selected=explicitFrame<0?out.sequence.front().first:unsigned(explicitFrame)%frames;out.image=crop(out.sheet,0,selected*out.sheet.width,out.sheet.width,out.sheet.width);
    }else if(explicitFrame>=0&&out.image.height>out.image.width){if(out.image.height%out.image.width)fail("Invalid clock/compass image strip");const unsigned frame=unsigned(explicitFrame)%(out.image.height/out.image.width);out.image=crop(out.image,0,frame*out.image.width,out.image.width,out.image.width);}
    return out;
}
void pastePadded(ResourceImage& target,const ResourceImage& image,const Rect& at,unsigned padding){
    if(at.x<padding||at.y<padding||std::uint64_t(at.x)+image.width+padding>target.width||std::uint64_t(at.y)+image.height+padding>target.height)fail("Atlas placement out of bounds");
    for(int y=-int(padding);y<int(image.height+padding);++y)for(int x=-int(padding);x<int(image.width+padding);++x){const unsigned sx=unsigned(std::clamp(x,0,int(image.width)-1)),sy=unsigned(std::clamp(y,0,int(image.height)-1));std::copy_n(image.pixels.data()+(std::size_t(sy)*image.width+sx)*4,4,target.pixels.data()+(std::size_t(int(at.y)+y)*target.width+unsigned(int(at.x)+x))*4);}
}
std::map<std::string,Rect> packSprites(const std::map<std::string,Sprite>& sprites,ResourceImage& image,unsigned padding=2){
    if(sprites.empty()||padding<1||padding>16)fail("Invalid texture atlas inputs");std::vector<std::string> order;std::uint64_t area=0;unsigned largest=0;
    for(const auto& entry:sprites){const auto& im=entry.second.image;area+=std::uint64_t(im.width+padding*2)*(im.height+padding*2);largest=std::max(largest,im.width+padding*2);order.push_back(entry.first);}if(area>64u*1024*1024)fail("Resource pack atlas exceeds 256 MiB");
    std::sort(order.begin(),order.end(),[&](const std::string& a,const std::string& b){const auto& x=sprites.at(a).image;const auto& y=sprites.at(b).image;return x.height!=y.height?x.height>y.height:x.width!=y.width?x.width>y.width:a<b;});
    unsigned width=1;while(width<largest||double(width)<std::sqrt(double(area))*1.08)width*=2;if(width>16384)fail("Resource pack atlas is too wide");
    std::map<std::string,Rect> rectangles;unsigned x=0,y=0,row=0;for(const auto& name:order){const auto& im=sprites.at(name).image;if(x+im.width+padding*2>width){x=0;y+=row;row=0;}rectangles[name]={x+padding,y+padding,im.width,im.height};x+=im.width+padding*2;row=std::max(row,im.height+padding*2);}
    unsigned height=1;while(height<y+row)height*=2;if(height>16384||std::uint64_t(width)*height*4>256u*1024*1024)fail("Resource pack atlas exceeds native renderer limits");image.width=width;image.height=height;image.pixels.assign(std::size_t(width)*height*4,0);
    for(auto& entry:rectangles){auto& rect=entry.second;rect.u0=(rect.x+.01)/width;rect.v0=(rect.y+.01)/height;rect.u1=(rect.x+rect.width-.01)/width;rect.v1=(rect.y+rect.height-.01)/height;pastePadded(image,sprites.at(entry.first).image,rect,padding);}return rectangles;
}
std::map<std::string,Rect> terrainRects(const Json& metadata){
    std::map<std::string,Rect> result;for(const auto& entry:metadata["textures"].object){const auto& j=entry.second;Rect r{unsigned(j["x"].integer()),unsigned(j["y"].integer()),unsigned(j["width"].integer()),unsigned(j["height"].integer()),j["u0"].number,j["v0"].number,j["u1"].number,j["v1"].number};if(!r.width||!r.height||r.u1<=r.u0||r.v1<=r.v0)fail("Invalid base atlas rectangle");result.emplace(entry.first,r);}return result;
}
float f32(const Bytes& bytes,std::size_t at){const auto bits=u32(bytes,at);float value;std::memcpy(&value,&bits,4);if(!std::isfinite(value))fail("Non-finite cached texture coordinate");return value;}
void putFloat(Bytes& bytes,std::size_t at,double value){const float v=static_cast<float>(value);std::uint32_t bits;std::memcpy(&bits,&v,4);put32(bytes,at,bits);}
const std::string& rectangleFor(const std::map<std::string,Rect>& rectangles,double u0,double v0,double u1,double v1){
    constexpr double epsilon=.000002;for(const auto& entry:rectangles){const auto& r=entry.second;if(u0>=r.u0-epsilon&&u1<=r.u1+epsilon&&v0>=r.v0-epsilon&&v1<=r.v1+epsilon)return entry.first;}fail("Cannot identify a base model's atlas texture");
}
void remapTerrain(Bytes& cache,const std::map<std::string,Rect>& old,const std::map<std::string,Rect>& next,unsigned width,unsigned height){
    if(cache.size()<36||std::memcmp(cache.data(),"NVASSET1",8))fail("Invalid base block-model cache");const unsigned count=u32(cache,24);std::size_t at=36;
    for(unsigned model=0;model<count;++model){const unsigned quads=u32(cache,at+4);at+=8;for(unsigned q=0;q<quads;++q,at+=100){if(at+100>cache.size())fail("Truncated base model cache");double u0=2,v0=2,u1=-1,v1=-1;for(unsigned vertex=0;vertex<4;++vertex){const auto p=at+20+vertex*20;const float u=f32(cache,p+12),v=f32(cache,p+16);u0=std::min(u0,double(u));v0=std::min(v0,double(v));u1=std::max(u1,double(u));v1=std::max(v1,double(v));}
        const auto& name=rectangleFor(old,u0,v0,u1,v1);const auto& a=old.at(name);const auto& b=next.at(name);for(unsigned vertex=0;vertex<4;++vertex){const auto p=at+20+vertex*20;const double u=f32(cache,p+12),v=f32(cache,p+16);putFloat(cache,p+12,b.u0+(u-a.u0)/(a.u1-a.u0)*(b.u1-b.u0));putFloat(cache,p+16,b.v0+(v-a.v0)/(a.v1-a.v0)*(b.v1-b.v0));}
    }}put32(cache,8,width);put32(cache,12,height);
}
#include "resource_baking.inc"
void terrainAtlas(const Sources& resources,const fs::path& stage,PreparedResourcePacks& report){
    NativeModels models(resources);const auto definitions=blockDefinitions(resources);const auto extraTextures=modelTextures(definitions,models);
    const auto metadata=json(readFile(stage/"atlas.json"));const auto old=terrainRects(metadata);ResourceImage previous;previous.width=unsigned(metadata["width"].integer());previous.height=unsigned(metadata["height"].integer());previous.pixels=readFile(stage/"atlas.rgba",512u*1024*1024);if(previous.pixels.size()!=std::size_t(previous.width)*previous.height*4)fail("Invalid base atlas pixels");
    std::map<std::string,Sprite> sprites;std::uint64_t decoded=0;for(const auto& entry:old){Sprite value;if(entry.first=="native:missing")value.image=crop(previous,entry.second.x,entry.second.y,entry.second.width,entry.second.height);else{value=sprite(resources,entry.first);++report.decodedImages;}decoded+=value.image.pixels.size()+value.sheet.pixels.size();if(decoded>512u*1024*1024)fail("Decoded resource textures exceed memory limit");sprites.emplace(entry.first,std::move(value));}
    for(const auto& name:extraTextures)if(!sprites.count(name)){auto value=sprite(resources,name);decoded+=value.image.pixels.size()+value.sheet.pixels.size();if(decoded>512u*1024*1024)fail("Decoded resource textures exceed memory limit");sprites.emplace(name,std::move(value));++report.decodedImages;}
    const unsigned padding=unsigned(metadata["padding"].integer(2));ResourceImage atlas;const auto rectangles=packSprites(sprites,atlas,padding);auto cache=readFile(stage/"assets.bin",256u*1024*1024);remapTerrain(cache,old,rectangles,atlas.width,atlas.height);rebakeTerrain(cache,definitions,models,rectangles,report);writeFile(stage/"assets.bin",cache);writeFile(stage/"atlas.rgba",atlas.pixels);
    Json next;next.type=Json::Type::Object;next.object["width"]=number(atlas.width);next.object["height"]=number(atlas.height);next.object["padding"]=number(padding);next.object["format"]=text("RGBA8");next.object["origin"]=text("top_left");Json textures;textures.type=Json::Type::Object;
    for(const auto& entry:rectangles){const auto& r=entry.second;Json j;j.type=Json::Type::Object;j.object["x"]=number(r.x);j.object["y"]=number(r.y);j.object["width"]=number(r.width);j.object["height"]=number(r.height);j.object["u0"]=number(r.u0);j.object["v0"]=number(r.v0);j.object["u1"]=number(r.u1);j.object["v1"]=number(r.v1);textures.object[entry.first]=j;}next.object["textures"]=std::move(textures);writeText(stage/"atlas.json",jsonText(next));
    Bytes animations={'N','V','A','N','I','M','0','1'};add32(animations,atlas.width);add32(animations,atlas.height);unsigned count=0;for(const auto& entry:sprites)if(!entry.second.sequence.empty())++count;add32(animations,count);std::uint64_t total=0;
    for(const auto& entry:sprites){const auto& s=entry.second;if(s.sequence.empty())continue;const auto& r=rectangles.at(entry.first);const unsigned frames=s.sheet.height/s.sheet.width,w=r.width+padding*2,h=r.height+padding*2;total+=std::uint64_t(w)*h*4*frames;if(total>256u*1024*1024)fail("Animated resource textures exceed native cache limit");addString(animations,entry.first);for(unsigned value:{r.x-padding,r.y-padding,w,h,frames,unsigned(s.sequence.size())})add32(animations,value);animations.push_back(s.interpolate?1:0);animations.insert(animations.end(),3,0);for(const auto& frame:s.sequence){add32(animations,frame.first);add32(animations,frame.second);}for(unsigned frame=0;frame<frames;++frame){ResourceImage padded;padded.width=w;padded.height=h;padded.pixels.assign(std::size_t(w)*h*4,0);const auto pixels=crop(s.sheet,0,frame*s.sheet.width,s.sheet.width,s.sheet.width);pastePadded(padded,pixels,{padding,padding,pixels.width,pixels.height},padding);animations.insert(animations.end(),padded.pixels.begin(),padded.pixels.end());}}
    writeFile(stage/"runtime/animations/terrain.bin",animations);
}
std::map<std::string,std::string> indexFile(const fs::path& path){std::map<std::string,std::string> result;if(!fs::exists(path))return result;const auto data=readFile(path);std::istringstream input(std::string(data.begin(),data.end()));std::string line;while(std::getline(input,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;const auto tab=line.find('\t');if(tab==std::string::npos)fail("Malformed runtime asset index");result[line.substr(0,tab)]=safeResourcePath(line.substr(tab+1));}return result;}
void writeIndex(const fs::path& path,const std::map<std::string,std::string>& values){std::string result;for(const auto& value:values)result+=value.first+'\t'+value.second+'\n';writeText(path,result);}
Bytes fontWidths(const ResourceImage& image){if(image.width!=image.height||image.width%16)fail("ASCII resource font must be square with a 16x16 glyph grid");const unsigned cell=image.width/16;Bytes widths(256);for(unsigned glyph=0;glyph<256;++glyph){unsigned right=0;for(unsigned x=0;x<cell;++x)for(unsigned y=0;y<cell;++y)if(image.pixels[(std::size_t(glyph/16*cell+y)*image.width+glyph%16*cell+x)*4+3])right=x+1;widths[glyph]=std::uint8_t(int(.5+double(right)*8/cell)+1);}return widths;}
void runtimeImages(const Sources& sources,const fs::path& stage,PreparedResourcePacks& report){
    auto gui=indexFile(stage/"runtime/gui/index.txt"),fonts=indexFile(stage/"runtime/font/index.txt"),entities=indexFile(stage/"runtime/entities/index.txt"),equipment=indexFile(stage/"runtime/equipment/index.txt");
    for(const auto& entry:sources){const auto& path=entry.first;if(!ends(path,".png"))continue;
        if(starts(path,"assets/minecraft/textures/gui/")){const auto name=path.substr(30,path.size()-30-4);const auto image=decodePng(entry.second.read());++report.decodedImages;writeFile(stage/"runtime/gui"/fs::u8path(name+".rgba"),nvtx(image));gui[name]=name+".rgba";}
        const std::string unicodePrefix="assets/minecraft/textures/font/unicode_page_";
        if(starts(path,unicodePrefix)&&path.size()==unicodePrefix.size()+6){const auto digits=path.substr(unicodePrefix.size(),2);if(digits.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)continue;const auto page=std::stoul(digits,nullptr,16);const auto name="unicode_page_"+lower(digits)+".rgba";const auto image=decodePng(entry.second.read());++report.decodedImages;writeFile(stage/"runtime/font"/name,nvtx(image));fonts[std::to_string(page)]=name;}
        if(starts(path,"assets/")&&path.find("/textures/entity/")!=std::string::npos){const auto split=path.find('/',7);if(split==std::string::npos)continue;const auto ns=path.substr(7,split-7),prefix="assets/"+ns+"/textures/entity/";if(!starts(path,prefix))continue;const auto name=path.substr(prefix.size(),path.size()-prefix.size()-4),target=ns+"/"+name+".rgba";const auto image=decodePng(entry.second.read());++report.decodedImages;writeFile(stage/"runtime/entities"/fs::u8path(target),nvtx(image));entities[ns+":entity/"+name]=target;}
        if(starts(path,"assets/minecraft/textures/models/armor/")){const auto name=path.substr(39,path.size()-39-4);if(name.empty()||name.find('/')!=std::string::npos)continue;const auto image=decodePng(entry.second.read());++report.decodedImages;writeFile(stage/"runtime/equipment"/(name+".rgba"),nvtx(image));equipment["minecraft:models/armor/"+name]=name+".rgba";}
    }
    const std::map<std::string,std::string> aliases={{"minecraft","title/minecraft"},{"mojang","title/mojang"},{"crafting","container/crafting_table"},{"chest","container/generic_54"},{"inventory","container/inventory"},{"brewing","container/brewing_stand"},{"enchanting","container/enchanting_table"},{"furnace","container/furnace"},{"dispenser","container/dispenser"},{"hopper","container/hopper"},{"anvil","container/anvil"},{"beacon","container/beacon"},{"horse","container/horse"},{"villager","container/villager"}};
    for(const auto& alias:aliases)if(gui.count(alias.second)){copyBinary(stage/"runtime/gui"/fs::u8path(gui[alias.second]),stage/"runtime/gui"/(alias.first+".rgba"));gui[alias.first]=alias.first+".rgba";}
    for(int i=0;i<6;++i){const auto name="panorama_"+std::to_string(i),source="title/background/"+name;if(gui.count(source)){copyBinary(stage/"runtime/gui"/fs::u8path(gui[source]),stage/"runtime/gui"/(name+".rgba"));gui[name]=name+".rgba";}}
    std::map<std::string,std::string> extras={{"sun","environment/sun"},{"moon_phases","environment/moon_phases"},{"end_sky","environment/end_sky"},{"clouds","environment/clouds"},{"resource_packs","gui/resource_packs"},{"unknown_pack","misc/unknown_pack"},{"particles","particle/particles"},{"glint","misc/enchanted_item_glint"},{"unknown_server","misc/unknown_server"},{"explosion","entity/explosion"},{"footprint","particle/footprint"}};for(int i=0;i<10;++i)extras["destroy_stage_"+std::to_string(i)]="blocks/destroy_stage_"+std::to_string(i);
    for(const auto& entry:extras){const auto name="assets/minecraft/textures/"+entry.second+".png";if(!sources.count(name))continue;const auto image=decodePng(resource(sources,name));++report.decodedImages;writeFile(stage/"runtime/gui"/(entry.first+".rgba"),nvtx(image));gui[entry.first]=entry.first+".rgba";if(starts(entry.first,"destroy_stage_"))writeFile(stage/"runtime/tiles"/(entry.first+".rgba"),nvtx(image));}
    if(sources.count("assets/minecraft/textures/misc/enchanted_item_glint.png")){copyBinary(stage/"runtime/gui/glint.rgba",stage/"runtime/equipment/enchanted_item_glint.rgba");equipment["minecraft:misc/enchanted_item_glint"]="enchanted_item_glint.rgba";}
    const auto font=decodePng(resource(sources,"assets/minecraft/textures/font/ascii.png"));++report.decodedImages;writeFile(stage/"font.rgba",font.pixels);writeFile(stage/"runtime/font/ascii.rgba",nvtx(font));writeFile(stage/"runtime/font/ascii-widths.bin",fontWidths(font));auto cache=readFile(stage/"assets.bin",256u*1024*1024);put32(cache,16,font.width);put32(cache,20,font.height);writeFile(stage/"assets.bin",cache);
    const auto sizes=resource(sources,"assets/minecraft/font/glyph_sizes.bin");if(sizes.size()!=65536)fail("glyph_sizes.bin must have exactly 65,536 bytes");writeFile(stage/"runtime/font/glyph_sizes.bin",sizes);
    const auto galactic=sources.find("assets/minecraft/textures/font/ascii_sga.png");if(galactic!=sources.end()){const auto image=decodePng(galactic->second.read());++report.decodedImages;writeFile(stage/"runtime/gui/enchant_font.rgba",nvtx(image));writeFile(stage/"runtime/font/sga-widths.bin",fontWidths(image));gui["enchant_font"]="enchant_font.rgba";}
    for(const char* name:{"steve","alex"}){
        const auto found=sources.find(std::string("assets/minecraft/textures/entity/")+name+".png");if(found==sources.end())continue;
        const auto image=decodePng(found->second.read());SkinImage normalized;std::string why;
        if(!SkinCache::normalize(image.width,image.height,image.pixels,normalized,why))fail("Invalid default player skin: "+why);
        ResourceImage skin;skin.width=64;skin.height=64;skin.pixels=std::move(normalized.pixels);
        writeFile(stage/"runtime/entities/minecraft"/(std::string(name)+".rgba"),nvtx(skin));
        if(std::string(name)=="steve")writeFile(stage/"runtime/steve.rgba",nvtx(skin));
    }
    auto patterns=readFile(stage/"runtime/tiles/patterns.bin");if(patterns.size()<76||std::memcmp(patterns.data(),"NVTILE01",8))fail("Invalid banner pattern cache");std::size_t at=76;for(unsigned i=0;i<u32(patterns,8);++i){binaryString(patterns,at);const auto texture=binaryString(patterns,at);const auto encoded=resource(sources,"assets/minecraft/textures/entity/"+texture+".png");if(encoded.size()<26)fail("Truncated banner pattern PNG");put32(patterns,at,encoded[24]==8&&encoded[25]==6?1:0);at+=4;}writeFile(stage/"runtime/tiles/patterns.bin",patterns);
    writeIndex(stage/"runtime/gui/index.txt",gui);writeIndex(stage/"runtime/font/index.txt",fonts);writeIndex(stage/"runtime/entities/index.txt",entities);writeIndex(stage/"runtime/equipment/index.txt",equipment);
}
std::map<std::string,Rect> itemRects(const Json& metadata,unsigned width,unsigned height){std::map<std::string,Rect> result;for(const auto& entry:metadata.object){if(entry.second.array.size()!=4)fail("Invalid item atlas region");const auto& v=entry.second.array;Rect r;r.u0=v[0].number;r.v0=v[1].number;r.u1=v[2].number;r.v1=v[3].number;r.x=unsigned(std::llround(r.u0*width-.01));r.y=unsigned(std::llround(r.v0*height-.01));r.width=unsigned(std::llround((r.u1-r.u0)*width+.02));r.height=unsigned(std::llround((r.v1-r.v0)*height+.02));result.emplace(entry.first,r);}return result;}
void itemAtlas(const Sources& sources,const fs::path& stage,PreparedResourcePacks& report,const HeldAssets& held){
    auto cache=readFile(stage/"runtime/items/icons.bin",64u*1024*1024);if(cache.size()<32||std::memcmp(cache.data(),"NVITEM01",8))fail("Invalid item cache");ResourceImage previous;previous.width=u32(cache,8);previous.height=u32(cache,12);previous.pixels=readFile(stage/"runtime/items/atlas.rgba",256u*1024*1024);if(previous.pixels.size()!=std::size_t(previous.width)*previous.height*4)fail("Invalid item atlas pixels");const auto old=itemRects(json(readFile(stage/"runtime/items/regions.json")),previous.width,previous.height);std::map<std::string,Sprite> sprites;
    struct Icon{std::string name;unsigned flags{};int frame=-1;std::vector<std::pair<std::string,int>> layers;std::array<unsigned,5> colors{0xffffff,0xffffff,0xffffff,0xffffff,0xffffff};};std::vector<Icon> icons;std::size_t at=32;const unsigned count=u32(cache,16);
    for(unsigned i=0;i<count;++i){Icon icon;icon.name=binaryString(cache,at);if(at+4>cache.size())fail("Truncated item icon");icon.flags=cache[at];const unsigned layers=cache[at+1];at+=4;for(unsigned j=0;j<layers;++j,at+=24){const auto& key=rectangleFor(old,f32(cache,at),f32(cache,at+4),f32(cache,at+8),f32(cache,at+12));const auto frameAt=key.find('@');if(frameAt!=std::string::npos)icon.frame=std::stoi(key.substr(frameAt+1));}icons.push_back(std::move(icon));}
    const Bytes tail(cache.begin()+at,cache.end());std::vector<bool> colored(count,false);for(unsigned entry=0;entry<u32(cache,20);++entry){const auto index=u32(cache,at+4);if(index>=count)fail("Invalid item color entry");if(!colored[index]){for(unsigned c=0;c<5;++c)icons[index].colors[c]=u32(cache,at+8+c*4);colored[index]=true;}at+=28;}
    Sprite missing;const auto& missingRect=old.at("missing");missing.image=crop(previous,missingRect.x,missingRect.y,missingRect.width,missingRect.height);sprites["missing"]=std::move(missing);NativeModels resolver(sources);
    for(unsigned i=0;i<count;++i){auto& icon=icons[i];if(!i){icon.layers={{"missing",-1}};continue;}const auto& model=resolver.resolve(canonicalResource(icon.name,"item/"));const auto builtin=model["builtin"].text();const bool flat=builtin=="builtin/generated"||builtin=="builtin/clock"||builtin=="builtin/compass";
        if(flat){for(int layer=0;layer<5;++layer){const auto name="layer"+std::to_string(layer);if(!model["textures"].has(name))break;const auto texture=NativeModels::texture(model,'#'+name),key=texture+(icon.frame<0?std::string():"@"+std::to_string(icon.frame));if(!sprites.count(key)){sprites[key]=sprite(sources,texture,icon.frame);++report.decodedImages;}icon.layers.emplace_back(key,layer);}icon.flags=icon.layers.empty()?0:1;if((builtin=="builtin/clock"||builtin=="builtin/compass")&&icon.frame<0)icon.flags|=2;}
        else{const auto found=held.lookup.find(icon.name);if(found==held.lookup.end())fail("Missing native item geometry: "+icon.name);std::string key="model:"+icon.name+':';for(unsigned color:icon.colors)key+=std::to_string(color)+',';if(!sprites.count(key)){Sprite image;image.image=rasterItem(held.models[found->second],held.atlas,icon.colors);sprites.emplace(key,std::move(image));}icon.layers.emplace_back(key,-1);icon.flags=1|(builtin=="builtin/entity"?2:0);}
    }
    ResourceImage atlas;const auto rectangles=packSprites(sprites,atlas);Bytes output(cache.begin(),cache.begin()+32);put32(output,8,atlas.width);put32(output,12,atlas.height);
    for(const auto& icon:icons){addString(output,icon.name);output.push_back(std::uint8_t(icon.flags));output.push_back(std::uint8_t(icon.layers.size()));output.insert(output.end(),2,0);for(const auto& layer:icon.layers){const auto& r=rectangles.at(layer.first);for(double value:{r.u0,r.v0,r.u1,r.v1})addFloat(output,value);add32(output,0xffffffffu);add32(output,unsigned(layer.second));}}output.insert(output.end(),tail.begin(),tail.end());
    writeFile(stage/"runtime/items/icons.bin",output);writeFile(stage/"runtime/items/atlas.rgba",atlas.pixels);writeFile(stage/"runtime/gui/items.rgba",nvtx(atlas));Json regions;regions.type=Json::Type::Object;for(const auto& entry:rectangles){Json v;v.type=Json::Type::Array;for(double n:{entry.second.u0,entry.second.v0,entry.second.u1,entry.second.v1})v.array.push_back(number(n));regions.object[entry.first]=v;}writeText(stage/"runtime/items/regions.json",jsonText(regions));
}
void baseCache(const fs::path& base,const fs::path& stage){
    for(fs::recursive_directory_iterator it(base),end;it!=end;++it){const auto relative=fs::relative(it->path(),base).generic_u8string();if(relative=="vanilla"||relative=="resources"||relative=="resourcepacks"){it.disable_recursion_pending();continue;}if(reparse(it->path()))fail("Base asset cache contains a symbolic link/junction");if(it->is_directory())continue;if(!it->is_regular_file())fail("Non-regular file in base asset cache");safeResourcePath(relative);copyBinary(it->path(),stage/fs::u8path(relative));}
}
void soundLayers(const fs::path& base,const std::vector<Pack>& packs,const Sources& resources,const fs::path& stage){
    std::string index;unsigned layer=0;const auto old=readFile(base/"runtime/audio/sounds-layers.txt");std::istringstream input(std::string(old.begin(),old.end()));std::string line;
    auto append=[&](const std::string& ns,const Bytes& bytes){safeResourcePath("assets/"+ns+"/sounds.json");if(json(bytes).type!=Json::Type::Object)fail("sounds.json must be an object");const auto path="runtime/audio/layers/"+std::to_string(layer++)+".json";writeFile(stage/fs::u8path(path),bytes);index+=ns+'\t'+path+'\n';};
    while(std::getline(input,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;const auto tab=line.find('\t');if(tab==std::string::npos)fail("Invalid base sound-layer index");append(line.substr(0,tab),readFile(base/fs::u8path(safeResourcePath(line.substr(tab+1)))));}
    for(const auto& pack:packs)for(const auto& entry:pack.files){const auto& name=entry.first;if(starts(name,"assets/")&&ends(name,"/sounds.json")&&std::count(name.begin(),name.end(),'/')==2)append(name.substr(7,name.size()-7-12),entry.second.read());}
    writeText(stage/"runtime/audio/sounds-layers.txt",index);std::string sounds;for(const auto& entry:resources){const auto& path=entry.first;if(!starts(path,"assets/")||!ends(path,".ogg"))continue;const auto split=path.find('/',7);if(split!=std::string::npos&&starts(path.substr(split+1),"sounds/"))sounds+=path.substr(7,split-7)+':'+path.substr(split+1)+'\n';}writeText(stage/"runtime/audio/ogg-files.txt",sounds);
}
void customSky(const Sources& resources,const fs::path& stage,PreparedResourcePacks& report){
    std::string index;
    const std::string prefix="assets/minecraft/mcpatcher/sky/world0/";
    unsigned layer=0;
    for(const auto& entry:resources){
        if(!starts(entry.first,prefix)||!ends(entry.first,".properties")||layer>=4)continue;
        std::map<std::string,std::string> properties;const auto bytes=entry.second.read();std::istringstream lines(std::string(bytes.begin(),bytes.end()));std::string line;
        while(std::getline(lines,line)){const auto comment=line.find('#');if(comment!=std::string::npos)line.resize(comment);const auto equal=line.find('=');if(equal==std::string::npos)continue;auto trim=[](std::string value){const auto first=value.find_first_not_of(" \t\r");if(first==std::string::npos)return std::string();const auto last=value.find_last_not_of(" \t\r");return value.substr(first,last-first+1);};properties[trim(line.substr(0,equal))]=trim(line.substr(equal+1));}
        auto source=properties["source"];if(starts(source,"./"))source.erase(0,2);if(source.empty()||source.find('/')!=std::string::npos||source.find('\\')!=std::string::npos||!ends(source,".png"))continue;
        const auto texture=resources.find(prefix+source);if(texture==resources.end())continue;
        auto time=[](const std::string& value,int fallback){int hour=-1,minute=-1;char colon=0;std::istringstream input(value);if(!(input>>hour>>colon>>minute)||colon!=':'||hour<0||hour>23||minute<0||minute>59)return fallback;return ((hour*60+minute-360+1440)%1440)*24000/1440;};
        const int in0=time(properties["startFadeIn"],0),in1=time(properties["endFadeIn"],in0),out1=time(properties["endFadeOut"],in0);
        const int out0=time(properties["startFadeOut"],out1);
        auto image=decodePng(texture->second.read());if(image.width%3||image.height%2||image.width/3!=image.height/2)continue;
        if(image.width/3>1024){ResourceImage reduced;reduced.width=3072;reduced.height=2048;reduced.pixels.resize(std::size_t(reduced.width)*reduced.height*4);for(unsigned y=0;y<reduced.height;++y)for(unsigned x=0;x<reduced.width;++x){const unsigned sx=std::uint64_t(x)*image.width/reduced.width,sy=std::uint64_t(y)*image.height/reduced.height;std::copy_n(image.pixels.data()+(std::size_t(sy)*image.width+sx)*4,4,reduced.pixels.data()+(std::size_t(y)*reduced.width+x)*4);}image=std::move(reduced);}
        const auto name="sky_"+std::to_string(layer++);writeFile(stage/"runtime/sky"/(name+".rgba"),nvtx(image));
        index+=name+'\t'+std::to_string(in0)+'\t'+std::to_string(in1)+'\t'+std::to_string(out0)+'\t'+std::to_string(out1)+'\n';++report.decodedImages;
    }
    writeText(stage/"runtime/sky/index.txt",index);
}
}
bool decodeResourcePng(const std::vector<std::uint8_t>& bytes,ResourceImage& image,std::string& error){
    try{auto decoded=decodePng(bytes);image=std::move(decoded);error.clear();return true;}catch(const std::exception& e){error=e.what();return false;}
}
bool readResourceImage(const std::string& directory,const std::string& resourcePath,ResourceImage& image,std::string& error){
    try{const auto path=fs::u8path(directory)/"resources"/fs::u8path(safeResourcePath(resourcePath));auto decoded=decodePng(readFile(path));image=std::move(decoded);error.clear();return true;}catch(const std::exception& e){error=e.what();return false;}
}
bool inspectResourcePack(const std::string& path,ResourcePackInfo& info,std::string& error){try{auto pack=openPack(path);const auto icon=pack.files.find("pack.png");if(icon!=pack.files.end()){try{auto image=decodePng(icon->second.read(),true);if(image.width<=2048&&image.height<=2048){pack.info.iconWidth=image.width;pack.info.iconHeight=image.height;pack.info.iconPixels=std::move(image.pixels);}}catch(const std::exception&) {}}info=std::move(pack.info);error.clear();return true;}catch(const std::exception& e){error=e.what();return false;}}
bool prepareResourcePacks(const std::string& baseDirectory,const std::vector<std::string>& paths,const std::string& cacheDirectory,PreparedResourcePacks& prepared,std::string& error){
    fs::path staging,cache;bool owned=false;
    try{
        const auto base=fs::weakly_canonical(fs::absolute(fs::u8path(baseDirectory)));Assets validation;std::string problem;if(!validation.load(base.u8string(),problem))fail(problem);PreparedResourcePacks next;
        if(paths.empty()){next.assetDirectory=base.u8string();prepared=std::move(next);error.clear();return true;}
        cache=fs::weakly_canonical(fs::absolute(fs::u8path(cacheDirectory)));if(within(cache,base))fail("Resource cache must be outside the original asset directory");std::vector<Pack> packs;Sources effective=folderFiles(base/"resources");std::map<std::string,std::string> folded;for(const auto& entry:effective)folded[lower(entry.first)]=entry.first;
        for(const auto& path:paths){auto pack=openPack(path);if(pack.info.directory&&within(cache,fs::u8path(pack.info.path)))fail("Resource cache must be outside resource pack folders");if(pack.info.format!=1)next.warnings.push_back("Pack "+pack.info.name+" declares format "+std::to_string(pack.info.format)+"; only Minecraft 1.8.9 resource conventions are supported.");
            for(const auto& entry:pack.files)if(starts(entry.first,"assets/")){
                const auto key=lower(entry.first);const auto old=folded.find(key);if(old!=folded.end()&&old->second!=entry.first)fail("Resource paths differ only by case: "+entry.first);folded[key]=entry.first;
                // FallbackResourceManager only finds metadata in the winning
                // resource pack or a higher-priority pack, never below it.
                if(!ends(entry.first,".mcmeta"))effective.erase(entry.first+".mcmeta");effective[entry.first]=entry.second;++next.replacedFiles;
            }
            next.packs.push_back(pack.info);packs.push_back(std::move(pack));
        }
        const auto key=selectionKey(base,packs);
        if(fs::exists(cache))for(const auto& entry:fs::directory_iterator(cache)){
            if(!entry.is_directory()||reparse(entry.path())||!starts(entry.path().filename().u8string(),"selection_"))continue;
            const auto marker=entry.path()/"selection.key";if(!fs::is_regular_file(marker)||!fs::is_regular_file(entry.path()/"resource-packs.json"))continue;
            const auto saved=readFile(marker,64);if(std::string(saved.begin(),saved.end())!=key)continue;
            if(!fs::is_regular_file(entry.path()/"assets.bin")||!fs::is_regular_file(entry.path()/"runtime/equipment/index.txt"))continue;
            next.assetDirectory=entry.path().u8string();prepared=std::move(next);error.clear();return true;
        }
        fs::create_directories(cache);static std::atomic<std::uint64_t> serial{};const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();const auto name=std::to_string(stamp)+"_"+std::to_string(++serial);staging=cache/(".staging_"+name);const auto published=cache/("selection_"+name);
        if(!fs::create_directory(staging))fail("Cannot create an exclusive resource staging directory");owned=true;baseCache(base,staging);
        for(const auto& entry:effective)stageResource(entry.second,staging/"resources"/fs::u8path(entry.first));effective=folderFiles(staging/"resources");
        terrainAtlas(effective,staging,next);runtimeImages(effective,staging,next);customSky(effective,staging,next);const auto held=firstPersonAssets(effective,staging,next);itemAtlas(effective,staging,next,held);soundLayers(base,packs,effective,staging);
        Assets assets;UnicodeFont font;ItemIconAtlas items;AnimationAtlasCache animations;if(!assets.load(staging.u8string(),problem)||!font.load(staging.u8string(),problem)||!items.load(staging.u8string(),problem)||!animations.load(staging.u8string(),problem))fail("Prepared resource cache is invalid: "+problem);
        Json report;report.type=Json::Type::Object;report.object["native_runtime"]=number(1);report.object["replaced_files"]=number(next.replacedFiles);report.object["decoded_images"]=number(next.decodedImages);report.object["rebaked_models"]=number(next.rebakedModels);Json warnings;warnings.type=Json::Type::Array;for(const auto& warning:next.warnings)warnings.array.push_back(text(warning));report.object["warnings"]=std::move(warnings);writeText(staging/"resource-packs.json",jsonText(report));
        writeText(staging/"selection.key",key);fs::rename(staging,published);owned=false;next.assetDirectory=published.u8string();prepared=std::move(next);error.clear();return true;
    }catch(const std::exception& e){
        if(owned){std::error_code ignored;const auto actual=fs::weakly_canonical(staging,ignored);if(!ignored&&actual.parent_path()==cache&&starts(actual.filename().u8string(),".staging_")&&!reparse(actual))fs::remove_all(actual,ignored);}
        error=e.what();return false;
    }
}
}
