#include "mc/assets.hpp"
#include "mc/platform.hpp"
#include "mc/renderer.hpp"
#include "mc/gl_api.hpp"
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
std::size_t privateBytes() {
    PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof(counters);
    if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),sizeof(counters)))
        throw std::runtime_error("Cannot read process memory counters");
    return counters.PrivateUsage;
}
std::vector<std::uint8_t> column(bool empty) {
    std::vector<std::uint8_t> data(mc::World::chunkDataSize(16,true,true));
    if(!empty)for(int y=0;y<16;++y)for(int z=0;z<16;++z)for(int x=0;x<16;++x)if((x+y+z)%3==0){
        const auto index=2*((y<<8)|(z<<4)|x);const auto state=mc::blockState(y%2?20:1);
        data[index]=std::uint8_t(state);data[index+1]=std::uint8_t(state>>8);
    }
    std::fill(data.begin()+10240,data.begin()+12288,255);
    std::fill(data.begin()+12288,data.end(),1);
    return data;
}
}
int main(){try{
    std::string error;mc::Assets assets;if(!assets.load("assets",error))throw std::runtime_error(error);
    mc::Window window;if(!window.create(640,360,L"NativeVoxel roaming memory verification",true,error))throw std::runtime_error(error);
    mc::Renderer renderer;if(!renderer.initialize(assets,error))throw std::runtime_error(error);
    window.setVsync(false);renderer.options.distance=2;renderer.options.shadows=false;renderer.options.fxaa=false;
    mc::World world;const auto solid=column(false),air=column(true);mc::Camera camera{8,78,-4,0,25,70};
    auto settle=[&](){const auto start=std::chrono::steady_clock::now();do{
        if(!window.pump())throw std::runtime_error("Window closed during roaming test");
        renderer.render(world,camera,640,360,0,6000);window.swap();
        if(renderer.stats().pendingMeshes==0)return;
        if(std::chrono::steady_clock::now()-start>std::chrono::seconds(20))throw std::runtime_error("Mesh queue failed to settle");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }while(true);};
    std::size_t minimum=std::numeric_limits<std::size_t>::max(),maximum=0,peakSections=0;
    for(int stop=0;stop<160;++stop){const int cx=stop*8;
        // Retain server columns for 16 stops to ensure view-distance eviction,
        // even before an explicit server unload reaches the renderer.
        for(int dz=0;dz<2;++dz)for(int dx=0;dx<2;++dx)
            if(!world.applyChunk(cx+dx,dz,true,16,true,solid.data(),solid.size(),&error))throw std::runtime_error(error);
        camera.x=cx*16+8;settle();peakSections=std::max(peakSections,renderer.stats().loadedSections);
        if(renderer.stats().loadedSections!=4||renderer.stats().triangles==0)throw std::runtime_error("Distant section meshes were retained or visible meshes were lost");
        // Replacing all three render layers with empty data must release their
        // VBOs and the translucent sorting index data.
        for(int dz=0;dz<2;++dz)for(int dx=0;dx<2;++dx)
            if(!world.applyChunk(cx+dx,dz,true,16,true,air.data(),air.size(),&error))throw std::runtime_error(error);
        settle();if(renderer.stats().triangles!=0)throw std::runtime_error("Empty chunk replacement retained stale geometry");
        if(stop>=16)for(int dz=0;dz<2;++dz)for(int dx=0;dx<2;++dx)world.unloadChunk((stop-16)*8+dx,dz);
        if(stop>=40){glFinish();const auto bytes=privateBytes();minimum=std::min(minimum,bytes);maximum=std::max(maximum,bytes);}
    }
    world.clear();settle();if(renderer.stats().loadedSections)throw std::runtime_error("Unload retained GPU section handles");
    // Driver and allocator pools need not return every byte immediately; growth
    // across 120 equally-sized warmed stops must remain bounded.
    if(maximum-minimum>64u*1024u*1024u)throw std::runtime_error("Warmed roaming private memory grew by more than 64 MiB");
    std::cout<<"Roaming GPU memory: 160 stops / 640 columns, peak visible resident sections="<<peakSections
             <<", final sections="<<renderer.stats().loadedSections<<", warmed private bytes min="<<minimum
             <<", max="<<maximum<<", growth="<<(maximum-minimum)<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
