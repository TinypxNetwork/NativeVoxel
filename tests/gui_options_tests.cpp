#include "mc/gui_options.hpp"
#include "mc/settings.hpp"
#include "mc/platform.hpp"
#include "mc/renderer.hpp"
#include "mc/i18n.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
unsigned checks=0,drawCalls=0;bool boundedDirt=true;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
void close(float a,float b,const char* message){require(std::abs(a-b)<1e-6f,message);}
const mc::OptionsWidget& widget(const mc::OptionsLayout& view,mc::OptionsAction action,int index=-1) {
    for(const auto& entry:view.widgets)if(entry.action==action&&entry.index==index)return entry;
    throw std::runtime_error("Requested option widget is absent");
}
int keyIndex(const mc::Settings& settings,const char* name){for(std::size_t i=0;i<settings.keys.size();++i)if(settings.keys[i].name==name)return int(i);throw std::runtime_error("Missing test key");}
mc::OptionsInput point(const mc::OptionsWidget& button,bool press=true){mc::OptionsInput input;input.x=float(button.x+button.width/2);input.y=float(button.y+button.height/2);input.leftPressed=input.leftDown=press;if(press)input.pressedKeys={VK_LBUTTON};return input;}
}
// Drawing and UTF-8 helpers only; Settings itself is the real implementation.
namespace mc {
struct Renderer::Impl {};
Renderer::Renderer()=default;Renderer::~Renderer()=default;Window::~Window()=default;
void Renderer::rect(float,float,float,float,std::uint32_t){++drawCalls;}
void Renderer::gradient(float,float,float,float,std::uint32_t,std::uint32_t){++drawCalls;}
void Renderer::text(const std::string&,float,float,float,std::uint32_t,bool){++drawCalls;}
float Renderer::textWidth(const std::string& text,float scale)const{return float(text.size())*6*scale;}
void Renderer::sprite(const std::string& name,float,float,float,float,float sx,float sy,float sw,float sh,std::uint32_t){++drawCalls;if(name=="options_background")boundedDirt=boundedDirt&&sx>=0&&sy>=0&&sx+sw<=16&&sy+sh<=16;}
std::string utf8(const std::wstring& value){if(value.empty())return {};int size=WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);std::string result(std::size_t(size),'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),&result[0],size,nullptr,nullptr);return result;}
}

int main(int argc,char** argv) {
    try {
        const std::string root=argc>1?argv[1]:".";mc::Settings settings;mc::OptionsScreen screen;mc::I18n english,chinese;
        require(english.load(root+"/assets","en_US")&&chinese.load(root+"/assets","zh_CN"),"Load original option translations");
        using Page=mc::OptionsPage;using Action=mc::OptionsAction;
        const int attack=keyIndex(settings,"attack"),pick=keyIndex(settings,"pickItem");
        auto view=screen.layout(Page::Controls,settings,320,240,&english);
        const auto& invert=widget(view,Action::InvertMouse);const auto& sensitivity=widget(view,Action::Sensitivity);const auto& done=widget(view,Action::Done);
        require(invert.x==5&&invert.y==18&&invert.width==150,"GuiControls invert bounds");
        require(sensitivity.x==165&&sensitivity.y==18&&sensitivity.width==150&&sensitivity.slider,"GuiControls sensitivity bounds");
        require(done.x==5&&done.y==211&&done.width==150,"GuiControls Done bounds");
        require(widget(view,Action::ResetAll).x==165&&!widget(view,Action::ResetAll).enabled,"Reset All disabled with defaults");
        require(widget(view,Action::Touchscreen).y==42&&!widget(view,Action::Touchscreen).enabled,"Unsupported touchscreen stays disabled");
        require(view.listTop==63&&view.listBottom==208&&view.scrollbarX==299,"Original control-list viewport");
        require(view.rows.front().keyIndex==-1&&view.rows.front().label=="Gameplay"&&view.rows.front().y==67,"Translated category ordering and first row");
        auto binding=widget(view,Action::BindKey,attack);
        require(binding.x==141&&binding.y==87&&binding.width==75,"Key binding original row coordinates");
        require(widget(view,Action::ResetKey,attack).x==226&&widget(view,Action::ResetKey,attack).width==50,"Key reset original row coordinates");
        require(binding.text=="Button 1","Original mouse-button translation");
        auto chineseView=screen.layout(Page::Controls,settings,640,360,&chinese);require(widget(chineseView,Action::BindKey,attack).text==u8"鼠标按键1","Chinese mouse-button translation");
        require(binding.hit(float(binding.x),float(binding.y))&&!binding.hit(float(binding.x+binding.width),float(binding.y))&&!binding.hit(float(binding.x),float(binding.y+20)),"Button half-open hit bounds");
        require(!screen.interact(Page::Controls,settings,point(binding),&english)&&screen.capturingKey()&&settings.key("attack")==VK_LBUTTON,"Opening bind click is consumed without binding itself");
        auto captured=screen.layout(Page::Controls,settings,320,240,&english);require(widget(captured,Action::BindKey,attack).text.find("> ")!=std::string::npos,"Captured binding is visibly marked");
        mc::OptionsInput input;input.pressedKeys={'J','K'};require(!screen.interact(Page::Controls,settings,input,&english)&&!screen.capturingKey()&&settings.key("attack")=='J',"First pressed key captured once");
        auto changed=screen.layout(Page::Controls,settings,320,240,&english);require(widget(changed,Action::ResetAll).enabled&&widget(changed,Action::ResetKey,attack).enabled,"Changed binding enables resets");
        settings.bind("pickItem",'J');require(mc::OptionsScreen::conflict(settings,attack)&&mc::OptionsScreen::conflict(settings,pick),"Duplicate nonzero keys conflict");
        changed=screen.layout(Page::Controls,settings,320,240,&english);require(widget(changed,Action::BindKey,attack).text.find(u8"\u00a7c")==0,"Conflicting binding is red");
        require(!screen.interact(Page::Controls,settings,point(widget(changed,Action::ResetKey,attack)),&english)&&settings.key("attack")==VK_LBUTTON&&!screen.capturingKey(),"Per-key reset applies default");
        screen.interact(Page::Controls,settings,point(binding),&english);input={};input.pressedKeys={VK_ESCAPE};
        require(!screen.interact(Page::Controls,settings,input,&english)&&settings.key("attack")==0&&!screen.capturingKey(),"Escape clears binding instead of closing controls");
        settings.bind("pickItem",0);require(!mc::OptionsScreen::conflict(settings,attack)&&!mc::OptionsScreen::conflict(settings,pick),"Multiple unbound keys are not conflicts");
        require(screen.interact(Page::Controls,settings,input,&english),"Uncaptured Escape closes screen");
        screen.interact(Page::Controls,settings,point(binding),&english);input={};input.pressedKeys={256,-1,0};screen.interact(Page::Controls,settings,input,&english);require(screen.capturingKey(),"Unsupported key codes do not erase pending capture");
        input.pressedKeys={VK_XBUTTON2};screen.interact(Page::Controls,settings,input,&english);require(settings.key("attack")==VK_XBUTTON2&&!screen.capturingKey(),"Mouse button 5 capture uses Win32 code");
        screen.interact(Page::Controls,settings,point(binding),&english);input.pressedKeys={VK_RSHIFT};screen.interact(Page::Controls,settings,input,&english);require(settings.key("attack")==VK_RSHIFT,"Right modifier retains its physical key identity");
        screen.interact(Page::Controls,settings,point(binding),&english);input={};input.focused=false;screen.interact(Page::Controls,settings,input,&english);require(!screen.capturingKey()&&settings.key("attack")==VK_RSHIFT,"Focus loss cancels capture without changing binding");
        screen.setBindingEnabled("attack",false);screen.interact(Page::Controls,settings,point(binding),&english);require(!screen.capturingKey()&&!widget(screen.layout(Page::Controls,settings,320,240,&english),Action::BindKey,attack).enabled,"Unimplemented bindings can be disabled by caller");
        input={};input.leftReleased=true;screen.interact(Page::Controls,settings,input,&english);
        screen.setBindingEnabled("attack",true);screen.interact(Page::Controls,settings,point(binding),&english);require(screen.capturingKey(),"Re-enabled binding accepts capture");
        require(!screen.interact(Page::Controls,settings,point(done),&english)&&settings.key("attack")==VK_LBUTTON&&!screen.capturingKey(),"Clicking Done while capturing binds the mouse instead of closing");
        require(screen.interact(Page::Controls,settings,point(done),&english),"A subsequent Done click closes");
        auto before=settings.keys;auto touch=widget(view,Action::Touchscreen);screen.interact(Page::Controls,settings,point(touch),&english);require(settings.keys.size()==before.size()&&!screen.capturingKey(),"Disabled touchscreen has no input effect");
        input=point(sensitivity);input.x=float(sensitivity.x+4+(sensitivity.width-8)/4);screen.interact(Page::Controls,settings,input,&english);close(settings.mouseSensitivity,.25f-0.5f/142.f,"Sensitivity click uses original four-pixel inset");
        input.leftPressed=false;input.pressedKeys.clear();input.x=-100;screen.interact(Page::Controls,settings,input,&english);close(settings.mouseSensitivity,0,"Sensitivity clamps below track");
        input.x=1000;screen.interact(Page::Controls,settings,input,&english);close(settings.mouseSensitivity,1,"Sensitivity clamps above track");
        input=point(invert,false);input.leftReleased=true;screen.interact(Page::Controls,settings,input,&english);require(!settings.invertMouse,"Slider release does not click underlying toggle");
        screen.interact(Page::Controls,settings,point(invert),&english);require(settings.invertMouse,"Invert toggle updates settings");
        settings.mouseSensitivity=.42f;settings.fov=91;settings.soundLevels[1]=.37f;
        view=screen.layout(Page::Controls,settings,320,240,&english);screen.interact(Page::Controls,settings,point(widget(view,Action::ResetAll)),&english);
        for(const auto& key:settings.keys)require(key.key==key.defaultKey,"Reset All restores each key only");
        close(settings.mouseSensitivity,.42f,"Key reset preserves sensitivity");close(settings.fov,91,"Key reset preserves FOV");close(settings.soundLevels[1],.37f,"Key reset preserves sound");require(settings.invertMouse,"Key reset preserves invert toggle");
        input={};input.x=20;input.y=100;input.wheel=-1;screen.interact(Page::Controls,settings,input,&english);close(screen.scroll(),10,"Wheel moves original half-row increment");
        input.y=30;screen.interact(Page::Controls,settings,input,&english);close(screen.scroll(),10,"Wheel outside list does not move it");
        input.y=100;input.wheel=-1000;screen.interact(Page::Controls,settings,input,&english);view=screen.layout(Page::Controls,settings,320,240,&english);close(screen.scroll(),view.maxScroll,"Wheel clamps at list bottom");
        input.width=320;input.height=900;input.wheel=0;screen.interact(Page::Controls,settings,input,&english);close(screen.scroll(),0,"Resize to fully visible list clamps scroll");
        screen.reset();input={};input.x=299;input.y=65;input.leftPressed=input.leftDown=true;screen.interact(Page::Controls,settings,input,&english);input.leftPressed=false;input.y=200;screen.interact(Page::Controls,settings,input,&english);require(screen.scroll()>0,"Scrollbar drag moves list");
        input.leftDown=false;input.leftReleased=true;screen.interact(Page::Controls,settings,input,&english);
        screen.reset();input={};input.x=20;input.y=140;input.leftPressed=input.leftDown=true;screen.interact(Page::Controls,settings,input,&english);input.leftPressed=false;input.y=110;screen.interact(Page::Controls,settings,input,&english);close(screen.scroll(),30,"Dragging list background scrolls by pointer distance");
        input.leftDown=false;input.leftReleased=true;screen.interact(Page::Controls,settings,input,&english);
        // Rows scrolled behind the top controls must not receive their clicks.
        view=screen.layout(Page::Controls,settings,320,240,&english);input={};input.x=150;input.y=60;input.leftPressed=input.leftDown=true;screen.interact(Page::Controls,settings,input,&english);require(!screen.capturingKey(),"Top mask prevents hidden row activation");
        screen.reset();settings.soundLevels.fill(1);auto sounds=screen.layout(Page::Sounds,settings,320,240,&english);
        require(sounds.widgets.size()==10,"Nine sound categories plus Done");const auto& master=widget(sounds,Action::SoundLevel,0);require(master.x==5&&master.y==28&&master.width==310,"Original master slider geometry");
        require(widget(sounds,Action::SoundLevel,1).x==5&&widget(sounds,Action::SoundLevel,1).y==52&&widget(sounds,Action::SoundLevel,2).x==165,"Original category slider columns");
        require(widget(sounds,Action::Done).x==60&&widget(sounds,Action::Done).y==208,"Original sound Done geometry");
        for(int category=0;category<9;++category){auto slider=widget(sounds,Action::SoundLevel,category);input=point(slider);input.x=float(slider.x+4);screen.interact(Page::Sounds,settings,input,&english);close(settings.soundLevels[std::size_t(category)],0,"Category slider reaches zero");
            auto display=screen.layout(Page::Sounds,settings,320,240,&english);require(widget(display,Action::SoundLevel,category).text.find("OFF")!=std::string::npos,"Zero sound category displays OFF");
            input.leftPressed=false;input.pressedKeys.clear();input.x=float(slider.x+slider.width-4);screen.interact(Page::Sounds,settings,input,&english);close(settings.soundLevels[std::size_t(category)],1,"Category slider reaches full scale");
            input.x=float(slider.x+4+(slider.width-8)/2);screen.interact(Page::Sounds,settings,input,&english);close(settings.soundLevels[std::size_t(category)],.5f,"Category slider midpoint");input.leftDown=false;input.leftReleased=true;screen.interact(Page::Sounds,settings,input,&english);}
        screen.reset();input=point(binding);screen.interact(Page::Controls,settings,input,&english);require(screen.capturingKey(),"Begin capture before page switch");input={};screen.interact(Page::Sounds,settings,input,&english);require(!screen.capturingKey()&&screen.scroll()==0,"Page change clears capture and scrolling");
        input={};input.pressedKeys={VK_ESCAPE};require(screen.interact(Page::Sounds,settings,input,&english),"Escape exits sound settings");
        screen.reset();screen.setWorldContext(false,false,2);view=screen.layout(Page::Main,settings,320,240,&english);
        require(view.widgets.size()==13,"Original Options page has FOV, Realms, ten entries and Done");
        const auto fovSlider=widget(view,Action::Fov);require(fovSlider.slider&&fovSlider.x==5&&fovSlider.y==28&&fovSlider.width==150,"FOV is the original draggable slider");
        require(widget(view,Action::OpenSkin).y==82&&widget(view,Action::OpenSounds).y==106&&widget(view,Action::OpenVideo).y==130&&widget(view,Action::OpenChat).y==154&&widget(view,Action::OpenSnooper).y==178,"Original Options entry rows");
        require(widget(view,Action::SuperSecret).text=="Super Secret Settings...","Original secret settings button text");
        input=point(fovSlider);input.x=float(fovSlider.x+4);screen.interact(Page::Main,settings,input,&english);close(settings.fov,30,"FOV drag reaches minimum");
        input.leftPressed=false;input.x=float(fovSlider.x+fovSlider.width-4);screen.interact(Page::Main,settings,input,&english);close(settings.fov,110,"FOV drag reaches maximum");
        input.x=float(fovSlider.x+fovSlider.width/2);screen.interact(Page::Main,settings,input,&english);close(settings.fov,70,"FOV slider midpoint is Normal");
        input.leftDown=false;input.leftReleased=true;screen.interact(Page::Main,settings,input,&english);require(screen.takePressSound(),"Slider releases request the original UI sound");
        screen.interact(Page::Main,settings,point(widget(view,Action::RealmsNotifications)),&english);require(!settings.realmsNotifications,"Realms notification preference toggles");
        screen.interact(Page::Main,settings,point(widget(view,Action::OpenSkin)),&english);require(screen.takeAction()==Action::OpenSkin,"Page button returns a navigation action");
        for(int i=0;i<25;++i)screen.interact(Page::Main,settings,point(widget(view,Action::SuperSecret)),&english);
        require(settings.secretShader==-1,"Twenty-four secret shaders cycle back to off");
        screen.setWorldContext(true,true,3);view=screen.layout(Page::Main,settings,320,240,&english);require(!widget(view,Action::Difficulty).enabled&&widget(view,Action::Difficulty).text.find("Hard")!=std::string::npos,"Multiplayer difficulty replaces Realms and stays server-controlled");
        screen.setWorldContext(true,false,2);require(widget(screen.layout(Page::Main,settings,320,240,&english),Action::Difficulty).enabled,"Local difficulty can be changed");
        screen.setWorldContext(false,false,2);screen.reset();
        for(int part=0;part<7;++part){view=screen.layout(Page::Skin,settings,320,240,&english);require(widget(view,Action::SkinPart,part).x==(part%2?165:5)&&widget(view,Action::SkinPart,part).y==40+(part/2)*24,"Skin model part source coordinates");screen.interact(Page::Skin,settings,point(widget(view,Action::SkinPart,part)),&english);}
        require(settings.skinParts==0,"Every skin model layer can be disabled independently");
        screen.interact(Page::Skin,settings,point(widget(view,Action::SkinPart,6)),&english);require(settings.skinParts==64,"Hat toggles without enabling other layers");
        view=screen.layout(Page::Chat,settings,320,240,&english);require(view.widgets.size()==11&&widget(view,Action::Done).y==160,"Ten chat settings and original Done row");
        for(int i:{0,1,2,4,9})screen.interact(Page::Chat,settings,point(widget(view,Action::ChatSetting,i)),&english);
        require(settings.chatVisibility==1&&!settings.chatColors&&!settings.chatLinks&&!settings.chatLinksPrompt&&settings.reducedDebugInfo,"Chat toggles update actual preferences");
        for(int i:{3,5,6,7,8}){const auto& slider=widget(view,Action::ChatSetting,i);require(slider.slider,"Original chat range settings are sliders");input=point(slider);input.x=float(slider.x+4);screen.interact(Page::Chat,settings,input,&english);input.leftPressed=input.leftDown=false;input.leftReleased=true;screen.interact(Page::Chat,settings,input,&english);}
        require(settings.chatOpacity==0&&settings.chatScale==0&&settings.chatHeightFocused==0&&settings.chatHeightUnfocused==0&&settings.chatWidth==0,"All chat slider values feed their Settings fields");
        view=screen.layout(Page::Snooper,settings,320,240,&english);require(widget(view,Action::Done).x==162&&widget(view,Action::SnooperEnabled).x==8,"Original snooper footer layout");
        screen.interact(Page::Snooper,settings,point(widget(view,Action::SnooperEnabled)),&english);require(!settings.snooperEnabled,"Snooper choice is preserved without a network dependency");
        view=screen.layout(Page::Stream,settings,320,240,&english);require(view.widgets.size()==12&&widget(view,Action::StreamSetting,8).y==160,"Original Twitch settings section positions");
        screen.interact(Page::Stream,settings,point(widget(view,Action::StreamSetting,3)),&english);require(!settings.streamSendMetadata,"Retired streaming settings still retain user choices");
        view=screen.layout(Page::Video,settings,320,240,&english);require(view.widgets.size()==17&&view.listTop==32&&view.listBottom==208,"Original video settings scrolling list");
        for(int i:{0,4,8,14,15})require(!widget(view,Action::VideoSetting,i).enabled,"Unavailable video paths are visibly disabled");
        for(int i:{1,3,7,12})require(widget(view,Action::VideoSetting,i).slider,"Supported original video range settings are sliders");
        screen.interact(Page::Video,settings,point(widget(view,Action::VideoSetting,2)),&english);require(settings.ambientOcclusion==0,"Smooth lighting toggle has an applied preference");
        screen.interact(Page::Video,settings,point(widget(view,Action::VideoSetting,13)),&english);require(!settings.blockAlternatives,"Alternate blocks preference is connected");
        auto mipmap=widget(view,Action::VideoSetting,12);input=point(mipmap);input.x=float(mipmap.x+4);screen.interact(Page::Video,settings,input,&english);require(settings.mipmapLevels==0,"Mipmap slider can disable mipmaps");input.leftPressed=false;input.x=float(mipmap.x+mipmap.width-4);screen.interact(Page::Video,settings,input,&english);require(settings.mipmapLevels==1,"Mipmap slider exposes only the supported atlas level");
        screen.reset();
        // Real Settings persistence after UI actions, preserving unrelated keys.
        const auto path=std::filesystem::absolute(std::filesystem::u8path(root+"/build/options_tests/options.txt"));std::filesystem::create_directories(path.parent_path());
        {std::ofstream out(path,std::ios::binary);out<<"unrelated.option:keep:all:colons\n";}
        std::string error;require(settings.load(path.u8string(),&error),"Load unrelated option");screen.reset();screen.interact(Page::Controls,settings,point(binding),&english);input={};input.pressedKeys={'K'};screen.interact(Page::Controls,settings,input,&english);
        require(settings.save(path.u8string(),&error),"Save changes made by UI");mc::Settings restored;require(restored.load(path.u8string(),&error),"Reload changes made by UI");
        require(restored.key("attack")=='K'&&restored.invertMouse==settings.invertMouse,"Key/invert changes survive Settings roundtrip");close(restored.mouseSensitivity,settings.mouseSensitivity,"Sensitivity survives roundtrip");
        require(restored.skinParts==64&&!restored.realmsNotifications&&!restored.snooperEnabled&&restored.chatVisibility==1&&!restored.chatColors&&!restored.chatLinks&&!restored.chatLinksPrompt&&restored.reducedDebugInfo,"Original skin/chat/obsolete-service option keys roundtrip");
        require(restored.chatScale==0&&restored.chatHeightFocused==0&&restored.chatHeightUnfocused==0&&restored.chatWidth==0&&restored.chatOpacity==0,"Chat ranges survive serialization");
        require(restored.ambientOcclusion==0&&!restored.blockAlternatives&&restored.mipmapLevels==1&&!restored.streamSendMetadata,"Video and streaming preferences survive serialization");
        for(unsigned i=0;i<9;++i)close(restored.soundLevels[i],settings.soundLevels[i],"Every category survives roundtrip");
        std::ifstream saved(path,std::ios::binary);std::string data((std::istreambuf_iterator<char>(saved)),std::istreambuf_iterator<char>());saved.close();require(data.find("unrelated.option:keep:all:colons")!=std::string::npos,"UI save preserves unrelated options");
        mc::Renderer renderer;mc::Window window;screen.reset();require(!screen.draw(Page::Controls,settings,renderer,window,chinese,2)&&drawCalls>20,"Controls draw submits original widgets without a graphics device");require(boundedDirt,"Dirt background UVs stay inside the clamped texture");drawCalls=0;
        require(!screen.draw(Page::Sounds,settings,renderer,window,chinese,2)&&drawCalls>=45,"Sound draw submits nine sliders without audio playback");
        screen.reset();screen.interact(Page::Controls,settings,point(binding),&english);window.pressedKeyOrder={'Z','A'};
        require(!screen.draw(Page::Controls,settings,renderer,window,english,2)&&settings.key("attack")=='Z'&&!screen.capturingKey(),"draw uses event order rather than ascending virtual-key order");
        for(int key:{VK_LSHIFT,VK_RSHIFT,VK_LCONTROL,VK_RCONTROL,VK_LMENU,VK_RMENU,VK_XBUTTON1,VK_XBUTTON2}) {
            screen.reset();screen.interact(Page::Controls,settings,point(binding),&english);window.pressedKeyOrder={key};
            require(!screen.draw(Page::Controls,settings,renderer,window,english,2)&&settings.key("attack")==key,"draw captures exact modifier or side mouse button");
            const bool savedOk=settings.save(path.u8string(),&error),loadedOk=savedOk&&restored.load(path.u8string(),&error);
            if(!savedOk||!loadedOk||restored.key("attack")!=key)throw std::runtime_error("Physical binding roundtrip: expected "+std::to_string(key)+", got "+std::to_string(restored.key("attack"))+", "+error);
            require(true,"Physical modifier and mouse binding survive Settings roundtrip");
        }
        window.pressedKeyOrder.clear();
        std::cout<<checks<<" option-screen checks passed: source positions, capture/reset/conflicts, scrolling, nine sliders, Settings persistence; no window or audio device\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Option-screen tests failed: "<<e.what()<<'\n';return 1;}
}
