#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mc::gui {

// GuiTextField's positions are represented by UTF-8 boundaries here. Its length
// limit is still measured in Java UTF-16 code units, including supplementary text.
class TextInput {
public:
    struct View {
        std::string text;
        std::size_t begin{},end{};
        float cursorX{},selectionStartX{},selectionEndX{};
        bool cursorAtEnd{};
    };
    explicit TextInput(int limit=100):limit_(std::max(0,limit)){}
    const std::string& text()const{return text_;}
    std::size_t cursor()const{return cursor_;}
    std::size_t anchor()const{return anchor_;}
    bool selected()const{return anchor_!=cursor_;}
    static std::size_t previous(const std::string& text,std::size_t index){
        index=std::min(index,text.size());if(!index)return 0;--index;
        while(index&&(static_cast<unsigned char>(text[index])&0xc0)==0x80)--index;return index;
    }
    static std::size_t next(const std::string& text,std::size_t index){
        index=std::min(index,text.size());if(index==text.size())return index;++index;
        while(index<text.size()&&(static_cast<unsigned char>(text[index])&0xc0)==0x80)++index;return index;
    }
    static int units(const std::string& text){int count=0;for(unsigned char c:text)if((c&0xc0)!=0x80)count+=c>=0xf0?2:1;return count;}
    static std::string utf8(char32_t c){
        std::string out;if(c<0x80)out+=char(c);
        else if(c<0x800){out+=char(0xc0|(c>>6));out+=char(0x80|(c&63));}
        else if(c<0x10000){out+=char(0xe0|(c>>12));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}
        else{out+=char(0xf0|(c>>18));out+=char(0x80|((c>>12)&63));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}return out;
    }
    void setText(const std::string& value){text_.clear();cursor_=anchor_=scroll_=0;insert(value);}
    void clear(){setText({});}
    void selectAll(){anchor_=0;cursor_=text_.size();}
    void select(std::size_t begin,std::size_t end){anchor_=boundary(begin);cursor_=boundary(end);}
    std::string selection()const{return text_.substr(std::min(cursor_,anchor_),std::max(cursor_,anchor_)-std::min(cursor_,anchor_));}
    void insert(const std::u32string& value){
        std::string bytes;for(auto cp:value)if(cp>=32&&cp!=127&&cp!=0xa7&&cp<=0x10ffff&&(cp<0xd800||cp>0xdfff))bytes+=utf8(cp);insert(bytes);
    }
    void insert(const std::string& value){
        const auto first=std::min(cursor_,anchor_),last=std::max(cursor_,anchor_);
        int available=limit_-units(text_)+units(text_.substr(first,last-first));std::string clean;
        for(std::size_t i=0;i<value.size();) {
            const auto j=next(value,i);const auto cp=value.substr(i,j-i);const auto lead=static_cast<unsigned char>(value[i]);
            const int cost=lead>=0xf0?2:1;
            if(lead>=32&&lead!=127&&cp!=u8"\u00a7") {if(cost>available)break;clean+=cp;available-=cost;}
            i=j;
        }
        text_.replace(first,last-first,clean);cursor_=anchor_=first+clean.size();scroll_=std::min(scroll_,text_.size());
    }
    std::size_t wordLeft(std::size_t index,bool skipSpaces=true)const{
        index=boundary(index);if(skipSpaces)while(index&&text_[previous(text_,index)]==' ')index=previous(text_,index);
        while(index&&text_[previous(text_,index)]!=' ')index=previous(text_,index);return index;
    }
    std::size_t wordRight(std::size_t index)const{
        index=boundary(index);while(index<text_.size()&&text_[index]!=' ')index=next(text_,index);
        while(index<text_.size()&&text_[index]==' ')index=next(text_,index);return index;
    }
    void move(int direction,bool control=false,bool shift=false){
        const auto destination=direction<0?(control?wordLeft(cursor_):previous(text_,cursor_)):(control?wordRight(cursor_):next(text_,cursor_));
        cursor_=destination;if(!shift)anchor_=cursor_;
    }
    void home(bool shift=false){cursor_=0;if(!shift)anchor_=cursor_;}
    void end(bool shift=false){cursor_=text_.size();if(!shift)anchor_=cursor_;}
    void erase(bool backwards,bool control=false){
        if(!selected())anchor_=backwards?(control?wordLeft(cursor_):previous(text_,cursor_)):(control?wordRight(cursor_):next(text_,cursor_));
        insert(std::string{});
    }
    View view(float maximum,const std::function<float(const std::string&)>& measure){
        maximum=std::max(1.f,maximum);scroll_=boundary(std::min(scroll_,cursor_));
        while(scroll_<cursor_&&measure(text_.substr(scroll_,cursor_-scroll_))>maximum-1)scroll_=next(text_,scroll_);
        while(scroll_>0){const auto prior=previous(text_,scroll_);if(measure(text_.substr(prior,cursor_-prior))>maximum-1)break;scroll_=prior;}
        std::size_t finish=scroll_;while(finish<text_.size()){const auto following=next(text_,finish);if(measure(text_.substr(scroll_,following-scroll_))>maximum)break;finish=following;}
        View result;result.begin=scroll_;result.end=finish;result.text=text_.substr(scroll_,finish-scroll_);
        result.cursorX=measure(text_.substr(scroll_,cursor_-scroll_));result.cursorAtEnd=cursor_==text_.size();
        const auto first=std::clamp(std::min(cursor_,anchor_),scroll_,finish),last=std::clamp(std::max(cursor_,anchor_),scroll_,finish);
        result.selectionStartX=measure(text_.substr(scroll_,first-scroll_));result.selectionEndX=measure(text_.substr(scroll_,last-scroll_));return result;
    }
    void click(const View& view,float x,const std::function<float(const std::string&)>& measure,bool shift=false){
        std::size_t position=view.begin;
        while(position<view.end){const auto following=next(text_,position);const auto before=measure(text_.substr(view.begin,position-view.begin)),after=measure(text_.substr(view.begin,following-view.begin));if(x<(before+after)*.5f)break;position=following;}
        cursor_=position;if(!shift)anchor_=cursor_;
    }
private:
    std::size_t boundary(std::size_t index)const{index=std::min(index,text_.size());while(index<text_.size()&&index&&(static_cast<unsigned char>(text_[index])&0xc0)==0x80)--index;return index;}
    int limit_;
    std::string text_;
    std::size_t cursor_{},anchor_{},scroll_{};
};

class ChatCompletion {
public:
    void cancel(){waiting_=false;candidates_.clear();hint_.clear();index_=0;expected_.clear();}
    bool waiting()const{return waiting_;}
    const std::string& hint()const{return hint_;}
    std::optional<std::string> tab(TextInput& input){
        if(!candidates_.empty()&&expected_==input.text()&&cursor_==input.cursor()){cycle(input);return std::nullopt;}
        cancel();if(!input.cursor())return std::nullopt;
        expected_=input.text();cursor_=input.cursor();wordStart_=input.wordLeft(cursor_,false);waiting_=true;
        return input.text().substr(0,cursor_);
    }
    bool response(TextInput& input,const std::vector<std::string>& values){
        if(!waiting_||expected_!=input.text()||cursor_!=input.cursor())return false;
        waiting_=false;candidates_.clear();
        for(const auto& value:values)if(!value.empty()&&candidates_.size()<65536)candidates_.push_back(value);
        if(candidates_.empty())return false;
        std::string common=candidates_.front();
        for(std::size_t i=1;i<candidates_.size()&&!common.empty();++i){std::size_t count=0;while(count<common.size()&&count<candidates_[i].size()&&common[count]==candidates_[i][count])++count;while(count<common.size()&&count&&(static_cast<unsigned char>(common[count])&0xc0)==0x80)--count;common.resize(count);}
        const auto current=input.text().substr(wordStart_,cursor_-wordStart_);
        if(!common.empty()&&!equalIgnoringAsciiCase(common,current)){
            input.select(wordStart_,cursor_);input.insert(common);candidates_.clear();expected_=input.text();cursor_=input.cursor();return true;
        }
        if(candidates_.size()>1)for(const auto& value:candidates_){if(!hint_.empty())hint_+=", ";hint_+=value;if(hint_.size()>4096){hint_.resize(4096);break;}}
        index_=0;cycle(input);return true;
    }
private:
    static bool equalIgnoringAsciiCase(const std::string& a,const std::string& b){
        if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i){auto fold=[](unsigned char c){return c>='A'&&c<='Z'?c+32:c;};if(fold(a[i])!=fold(b[i]))return false;}return true;
    }
    void cycle(TextInput& input){input.select(wordStart_,cursor_);input.insert(candidates_[index_++%candidates_.size()]);cursor_=input.cursor();expected_=input.text();}
    bool waiting_{};
    std::string expected_,hint_;
    std::size_t cursor_{},wordStart_{},index_{};
    std::vector<std::string> candidates_;
};

} // namespace mc::gui
