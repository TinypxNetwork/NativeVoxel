#pragma once
#include <fstream>
#include <string>
#include <unordered_map>
namespace mc {
class I18n {
public:
    bool load(const std::string& assets,const std::string& language){
        strings_.clear();read(assets+"/resources/assets/minecraft/lang/en_US.lang");
        language_=language;bool ok=read(assets+"/resources/assets/minecraft/lang/"+language+".lang");
        std::size_t count=0,wide=0;for(const auto& entry:strings_)for(unsigned char c:entry.second)if((c&0xc0)!=0x80){++count;if(c>=0xc4)++wide;}
        unicode_=count&&wide>count/10;return ok;
    }
    std::string operator()(const std::string& key) const {auto i=strings_.find(key);return i==strings_.end()?key:i->second;}
    const std::string& language() const {return language_;}
    bool unicode()const{return unicode_;}
private:
    std::unordered_map<std::string,std::string>strings_;std::string language_="zh_CN";bool unicode_=false;
    bool read(const std::string& path){std::ifstream f(path);if(!f)return false;std::string line;while(std::getline(f,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty()||line[0]=='#')continue;auto p=line.find('=');if(p!=std::string::npos)strings_[line.substr(0,p)]=line.substr(p+1);}return true;}
};
}
