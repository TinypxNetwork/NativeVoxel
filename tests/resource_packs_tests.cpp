#include "mc/resource_packs.hpp"
#include "mc/assets.hpp"
#include "mc/render_assets.hpp"
#include "mc/first_person.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

int main(int argc,char** argv){
    try{
#ifdef _WIN32
        int count=0;auto wide=CommandLineToArgvW(GetCommandLineW(),&count);if(!wide)throw std::runtime_error("Cannot read wide test command line");std::vector<std::string> utf8;std::vector<char*> arguments;
        for(int i=0;i<count;++i){const int bytes=WideCharToMultiByte(CP_UTF8,0,wide[i],-1,nullptr,0,nullptr,nullptr);if(bytes<=0)throw std::runtime_error("Invalid UTF-16 command line");std::string value(std::size_t(bytes),'\0');WideCharToMultiByte(CP_UTF8,0,wide[i],-1,&value[0],bytes,nullptr,nullptr);value.pop_back();utf8.push_back(std::move(value));}LocalFree(wide);for(auto& argument:utf8)arguments.push_back(&argument[0]);argc=count;argv=arguments.data();
#endif
        if(argc<3)throw std::runtime_error("Expected inspect/prepare/base command and paths");std::string error;
        if(std::string(argv[1])=="inspect"){mc::ResourcePackInfo info;if(!mc::inspectResourcePack(argv[2],info,error))throw std::runtime_error(error);std::cout<<info.format<<' '<<info.files<<' '<<info.description<<'\n';return 0;}
        if(std::string(argv[1])=="rollback"){
            if(argc!=6)throw std::runtime_error("Expected base/cache/selected/bad-pack paths");mc::PreparedResourcePacks previous;previous.assetDirectory=argv[4];previous.decodedImages=123;mc::ResourcePackInfo selected;selected.path="selected";previous.packs.push_back(selected);mc::Assets loaded;if(!loaded.load(previous.assetDirectory,error))throw std::runtime_error(error);const auto pixels=loaded.atlasPixels();
            if(mc::prepareResourcePacks(argv[2],{argv[5]},argv[3],previous,error)||error.empty())throw std::runtime_error("Invalid pack must fail");
            if(previous.assetDirectory!=argv[4]||previous.decodedImages!=123||previous.packs.size()!=1||previous.packs[0].path!="selected"||loaded.atlasPixels()!=pixels)throw std::runtime_error("Failed reload changed the selected cache or live assets");
            std::cout<<"PASS: failed reload preserves selected cache and live assets\n";return 0;
        }
        if(std::string(argv[1])=="prepare"){
            if(argc<4)throw std::runtime_error("Expected base/cache/pack paths");std::vector<std::string> packs;for(int i=4;i<argc;++i)packs.emplace_back(argv[i]);mc::PreparedResourcePacks prepared;prepared.assetDirectory="unchanged";
            if(!mc::prepareResourcePacks(argv[2],packs,argv[3],prepared,error)){if(prepared.assetDirectory!="unchanged")throw std::runtime_error("Failed reload mutated its output");throw std::runtime_error(error);}
            mc::Assets assets;mc::UnicodeFont font;mc::ItemIconAtlas items;mc::AnimationAtlasCache animations,heldAnimations;mc::FirstPersonAssets held;
            if(!assets.load(prepared.assetDirectory,error)||!font.load(prepared.assetDirectory,error)||!items.load(prepared.assetDirectory,error)||!animations.load(prepared.assetDirectory,error)||!held.load(prepared.assetDirectory,error)||!heldAnimations.load(prepared.assetDirectory,error,"first_person"))throw std::runtime_error(error);
            if(!held.worldModelsReady()||!held.equipmentModelsReady()||!held.itemModel(1,0,mc::ItemModelTransform::None).supported)throw std::runtime_error("Prepared held-item models are incomplete");
            std::cout<<prepared.assetDirectory<<'\n';std::cout<<assets.atlasWidth()<<' '<<assets.atlasHeight()<<' '<<prepared.decodedImages<<' '<<font.charWidth('A')<<'\n';std::cout<<prepared.rebakedModels<<' '<<held.atlasWidth()<<' '<<held.atlasHeight()<<'\n';return 0;
        }
        throw std::runtime_error("Unknown resource pack test command");
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
