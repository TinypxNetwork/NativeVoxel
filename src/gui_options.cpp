#include "mc/gui_options.hpp"
#include "mc/settings.hpp"
#include "mc/renderer.hpp"
#include "mc/platform.hpp"
#include "mc/i18n.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <iomanip>
#include <sstream>

namespace mc {
namespace {
std::string translated(const I18n *tr, const std::string &key, const std::string &fallback) {
    if (!tr)
        return fallback;
    auto value = (*tr)(key);
    return value == key ? fallback : value;
}
std::string bindingLabel(int key, const I18n *tr) {
    int button = key == VK_LBUTTON    ? 1
                 : key == VK_RBUTTON  ? 2
                 : key == VK_MBUTTON  ? 3
                 : key == VK_XBUTTON1 ? 4
                 : key == VK_XBUTTON2 ? 5
                                      : 0;
    if (!button)
        return key == 0 ? "NONE" : Settings::keyName(key);
    auto label = translated(tr, "key.mouseButton", "Button %1$s");
    auto marker = label.find("%1$s");
    std::size_t length = 4;
    if (marker == std::string::npos) {
        marker = label.find("%s");
        length = 2;
    }
    if (marker != std::string::npos)
        label.replace(marker, length, std::to_string(button));
    return label;
}
std::string keyLabel(const KeyOption &key, const I18n *tr) { return translated(tr, "key." + key.name, key.name); }
std::string categoryLabel(const KeyOption &key, const I18n *tr) {
    return translated(tr, "key.categories." + key.category, key.category);
}
float unit(float value) { return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f; }
bool pressed(const OptionsInput &input, int key) {
    return std::find(input.pressedKeys.begin(), input.pressedKeys.end(), key) != input.pressedKeys.end();
}
void sliderValue(Settings &settings, const OptionsWidget &widget, float x) {
    const float value = unit((x - float(widget.x + 4)) / float(std::max(1, widget.width - 8)));
    if (widget.action == OptionsAction::Sensitivity)
        settings.mouseSensitivity = value;
    else if (widget.action == OptionsAction::SoundLevel && widget.index >= 0 &&
             widget.index < int(settings.soundLevels.size()))
        settings.soundLevels[std::size_t(widget.index)] = value;
    else if (widget.action == OptionsAction::Fov) settings.fov = std::round(30.f + 80.f * value);
    else if (widget.action == OptionsAction::ChatSetting) {
        switch(widget.index) {
        case 3: settings.chatOpacity=value;break; case 5: settings.chatScale=value;break;
        case 6: settings.chatHeightFocused=value;break;case 7: settings.chatHeightUnfocused=value;break;
        case 8: settings.chatWidth=value;break;default:break;
        }
    } else if (widget.action == OptionsAction::VideoSetting) {
        switch(widget.index) {
        case 1: settings.renderDistance=int(std::round(2.f+30.f*value));break;
        case 3: settings.maxFps=int(std::round((10.f+250.f*value)/10.f))*10;break;
        case 7: settings.gamma=value;break;case 12:settings.mipmapLevels=int(std::round(value));break;
        default:break;
        }
    } else if (widget.action == OptionsAction::StreamSetting) {
        switch(widget.index) {
        case 0: settings.streamBytesPerPixel=value;break;case 1:settings.streamFps=value;break;
        case 2: settings.streamKbps=value;break;case 4:settings.streamMicVolume=value;break;
        case 5: settings.streamSystemVolume=value;break;default:break;
        }
    }
}
} // namespace

void OptionsScreen::reset() {
    capturedKey_.clear();
    initialized_ = false;
    draggingScroll_ = draggingList_ = false;
    scroll_ = dragY_ = dragScroll_ = 0;
    slider_ = OptionsAction::None;
    sliderIndex_ = -1;
    action_ = OptionsAction::None;
    pressSound_ = false;
}
void OptionsScreen::setBindingEnabled(const std::string &name, bool enabled) {
    if (enabled)
        disabledBindings_.erase(name);
    else {
        disabledBindings_.insert(name);
        if (capturedKey_ == name)
            capturedKey_.clear();
    }
}
bool OptionsScreen::conflict(const Settings &settings, int index) {
    if (index < 0 || index >= int(settings.keys.size()) || settings.keys[std::size_t(index)].key == 0)
        return false;
    for (std::size_t i = 0; i < settings.keys.size(); ++i)
        if (i != std::size_t(index) && settings.keys[i].key == settings.keys[std::size_t(index)].key)
            return true;
    return false;
}

OptionsLayout OptionsScreen::layout(OptionsPage page, const Settings &settings, int width, int height,
                                    const I18n *tr) const {
    OptionsLayout result;
    result.width = std::max(1, width);
    result.height = std::max(1, height);
    const int center = result.width / 2;
    auto add = [&](OptionsAction action, int index, int x, int y, int w, const std::string &text, bool enabled = true,
                   bool slider = false, float value = 0, bool row = false) {
        result.widgets.push_back({action, index, x, y, w, 20, enabled, slider, row, value, text});
    };
    auto label = [&](const std::string& key,const std::string& fallback){return translated(tr,key,fallback);};
    auto onOff = [&](bool value){return label(value?"options.on":"options.off",value?"ON":"OFF");};
    auto percent = [&](float value){return value==0?label("options.off","OFF"):std::to_string(int(value*100))+"%";};
    auto grid = [&](OptionsAction action,int index,int slot,int y,const std::string& text,bool enabled=true,bool slider=false,float value=0,bool row=false){
        add(action,index,center-155+(slot%2)*160,y,150,text,enabled,slider,value,row);
    };
    if(page==OptionsPage::Main) {
        const int base=result.height/6;
        const auto fov = settings.fov==70?label("options.fov.min","Normal"):settings.fov==110?label("options.fov.max","Quake Pro"):std::to_string(int(settings.fov));
        grid(OptionsAction::Fov,-1,0,base-12,label("options.fov","FOV")+": "+fov,true,true,unit((settings.fov-30)/80));
        if(worldActive_) {
            static const char* difficulties[]={"peaceful","easy","normal","hard"};
            static const char* names[]={"Peaceful","Easy","Normal","Hard"};
            const int difficulty=std::clamp(difficulty_,0,3);
            grid(OptionsAction::Difficulty,-1,1,base-12,label("options.difficulty","Difficulty")+": "+label(std::string("options.difficulty.")+difficulties[difficulty],names[difficulty]),!remoteWorld_&&!hardcore_);
        } else grid(OptionsAction::RealmsNotifications,-1,1,base-12,label("options.realmsNotifications","Realms Notifications")+": "+onOff(settings.realmsNotifications));
        struct Entry{OptionsAction action;const char* key;const char* fallback;};
        const Entry entries[]={
            {OptionsAction::OpenSkin,"options.skinCustomisation","Skin Customization..."},{OptionsAction::SuperSecret,"","Super Secret Settings..."},
            {OptionsAction::OpenSounds,"options.sounds","Music & Sounds..."},{OptionsAction::OpenStream,"options.stream","Broadcast Settings..."},
            {OptionsAction::OpenVideo,"options.video","Video Settings..."},{OptionsAction::OpenControls,"options.controls","Controls..."},
            {OptionsAction::OpenLanguage,"options.language","Language..."},{OptionsAction::OpenChat,"options.chat.title","Chat Settings..."},
            {OptionsAction::OpenResourcePacks,"options.resourcepack","Resource Packs..."},{OptionsAction::OpenSnooper,"options.snooper.view","Snooper Settings..."}};
        for(int i=0;i<10;++i)grid(entries[i].action,-1,i,base+42+(i/2)*24,entries[i].key[0]?label(entries[i].key,entries[i].fallback):entries[i].fallback);
        add(OptionsAction::Done,-1,center-100,base+168,200,label("gui.done","Done"));
        return result;
    }
    if(page==OptionsPage::Skin) {
        static const char* keys[]={"cape","jacket","left_sleeve","right_sleeve","left_pants_leg","right_pants_leg","hat"};
        static const char* names[]={"Cape","Jacket","Left Sleeve","Right Sleeve","Left Pants Leg","Right Pants Leg","Hat"};
        for(int i=0;i<7;++i)grid(OptionsAction::SkinPart,i,i,result.height/6+(i/2)*24,label(std::string("options.modelPart.")+keys[i],names[i])+": "+onOff((settings.skinParts&(1u<<i))!=0));
        add(OptionsAction::Done,-1,center-100,result.height/6+96,200,label("gui.done","Done"));
        return result;
    }
    if(page==OptionsPage::Chat) {
        static const char* keys[]={"options.chat.visibility","options.chat.color","options.chat.links","options.chat.opacity","options.chat.links.prompt","options.chat.scale","options.chat.height.focused","options.chat.height.unfocused","options.chat.width","options.reducedDebugInfo"};
        static const char* names[]={"Chat","Colors","Web Links","Opacity","Prompt on Links","Scale","Focused Height","Unfocused Height","Width","Reduced Debug Info"};
        static const char* modes[]={"options.chat.visibility.full","options.chat.visibility.system","options.chat.visibility.hidden"};
        static const char* modeNames[]={"Shown","Commands Only","Hidden"};
        for(int i=0;i<10;++i){
            float value=0;bool slider=false;std::string amount;
            switch(i) {
            case 0:amount=label(modes[std::clamp(settings.chatVisibility,0,2)],modeNames[std::clamp(settings.chatVisibility,0,2)]);break;
            case 1:amount=onOff(settings.chatColors);break;case 2:amount=onOff(settings.chatLinks);break;
            case 3:value=unit(settings.chatOpacity);amount=std::to_string(int(value*90+10))+"%";slider=true;break;
            case 4:amount=onOff(settings.chatLinksPrompt);break;case 5:value=unit(settings.chatScale);amount=percent(value);slider=true;break;
            case 6:value=unit(settings.chatHeightFocused);amount=std::to_string(int(value*160+20))+"px";slider=true;break;
            case 7:value=unit(settings.chatHeightUnfocused);amount=std::to_string(int(value*160+20))+"px";slider=true;break;
            case 8:value=unit(settings.chatWidth);amount=std::to_string(int(value*280+40))+"px";slider=true;break;
            case 9:amount=onOff(settings.reducedDebugInfo);break;
            }
            grid(OptionsAction::ChatSetting,i,i,result.height/6+(i/2)*24,label(keys[i],names[i])+": "+amount,true,slider,value);
        }
        add(OptionsAction::Done,-1,center-100,result.height/6+120,200,label("gui.done","Done"));
        return result;
    }
    if(page==OptionsPage::Snooper) {
        add(OptionsAction::SnooperEnabled,-1,center-152,result.height-30,150,label("options.snooper","Allow Snooper")+": "+onOff(settings.snooperEnabled));
        add(OptionsAction::Done,-1,center+2,result.height-30,150,label("gui.done","Done"));
        return result;
    }
    if(page==OptionsPage::Stream) {
        static const char* keys[]={"options.stream.bytesPerPixel","options.stream.fps","options.stream.kbps","options.stream.sendMetadata","options.stream.micVolumne","options.stream.systemVolume","options.stream.micToggleBehavior","options.stream.compression","options.stream.chat.enabled","options.stream.chat.userFilter"};
        static const char* names[]={"Quality","Framerate","Bandwidth","Send Metadata","Mic Volume","System Volume","Mic Toggle","Compression","Enable Chat","User Filter"};
        for(int i=0;i<10;++i) {
            float value=0;bool slider=false;std::string amount;
            switch(i) {
            case 0:{value=unit(settings.streamBytesPerPixel);std::ostringstream s;s<<std::fixed<<std::setprecision(3)<<(.1f+value*.1f)<<" bpp";amount=s.str();slider=true;break;}
            case 1:value=unit(settings.streamFps);amount=std::to_string(int(10+value*50))+" fps";slider=true;break;
            case 2:value=unit(settings.streamKbps);amount=std::to_string(int(230+value*3270))+" Kbps";slider=true;break;
            case 3:amount=onOff(settings.streamSendMetadata);break;
            case 4:value=unit(settings.streamMicVolume);amount=percent(value);slider=true;break;
            case 5:value=unit(settings.streamSystemVolume);amount=percent(value);slider=true;break;
            case 6:amount=label(settings.streamMicToggleBehavior?"options.stream.mic_toggle.talk":"options.stream.mic_toggle.mute",settings.streamMicToggleBehavior?"Push to Talk":"Push to Mute");break;
            case 7:{const char* keys[]={"options.stream.compression.low","options.stream.compression.medium","options.stream.compression.high"};const char* names[]={"Low","Medium","High"};const int n=std::clamp(settings.streamCompression,0,2);amount=label(keys[n],names[n]);break;}
            case 8:{const char* keys[]={"options.stream.chat.enabled.streaming","options.stream.chat.enabled.always","options.stream.chat.enabled.never"};const char* names[]={"While Streaming","Always","Never"};const int n=std::clamp(settings.streamChatEnabled,0,2);amount=label(keys[n],names[n]);break;}
            case 9:{const char* keys[]={"options.stream.chat.userFilter.all","options.stream.chat.userFilter.subs","options.stream.chat.userFilter.mods"};const char* names[]={"Everyone","Subscribers","Moderators"};const int n=std::clamp(settings.streamChatUserFilter,0,2);amount=label(keys[n],names[n]);break;}
            }
            const int slot=i>=8?i+2:i;
            grid(OptionsAction::StreamSetting,i,slot,result.height/6+(slot/2)*24,label(keys[i],names[i])+": "+amount,true,slider,value);
        }
        add(OptionsAction::Done,-1,center-155,result.height/6+168,150,label("gui.done","Done"));
        add(OptionsAction::None,-1,center+5,result.height/6+168,150,label("options.stream.ingestSelection","Broadcast Server List..."),false);
        return result;
    }
    if(page==OptionsPage::Video) {
        result.listTop=32;result.listBottom=std::max(32,result.height-32);result.scrollbarX=center+156;
        const int viewport=result.listBottom-result.listTop,contentHeight=8*25;
        result.maxScroll=float(std::max(0,contentHeight-(viewport-4)));
        result.scroll=page_==page?std::clamp(scroll_,0.f,result.maxScroll):0;
        static const char* keys[]={"options.graphics","options.renderDistance","options.ao","options.framerateLimit","options.anaglyph","options.viewBobbing","options.guiScale","options.gamma","options.renderClouds","options.particles","options.fullscreen","options.vsync","options.mipmapLevels","options.blockAlternatives","options.vbo","options.entityShadows"};
        static const char* names[]={"Graphics","Render Distance","Smooth Lighting","Max Framerate","3D Anaglyph","View Bobbing","GUI Scale","Brightness","Clouds","Particles","Fullscreen","Use VSync","Mipmap Levels","Alternate Blocks","Use VBOs","Entity Shadows"};
        for(int i=0;i<16;++i) {
            float value=0;bool slider=false,enabled=true;std::string amount;
            switch(i) {
            case 0:amount=label("options.graphics.fancy","Fancy");enabled=false;break;
            case 1:value=unit((settings.renderDistance-2)/30.f);amount=std::to_string(settings.renderDistance)+" chunks";slider=true;break;
            case 2:{const char* keys[]={"options.ao.off","options.ao.min","options.ao.max"};const char* names[]={"OFF","Minimum","Maximum"};const int n=std::clamp(settings.ambientOcclusion,0,2);amount=label(keys[n],names[n]);break;}
            case 3:value=unit((settings.maxFps-10)/250.f);amount=settings.maxFps==260?label("options.framerateLimit.max","Unlimited"):std::to_string(settings.maxFps)+" fps";slider=true;break;
            case 4:amount=onOff(false);enabled=false;break;
            case 5:amount=onOff(settings.viewBobbing);break;
            case 6:{const char* keys[]={"options.guiScale.auto","options.guiScale.small","options.guiScale.normal","options.guiScale.large"};const char* names[]={"Auto","Small","Normal","Large"};const int n=std::clamp(settings.guiScale,0,3);amount=label(keys[n],names[n]);break;}
            case 7:value=unit(settings.gamma);amount=value==0?label("options.gamma.min","Moody"):value==1?label("options.gamma.max","Bright"):"+"+std::to_string(int(value*100))+"%";slider=true;break;
            case 8:amount=onOff(false);enabled=false;break;
            case 9:{const char* keys[]={"options.particles.all","options.particles.decreased","options.particles.minimal"};const char* names[]={"All","Decreased","Minimal"};const int n=std::clamp(settings.particles,0,2);amount=label(keys[n],names[n]);break;}
            case 10:amount=onOff(settings.fullscreen);break;case 11:amount=onOff(settings.vsync);break;
            // The runtime atlas currently has padding for mip level 1. Expose
            // its effective range while preserving higher legacy file values.
            case 12:value=float(std::clamp(settings.mipmapLevels,0,1));amount=value==0?onOff(false):"1";slider=true;break;
            case 13:amount=onOff(settings.blockAlternatives);break;
            case 14:amount=onOff(true);enabled=false;break;case 15:amount=onOff(false);enabled=false;break;
            }
            grid(OptionsAction::VideoSetting,i,i,result.listTop+4+(i/2)*25-int(result.scroll),label(keys[i],names[i])+": "+amount,enabled,slider,value,true);
        }
        if(result.maxScroll>0&&viewport>0){result.thumbHeight=std::clamp(viewport*viewport/contentHeight,std::min(32,std::max(1,viewport-8)),std::max(1,viewport-8));result.thumbY=result.listTop+int(result.scroll*(viewport-result.thumbHeight)/result.maxScroll);}
        add(OptionsAction::Done,-1,center-100,result.height-27,200,label("gui.done","Done"));
        return result;
    }
    if (page == OptionsPage::Sounds) {
        static const char *names[] = {"master",  "music",   "record", "weather", "block",
                                      "hostile", "neutral", "player", "ambient"};
        static const char *labels[] = {
            "Master Volume",      "Music",   "Jukebox/Note Blocks", "Weather", "Blocks", "Hostile Creatures",
            "Friendly Creatures", "Players", "Ambient/Environment"};
        for (int category = 0; category < 9; ++category) {
            int slot = category == 0 ? 0 : category + 1;
            float value = unit(settings.soundLevels[std::size_t(category)]);
            auto amount = value == 0 ? translated(tr, "options.off", "OFF") : std::to_string(int(value * 100.f)) + "%";
            add(OptionsAction::SoundLevel, category, center - 155 + slot % 2 * 160,
                result.height / 6 - 12 + 24 * (slot / 2), category == 0 ? 310 : 150,
                translated(tr, std::string("soundCategory.") + names[category], labels[category]) + ": " + amount, true,
                true, value);
        }
        add(OptionsAction::Done, -1, center - 100, result.height / 6 + 168, 200, translated(tr, "gui.done", "Done"));
        return result;
    }
    result.listTop = 63;
    result.listBottom = std::max(result.listTop, result.height - 32);
    result.scrollbarX = center + 139;
    std::vector<int> order(settings.keys.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const auto &first = settings.keys[std::size_t(a)];
        const auto &second = settings.keys[std::size_t(b)];
        auto categoryA = categoryLabel(first, tr), categoryB = categoryLabel(second, tr);
        return categoryA == categoryB ? keyLabel(first, tr) < keyLabel(second, tr) : categoryA < categoryB;
    });
    std::string previousCategory;
    bool first = true;
    for (int index : order) {
        const auto &key = settings.keys[std::size_t(index)];
        if (first || key.category != previousCategory) {
            result.rows.push_back({-1, 0, categoryLabel(key, tr)});
            previousCategory = key.category;
            first = false;
        }
        result.rows.push_back({index, 0, keyLabel(key, tr)});
    }
    const int contentHeight = int(result.rows.size()) * 20, viewport = result.listBottom - result.listTop;
    result.maxScroll = float(std::max(0, contentHeight - (viewport - 4)));
    result.scroll = page_ == page ? std::clamp(scroll_, 0.f, result.maxScroll) : 0.f;
    int rowIndex = 0;
    for (auto &row : result.rows) {
        row.y = result.listTop + 4 - int(result.scroll) + rowIndex++ * 20;
        if (row.keyIndex < 0)
            continue;
        const auto &key = settings.keys[std::size_t(row.keyIndex)];
        const bool enabled = disabledBindings_.count(key.name) == 0;
        auto display = bindingLabel(key.key, tr);
        if (capturedKey_ == key.name)
            display = u8"\u00a7f> \u00a7e" + display + u8"\u00a7f <";
        else if (conflict(settings, row.keyIndex))
            display = u8"\u00a7c" + display;
        add(OptionsAction::BindKey, row.keyIndex, center - 19, row.y, 75, display, enabled, false, 0, true);
        add(OptionsAction::ResetKey, row.keyIndex, center + 66, row.y, 50, translated(tr, "controls.reset", "Reset"),
            enabled && key.key != key.defaultKey, false, 0, true);
    }
    if (result.maxScroll > 0 && viewport > 0) {
        const int upper = std::max(1, viewport - 8), lower = std::min(32, upper);
        result.thumbHeight = std::clamp(contentHeight ? viewport * viewport / contentHeight : upper, lower, upper);
        result.thumbY =
            result.listTop + int(result.scroll) * std::max(0, viewport - result.thumbHeight) / int(result.maxScroll);
    }
    bool changed = false;
    for (const auto &key : settings.keys)
        changed = changed || key.key != key.defaultKey;
    add(OptionsAction::Done, -1, center - 155, result.height - 29, 150, translated(tr, "gui.done", "Done"));
    add(OptionsAction::ResetAll, -1, center + 5, result.height - 29, 150,
        translated(tr, "controls.resetAll", "Reset Keys"), changed);
    add(OptionsAction::InvertMouse, -1, center - 155, 18, 150,
        translated(tr, "options.invertMouse", "Invert Mouse") + ": " +
            translated(tr, settings.invertMouse ? "options.on" : "options.off", settings.invertMouse ? "ON" : "OFF"));
    float sensitivity = unit(settings.mouseSensitivity);
    auto amount = sensitivity == 0   ? translated(tr, "options.sensitivity.min", "*yawn*")
                  : sensitivity == 1 ? translated(tr, "options.sensitivity.max", "HYPERSPEED!!!")
                                     : std::to_string(int(sensitivity * 200.f)) + "%";
    add(OptionsAction::Sensitivity, -1, center + 5, 18, 150,
        translated(tr, "options.sensitivity", "Sensitivity") + ": " + amount, true, true, sensitivity);
    // There is no touchscreen input implementation or Settings field yet.
    add(OptionsAction::Touchscreen, -1, center - 155, 42, 150,
        translated(tr, "options.touchscreen", "Touchscreen Mode") + ": " + translated(tr, "options.off", "OFF"), false);
    return result;
}

bool OptionsScreen::interact(OptionsPage page, Settings &settings, const OptionsInput &input, const I18n *tr) {
    if (!initialized_ || page_ != page) {
        reset();
        page_ = page;
        initialized_ = true;
    }
    if (!input.focused) {
        capturedKey_.clear();
        slider_ = OptionsAction::None;
        sliderIndex_ = -1;
        draggingList_ = draggingScroll_ = false;
        return false;
    }
    auto view = layout(page, settings, input.width, input.height, tr);
    scroll_ = view.scroll;
    const bool withinList = (page == OptionsPage::Controls || page == OptionsPage::Video) && input.y >= view.listTop && input.y < view.listBottom &&
                            input.x >= 0 && input.x < view.width;
    if (withinList && input.wheel) {
        scroll_ = std::clamp(scroll_ - float(input.wheel) * (page==OptionsPage::Video?12.5f:10.f), 0.f, view.maxScroll);
        view = layout(page, settings, input.width, input.height, tr);
    }
    if (capturingKey()) {
        bool exists = false;
        for (const auto &key : settings.keys)
            exists = exists || key.name == capturedKey_;
        if (!exists) {
            capturedKey_.clear();
            return false;
        }
        for (int key : input.pressedKeys)
            if (key > 0 && key < 256) {
                settings.bind(capturedKey_, key == VK_ESCAPE ? 0 : key);
                capturedKey_.clear();
                return false;
            }
        if (input.leftPressed) {
            settings.bind(capturedKey_, VK_LBUTTON);
            capturedKey_.clear();
        }
        return false;
    }
    if (pressed(input, VK_ESCAPE)) {
        reset();
        return true;
    }
    if (slider_ != OptionsAction::None) {
        if (input.leftDown || input.leftReleased)
            for (const auto &widget : view.widgets)
                if (widget.action == slider_ && widget.index == sliderIndex_) {
                    sliderValue(settings, widget, input.x);
                    break;
                }
        if (!input.leftDown || input.leftReleased) {
            pressSound_ = true;
            slider_ = OptionsAction::None;
            sliderIndex_ = -1;
        }
        return false;
    }
    if (draggingScroll_ || draggingList_) {
        if (input.leftDown) {
            const int travel = view.listBottom - view.listTop - view.thumbHeight;
            float multiplier = draggingScroll_ ? (travel > 0 ? view.maxScroll / float(travel) : 0.f) : -1.f;
            scroll_ = std::clamp(dragScroll_ + (input.y - dragY_) * multiplier, 0.f, view.maxScroll);
        }
        if (!input.leftDown || input.leftReleased)
            draggingScroll_ = draggingList_ = false;
        return false;
    }
    if (!input.leftPressed)
        return false;
    for (const auto &widget : view.widgets) {
        if (widget.listRow && !withinList)
            continue;
        if (!widget.enabled || !widget.hit(input.x, input.y))
            continue;
        if (widget.slider) {
            slider_ = widget.action;
            sliderIndex_ = widget.index;
            sliderValue(settings, widget, input.x);
            return false;
        }
        action_ = widget.action;
        pressSound_ = true;
        switch (widget.action) {
        case OptionsAction::Done:
            reset();
            pressSound_=true;
            return true;
        case OptionsAction::ResetAll:
            settings.resetKeys();
            return false;
        case OptionsAction::InvertMouse:
            settings.invertMouse = !settings.invertMouse;
            return false;
        case OptionsAction::RealmsNotifications:settings.realmsNotifications=!settings.realmsNotifications;return false;
        case OptionsAction::SnooperEnabled:settings.snooperEnabled=!settings.snooperEnabled;return false;
        case OptionsAction::SuperSecret:settings.secretShader=settings.secretShader>=23?-1:settings.secretShader+1;return false;
        case OptionsAction::SkinPart:
            if(widget.index>=0&&widget.index<7)settings.skinParts^=1u<<widget.index;
            return false;
        case OptionsAction::ChatSetting:
            switch(widget.index){
            case 0:settings.chatVisibility=(settings.chatVisibility+1)%3;break;
            case 1:settings.chatColors=!settings.chatColors;break;case 2:settings.chatLinks=!settings.chatLinks;break;
            case 4:settings.chatLinksPrompt=!settings.chatLinksPrompt;break;case 9:settings.reducedDebugInfo=!settings.reducedDebugInfo;break;
            default:break;
            }
            return false;
        case OptionsAction::VideoSetting:
            switch(widget.index){
            case 2:settings.ambientOcclusion=(settings.ambientOcclusion+1)%3;break;
            case 5:settings.viewBobbing=!settings.viewBobbing;break;case 6:settings.guiScale=(settings.guiScale+1)%4;break;
            case 9:settings.particles=(settings.particles+1)%3;break;case 10:settings.fullscreen=!settings.fullscreen;break;
            case 11:settings.vsync=!settings.vsync;break;case 13:settings.blockAlternatives=!settings.blockAlternatives;break;default:break;
            }
            return false;
        case OptionsAction::StreamSetting:
            switch(widget.index){
            case 3:settings.streamSendMetadata=!settings.streamSendMetadata;break;
            case 6:settings.streamMicToggleBehavior=(settings.streamMicToggleBehavior+1)%2;break;
            case 7:settings.streamCompression=(settings.streamCompression+1)%3;break;
            case 8:settings.streamChatEnabled=(settings.streamChatEnabled+1)%3;break;
            case 9:settings.streamChatUserFilter=(settings.streamChatUserFilter+1)%3;break;default:break;
            }
            return false;
        case OptionsAction::BindKey:
            capturedKey_ = settings.keys[std::size_t(widget.index)].name;
            return false;
        case OptionsAction::ResetKey: {
            auto &key = settings.keys[std::size_t(widget.index)];
            settings.bind(key.name, key.defaultKey);
            return false;
        }
        default:
            return false;
        }
    }
    if (withinList && view.maxScroll > 0) {
        draggingScroll_ = input.x >= view.scrollbarX && input.x < view.scrollbarX + 6;
        draggingList_ = !draggingScroll_;
        dragY_ = input.y;
        dragScroll_ = scroll_;
    }
    return false;
}

bool OptionsScreen::draw(OptionsPage page, Settings &settings, Renderer &renderer, Window &window, I18n &tr,
                         float scale) {
    if (!std::isfinite(scale) || scale <= 0)
        scale = 1;
    OptionsInput input;
    input.width = std::max(1, int(std::ceil(window.width() / scale)));
    input.height = std::max(1, int(std::ceil(window.height() / scale)));
    input.x = window.mouseX / scale;
    input.y = window.mouseY / scale;
    input.leftPressed = window.pressed(VK_LBUTTON);
    input.leftDown = window.down(VK_LBUTTON);
    input.leftReleased = window.released(VK_LBUTTON);
    input.wheel = window.wheel;
    input.focused = window.focused();
    input.pressedKeys = window.pressedKeyOrder;
    if (interact(page, settings, input, &tr))
        return true;
    auto view = layout(page, settings, input.width, input.height, &tr);
    if(worldActive_)renderer.gradient(0,0,float(window.width()),float(window.height()),0x101010b0u,0x202020d0u);
    else for(int y=0;y<view.height;y+=32)for(int x=0;x<view.width;x+=32)
        renderer.sprite("options_background",x*scale,y*scale,32*scale,32*scale,0,0,16,16,0x404040ffu);
    auto centered = [&](const std::string &text, float x, float y, std::uint32_t color = 0xffffffffu) {
        renderer.text(text, x * scale - renderer.textWidth(text, scale) / 2, y * scale, scale, color);
    };
    auto button = [&](const OptionsWidget &widget) {
        bool hover =
            widget.hit(input.x, input.y) && (!widget.listRow || (input.y >= view.listTop && input.y < view.listBottom));
        int state = !widget.enabled || widget.slider ? 0 : hover ? 2 : 1, half = widget.width / 2;
        renderer.sprite("widgets", widget.x * scale, widget.y * scale, half * scale, widget.height * scale, 0,
                        float(46 + state * 20), float(half), float(widget.height));
        renderer.sprite("widgets", (widget.x + half) * scale, widget.y * scale, half * scale, widget.height * scale,
                        float(200 - half), float(46 + state * 20), float(half), float(widget.height));
        if (widget.slider) {
            const int x = widget.x + int(unit(widget.value) * float(widget.width - 8));
            renderer.sprite("widgets", x * scale, widget.y * scale, 4 * scale, 20 * scale, 0, 66, 4, 20);
            renderer.sprite("widgets", (x + 4) * scale, widget.y * scale, 4 * scale, 20 * scale, 196, 66, 4, 20);
        }
        centered(widget.text, float(widget.x + widget.width / 2), float(widget.y + (widget.height - 8) / 2),
                 !widget.enabled ? 0xa0a0a0ffu
                 : hover         ? 0xffffa0ffu
                                 : 0xe0e0e0ffu);
    };
    if (page == OptionsPage::Controls || page == OptionsPage::Video) {
        auto background = [&](int top, int bottom, int offset, std::uint32_t color) {
            // Runtime UI textures clamp at their edges. Tile the original dirt
            // in bounded source rectangles, preserving GuiSlot's scroll phase.
            for (int y = top; y < bottom;) {
                int sourceY = ((y + offset) % 32 + 32) % 32, h = std::min(32 - sourceY, bottom - y);
                for (int x = 0; x < view.width; x += 32) {
                    int w = std::min(32, view.width - x);
                    renderer.sprite("options_background", x * scale, y * scale, w * scale, h * scale, 0,
                                    float(sourceY) / 2, float(w) / 2, float(h) / 2, color);
                }
                y += h;
            }
        };
        background(view.listTop, view.listBottom, int(view.scroll), 0x202020ffu);
        float widest = 0;
        for (const auto &row : view.rows)
            if (row.keyIndex >= 0)
                widest = std::max(widest, renderer.textWidth(row.label, scale) / scale);
        for (const auto &row : view.rows)
            if (row.y + 20 > view.listTop && row.y < view.listBottom) {
                if (row.keyIndex < 0)
                    centered(row.label, float(view.width / 2), float(row.y + 6));
                else {
                    bool enabled = disabledBindings_.count(settings.keys[std::size_t(row.keyIndex)].name) == 0;
                    renderer.text(row.label, (float(view.width / 2 - 34) - widest) * scale, (row.y + 4) * scale, scale,
                                  enabled ? 0xffffffffu : 0xa0a0a0ffu);
                }
            }
        for (const auto &widget : view.widgets)
            if (widget.listRow && widget.y + 20 > view.listTop && widget.y < view.listBottom)
                button(widget);
        // GuiSlot masks partial rows by drawing its top/bottom backgrounds after
        // the list. This also works with Renderer's deferred UI batches.
        background(0, view.listTop, 0, 0x404040ffu);
        background(view.listBottom, view.height, 0, 0x404040ffu);
        renderer.gradient(0, view.listTop * scale, view.width * scale, 4 * scale, 0x000000ffu, 0x00000000u);
        renderer.gradient(0, (view.listBottom - 4) * scale, view.width * scale, 4 * scale, 0x00000000u, 0x000000ffu);
        if (view.thumbHeight > 0) {
            renderer.rect(view.scrollbarX * scale, view.listTop * scale, 6 * scale,
                          (view.listBottom - view.listTop) * scale, 0x000000ffu);
            renderer.rect(view.scrollbarX * scale, view.thumbY * scale, 6 * scale, view.thumbHeight * scale,
                          0x808080ffu);
            renderer.rect(view.scrollbarX * scale, view.thumbY * scale, 5 * scale, (view.thumbHeight - 1) * scale,
                          0xc0c0c0ffu);
        }
    }
    for (const auto &widget : view.widgets)
        if (!widget.listRow)
            button(widget);
    const char* title="options.title";float titleY=20;
    switch(page){
    case OptionsPage::Controls:title="controls.title";titleY=8;break;
    case OptionsPage::Sounds:title="options.sounds.title";titleY=15;break;
    case OptionsPage::Main:title="options.title";titleY=15;break;
    case OptionsPage::Video:title="options.videoTitle";titleY=5;break;
    case OptionsPage::Skin:title="options.skinCustomisation.title";break;
    case OptionsPage::Chat:title="options.chat.title";break;
    case OptionsPage::Snooper:title="options.snooper.title";titleY=8;break;
    case OptionsPage::Stream:title="options.stream.title";break;
    }
    centered(tr(title),float(view.width/2),titleY);
    if(page==OptionsPage::Stream) centered(tr("options.stream.chat.title"),float(view.width/2),float(view.height/6+102));
    if(page==OptionsPage::Snooper){
        const std::string description=tr("options.snooper.desc");std::string line;float y=22;
        for(std::size_t i=0;i<description.size();){
            std::size_t end=i+1;while(end<description.size()&&(static_cast<unsigned char>(description[end])&0xc0)==0x80)++end;
            const auto letter=description.substr(i,end-i);
            if(renderer.textWidth(line+letter,1)>view.width-30){
                const auto space=line.find_last_of(' ');
                if(space!=std::string::npos&&space>0){
                    centered(line.substr(0,space),float(view.width/2),y,0x808080ffu);line.erase(0,space+1);
                } else {centered(line,float(view.width/2),y,0x808080ffu);line.clear();}
                y+=9;
            }
            line+=letter;i=end;
        }
        if(!line.empty())centered(line,float(view.width/2),y,0x808080ffu);
        const float listTop=std::max(80.f,y+14),listBottom=float(view.height-40);
        if(listBottom>listTop+10){
            renderer.rect(0,listTop*scale,view.width*scale,(listBottom-listTop)*scale,0x00000060u);
            const std::pair<const char*,std::string> stats[]={
                {"version","1.8.9"},{"os_name","Windows"},{"display_type","OpenGL"},
                {"gui_supported","true"},{"snooper_enabled",settings.snooperEnabled?"true":"false"},
                {"client_brand","NativeVoxel"}};
            float sy=listTop+4;for(const auto& stat:stats){if(sy+9>listBottom)break;
                auto value=stat.second;
                while(!value.empty()&&renderer.textWidth(value,1)>std::max(0,view.width-240))value.pop_back();
                renderer.text(stat.first,10*scale,sy*scale,scale);renderer.text(value,230*scale,sy*scale,scale);sy+=10;}
        }
    }
    return false;
}
} // namespace mc
