#include "mc/audio.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::size_t checks=0;
void require(bool condition,const std::string& message) {
    ++checks;if(!condition)throw std::runtime_error(message);
}
bool close(float a,float b){return std::abs(a-b)<0.00001f;}

void registryTests() {
    mc::SoundRegistry registry;std::string error;
    require(registry.mergeJson(R"JSON({
        "base":{"category":"block","sounds":[{"name":"test/a","weight":2,"volume":0.25,"pitch":1.4},"test/b"]},
        "alias":{"category":"music","sounds":[{"name":"base","type":"event","weight":99,"volume":8,"pitch":9,"stream":true}]},
        "stream":{"sounds":[{"name":"music/a","stream":true}]},
        "empty":{"sounds":[]},
        "missing_alias":{"sounds":[{"name":"missing","type":"event"}]}
    })JSON","minecraft",error),error);
    require(registry.eventCount()==5,"all fixture events parsed");
    require(registry.weight("base")==3,"file weight sum");
    require(registry.weight("alias")==3,"1.8.9 alias inherits child weight, ignores wrapper weight");
    require(registry.weight("empty")==0&&registry.weight("missing_alias")==0,"empty and missing alias weights");
    auto seed=mc::SoundRegistry::javaSeed(12345);mc::ResolvedSound sound;
    int aCount=0;
    for(int i=0;i<6000;++i) {
        require(registry.resolve("alias",seed,sound,error),error);
        require(sound.category==mc::SoundCategory::Music,"outer event category survives alias resolution");
        require(!sound.stream,"alias wrapper stream flag is ignored in 1.8.9");
        if(sound.resource=="minecraft:sounds/test/a.ogg") {
            ++aCount;require(close(sound.volume,.25f)&&close(sound.pitch,1.4f),"alias wrapper does not multiply leaf pitch/volume");
        } else require(sound.resource=="minecraft:sounds/test/b.ogg"&&sound.volume==1&&sound.pitch==1,"default sound properties");
    }
    require(aCount>3850&&aCount<4150,"weighted selection distribution matches two-to-one pool");
    require(registry.resolve("stream",seed,sound,error)&&sound.stream,"stream flag preserved on file entry");
    require(!registry.resolve("empty",seed,sound,error)&&!error.empty(),"empty event reported");
    require(!registry.resolve("unknown",seed,sound,error),"unknown event reported");

    require(registry.mergeJson(R"({"base":{"category":"weather","sounds":[{"name":"test/c","weight":4}]}})","minecraft",error),error);
    require(registry.weight("base")==7,"replace=false appends sound entries");
    require(registry.resolve("base",seed,sound,error)&&sound.category==mc::SoundCategory::Block,"append retains existing category");
    require(registry.mergeJson(R"({"base":{"replace":true,"category":"player","sounds":["new"]}})","minecraft",error),error);
    require(registry.weight("base")==1&&registry.weight("alias")==1,"replace updates alias weights dynamically");
    require(registry.resolve("base",seed,sound,error)&&sound.resource=="minecraft:sounds/new.ogg"&&sound.category==mc::SoundCategory::Player,"replace resets category and choices");

    std::size_t before=registry.eventCount();
    for(const std::string invalid:{R"({"new":{"sounds":[{"name":"x","weight":0}]}})",
                                   R"({"new":{"sounds":[{"name":"x","pitch":0}]}})",
                                   R"({"new":{"sounds":[{"name":"x","volume":1e309}]}})",
                                   R"({"new":{"sounds":[{"name":"x","stream":"true"}]}})",
                                   R"({"new":{"sounds":[{"name":"x","type":"bogus"}]}})",
                                   R"({"new":{"category":"records","sounds":[]}})",
                                   R"({"new":{"sounds":["../outside"]}})",
                                   R"({"new":{"sounds":["C:\outside"]}})",
                                   R"({"new":{"sounds":["x"]},})",R"({"new":{"sounds":["\uD800"]}})"}) {
        require(!registry.mergeJson(invalid,"minecraft",error),"invalid sound JSON must fail: "+invalid);
        require(registry.eventCount()==before,"failed merge is transactional");
    }
    require(registry.mergeJson(R"({"unicode\u002ename":{"sounds":["custom/path"]}})","custom",error),error);
    require(registry.resolve("custom:unicode.name",seed,sound,error)&&sound.resource=="custom:sounds/custom/path.ogg","Unicode JSON escape and inherited file namespace");
    // Explicit namespace file references are supported; SOUND_EVENT reproduces
    // the supplied MCP raw-name path behavior rather than modern-version semantics.
    require(registry.mergeJson(R"({"file":{"sounds":["other:file"]},"bad_alias":{"sounds":[{"name":"minecraft:base","type":"event"}]}})","custom",error),error);
    require(registry.resolve("custom:file",seed,sound,error)&&sound.resource=="other:sounds/file.ogg","explicit file namespace");
    require(!registry.resolve("custom:bad_alias",seed,sound,error),"1.8.9 explicit alias namespace raw-path quirk");
    mc::SoundRegistry cyclic;
    require(cyclic.mergeJson(R"({"a":{"sounds":[{"name":"b","type":"event"}]},"b":{"sounds":[{"name":"a","type":"event"}]}})","minecraft",error),error);
    require(!cyclic.resolve("a",seed,sound,error)&&error.find("Cyclic")!=std::string::npos,"alias cycles are bounded");

    mc::SoundRegistry randomFixture;
    require(randomFixture.mergeJson(R"({"ten":{"sounds":["0","1","2","3","4","5","6","7","8","9"]}})","minecraft",error),error);
    seed=mc::SoundRegistry::javaSeed(0);
    const int expected[]={0,8,9,7,5,3,1,1,9,4}; // java.util.Random(0).nextInt(10)
    for(int value:expected)require(randomFixture.resolve("ten",seed,sound,error)&&sound.resource=="minecraft:sounds/"+std::to_string(value)+".ogg","Java Random known sequence");
}

void propertyTests() {
    mc::ResolvedSound sound;sound.volume=.5f;sound.pitch=2;
    auto values=mc::Audio::properties(sound,.1f,.25f,.5f,.25f);
    require(close(values.gain,.00625f)&&values.pitch==.5f&&values.maxDistance==16,"gain/pitch normalization order");
    values=mc::Audio::properties(sound,4,10);
    require(values.gain==1&&values.pitch==2&&values.maxDistance==64,"source volume controls attenuation distance before clamping");
    require(mc::Audio::attenuation(0,64)==1&&mc::Audio::attenuation(32,64)==.5f&&mc::Audio::attenuation(64,64)==0,"linear distance attenuation");
    require(mc::Audio::attenuation(100000,16,true)==1,"relative UI sound has no attenuation");
    require(mc::Audio::attenuation(std::numeric_limits<double>::infinity(),16)==0,"invalid distance is silent");
    auto orientation=mc::Audio::listenerOrientation(0,0);
    require(close(orientation[0],0)&&close(orientation[1],0)&&close(orientation[2],1)&&close(orientation[4],1),"yaw zero faces positive Z");
    orientation=mc::Audio::listenerOrientation(90,0);require(close(orientation[0],-1)&&close(orientation[2],0),"positive yaw faces negative X");
    orientation=mc::Audio::listenerOrientation(0,90);require(close(orientation[1],-1)&&close(orientation[5],1),"positive pitch looks down");
    for(float yaw:{-123.f,0.f,40.f,275.f})for(float pitch:{-90.f,-30.f,0.f,74.f,90.f}) {
        auto v=mc::Audio::listenerOrientation(yaw,pitch);
        require(close(v[0]*v[3]+v[1]*v[4]+v[2]*v[5],0),"listener forward/up orthogonal");
        require(close(v[0]*v[0]+v[1]*v[1]+v[2]*v[2],1)&&close(v[3]*v[3]+v[4]*v[4]+v[5]*v[5],1),"listener orientation normalized");
    }
}

std::string soundFile(const std::string& directory,const std::string& resource) {
    auto colon=resource.find(':');return directory+"/resources/assets/"+resource.substr(0,colon)+"/"+resource.substr(colon+1);
}
void resourceTests(const std::string& directory,bool decodeAll) {
    std::string error;mc::SoundRegistry registry;require(registry.load(directory,error),error);
    require(registry.eventCount()==246,"default 1.8.9 sounds.json event count");
    require(registry.warnings().empty(),"all sound event files exist");
    require(registry.weight("music.game.creative")==registry.weight("music.game")+6,"original creative music alias pool");
    auto seed=mc::SoundRegistry::javaSeed(5);mc::ResolvedSound sound;
    for(const auto& event:registry.eventNames()) {
        require(registry.weight(event)>0,"nonempty default event: "+event);
        require(registry.resolve(event,seed,sound,error),error);
        require(std::ifstream(soundFile(directory,sound.resource),std::ios::binary).good(),"resolved sound exists: "+sound.resource);
    }
    const std::string click=directory+"/resources/assets/minecraft/sounds/random/click.ogg";
    mc::DecodedSound decoded;require(mc::Audio::decodeVorbisFile(click,decoded,error),error);
    require(decoded.frames()>100&&decoded.sampleRate>=8000&&(decoded.channels==1||decoded.channels==2),"decode original click sample");
    require(std::any_of(decoded.samples.begin(),decoded.samples.end(),[](std::int16_t value){return value!=0;}),"decoded waveform is not all silence");
    require(!mc::Audio::decodeVorbisFile(click,decoded,error,1),"decode allocation bounded by maxFrames");
    require(!mc::Audio::decodeVorbisFile(directory+"/resources/assets/minecraft/sounds.json",decoded,error),"non-Ogg data rejected");
    {
        std::ifstream original(click,std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(original)),std::istreambuf_iterator<char>());
        struct Temporary {
            std::string path;
            ~Temporary(){std::remove(path.c_str());}
        } temporary{directory+"/runtime/audio/.corrupt-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".ogg"};
        require(bytes.size()>40,"corruption fixture has an Ogg page");
        bytes.back()^=0x10;
        {std::ofstream out(temporary.path,std::ios::binary);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));}
        require(!mc::Audio::decodeVorbisFile(temporary.path,decoded,error)&&error.find("CRC")!=std::string::npos,"modified Ogg page CRC rejected before decoder");
        {std::ofstream out(temporary.path,std::ios::binary|std::ios::trunc);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()-5));}
        require(!mc::Audio::decodeVorbisFile(temporary.path,decoded,error)&&error.find("Truncated")!=std::string::npos,"truncated Ogg container rejected");
    }

    // Silent backend is mandatory: this test never loads OpenAL or opens a device.
    mc::Audio audio;require(audio.initialize(directory,error,true),error);
    require(audio.enabled()&&audio.nullOutput(),"nullOutput explicitly selected");
    audio.updateListener(5,65,8,90,10);
    require(audio.play("random.click",0,0,0,1,1,true),audio.lastError());
    require(audio.play("random.click",0,0,0,1,1,true),audio.lastError());
    require(audio.activeVoices()==2&&audio.decodedBufferCount()==1,"decoded static buffer reuse");
    audio.stopAll();require(audio.activeVoices()==0,"stopAll releases voices");
    for(int i=0;i<40;++i)require(audio.play("random.click",0,0,0,1,1,true),audio.lastError());
    require(audio.activeVoices()==32,"static source budget and oldest-voice replacement");
    audio.stopAll();auto cacheCount=audio.decodedBufferCount();
    require(audio.play("music.menu",0,0,0,1,1,true),audio.lastError());
    require(audio.activeVoices()==1&&audio.decodedBufferCount()==cacheCount,"stream flag uses incremental decoder instead of static cache");
    audio.tick();audio.stopAll();
    require(audio.play("step.stone",5,65,8),audio.lastError());
    audio.setCategoryVolume(mc::SoundCategory::Neutral,0);
    require(audio.activeVoices()==0,"muting category stops its playing voices");
    require(!audio.play("step.stone",5,65,8),"muted category skips new sounds");
    audio.setCategoryVolume(mc::SoundCategory::Neutral,1);
    require(audio.play("step.stone",5,65,8),audio.lastError());
    audio.setMasterVolume(0);require(audio.activeVoices()==0&&!audio.play("random.click",0,0,0),"master mute stops and skips sounds");
    audio.setMasterVolume(1);require(!audio.play("not.a.sound",0,0,0)&&!audio.lastError().empty(),"unknown playback event reported");
    require(!audio.play("random.click",std::numeric_limits<double>::quiet_NaN(),0,0),"invalid source coordinates rejected");
    require(!audio.initialize(directory+"/does-not-exist",error,true)&&audio.enabled(),"failed reinitialize retains prior audio registry");
    audio.stopAll();

    if(decodeAll) {
        std::ifstream files(directory+"/runtime/audio/ogg-files.txt");require(files.good(),"OGG inventory exists");
        std::string resource;std::size_t count=0;std::uint64_t frames=0;
        while(std::getline(files,resource)) {
            if(!resource.empty()&&resource.back()=='\r')resource.pop_back();
            if(resource.empty())continue;
            mc::DecodedSound pcm;
            require(mc::Audio::decodeVorbisFile(soundFile(directory,resource),pcm,error),resource+": "+error);
            require(pcm.frames()>0,"nonempty original waveform: "+resource);
            ++count;frames+=pcm.frames();
        }
        require(count==578,"all 578 original OGG resources decoded");
        std::cout<<"Decoded "<<count<<" original OGG files, "<<frames<<" PCM frames without audio output.\n";
    }
}
} // namespace

int main(int argc,char** argv) {
    try {
        registryTests();propertyTests();
        const std::string directory=argc>1?argv[1]:"assets";
        resourceTests(directory,argc>2&&std::string(argv[2])=="--decode-all");
        std::cout<<checks<<" audio checks passed (null output; no audio device opened).\n";return 0;
    } catch(const std::exception& error){std::cerr<<"Audio test failed: "<<error.what()<<'\n';return 1;}
}
