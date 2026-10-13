#include "mc/platform.hpp"
#include "mc/renderer.hpp"
#include "mc/assets.hpp"
#include "mc/world.hpp"
#include "mc/gl_api.hpp"
#include "mc/nbt.hpp"
#include "mc/game.hpp"
#include "mc/render_fidelity.hpp"
#include "panorama_gpu_oracle.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <stdexcept>
#include <vector>

namespace {
std::vector<unsigned char> pixels() {
    std::vector<unsigned char> result(1280 * 720 * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, 1280, 720, GL_RGB, GL_UNSIGNED_BYTE, result.data());
    return result;
}
} // namespace

int main(int argc, char **argv) {
    try {
        std::string error;
        mc::Assets assets;
        if (!assets.load("assets", error))
            throw std::runtime_error(error);
        mc::World world;
        world.generatePreview(5);
        for (int x = 49; x <= 61; ++x)
            for (int z = -60; z <= -50; ++z)
                world.setBlock(x, 80, z, mc::blockState(5));
        world.setBlock(53, 81, -55, mc::blockState(54, 2));
        world.setBlock(54, 81, -55, mc::blockState(54, 2));
        world.setBlock(57, 81, -55, mc::blockState(144, 1));
        world.setBlock(59, 81, -55, mc::blockState(176, 8));
        mc::nbt::Tag skull;
        skull.type = 10;
        skull.compound["SkullType"] = mc::nbt::integer(1, 2);
        skull.compound["Rot"] = mc::nbt::integer(1, 8);
        auto skullData = mc::nbt::encode(skull);
        mc::nbt::Tag banner;
        banner.type = 10;
        banner.compound["Base"] = mc::nbt::integer(3, 2);
        auto bannerData = mc::nbt::encode(banner);
        mc::ChestLidState lid;
        lid.current = lid.previous = .75f;
        std::vector<mc::RenderTile> tiles{{53, 81, -55, nullptr, lid},
                                          {54, 81, -55, nullptr, lid},
                                          {57, 81, -55, &skullData, {}},
                                          {59, 81, -55, &bannerData, {}}};
        std::vector<mc::RenderEntity> entities(2);
        entities[0].kind = 2;
        entities[0].itemId = 264;
        entities[0].itemCount = 50;
        entities[0].x = 54.5;
        entities[0].y = 81.5;
        entities[0].z = -57;
        entities[0].ageTicks = 30;
        entities[1].kind = 3;
        entities[1].xpValue = 7;
        entities[1].x = 56.5;
        entities[1].y = 81.5;
        entities[1].z = -57;
        entities[1].ageTicks = 30;
        mc::Window window;
        if (!window.create(1280, 720, L"NativeVoxel rendering verification", true, error))
            throw std::runtime_error(error);
        mc::Renderer renderer;
        if (!renderer.initialize(assets, error))
            throw std::runtime_error(error);
        window.setVsync(false);
        mc::Camera camera{56, 84, -63, 0, 18, 75};
        auto started = std::chrono::steady_clock::now();
        int frames = 0;
        while (window.pump()) {
            renderer.render(world, camera, 1280, 720, 0, 6000, entities, {}, tiles);
            renderer.beginUI(1280, 720);
            renderer.rect(22, 22, 540, 90, 0x101a27d9);
            renderer.text("NATIVEVOXEL / C++ / 1.8.9", 38, 36, 3, 0xffffffff);
            renderer.text("Vanilla assets | GPU meshes | Shadows | AO", 38, 78, 2, 0xa8c6dbff);
            renderer.endUI();
            ++frames;
            double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            if ((frames > 30 && !renderer.stats().pendingMeshes) || seconds > 30)
                break;
            window.swap();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        renderer.render(world, camera, 1280, 720, 0, 6000, entities, {}, tiles);
        auto baseline = pixels();
        mc::FirstPersonPose hand;
        hand.itemId = 95;
        hand.metadata = 14;
        renderer.setFirstPerson(hand);
        renderer.render(world, camera, 1280, 720, 0, 6000, entities, {}, tiles);
        auto held = pixels();
        std::size_t changed = 0;
        for (std::size_t i = 0; i < held.size(); i += 3)
            if (held[i] != baseline[i] || held[i + 1] != baseline[i + 1] || held[i + 2] != baseline[i + 2])
                ++changed;
        if (changed < 3000)
            throw std::runtime_error("First-person translucent mesh was not visible");
        GLboolean depthWrite = GL_FALSE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
        if (depthWrite != GL_TRUE)
            throw std::runtime_error("First-person pass leaked its depth-write state");
        renderer.hideFirstPerson();
        renderer.render(world, camera, 1280, 720, 0, 6000, entities, {}, tiles);
        if (pixels() != baseline)
            throw std::runtime_error("First-person GL state changed the next world frame");
        for(float pole:{-90.f,90.f}) {
            auto extreme=camera;extreme.pitch=pole;
            renderer.render(world,extreme,1280,720,0,6000,entities,{},tiles);
            const auto atPole=pixels();
            extreme.pitch=pole>0?89.999f:-89.999f;
            renderer.render(world,extreme,1280,720,0,6000,entities,{},tiles);
            const auto nearPole=pixels();std::size_t changedAtPole=0;
            for(std::size_t i=0;i<atPole.size();i+=3)
                if(std::abs(int(atPole[i])-int(nearPole[i]))>8||std::abs(int(atPole[i+1])-int(nearPole[i+1]))>8||std::abs(int(atPole[i+2])-int(nearPole[i+2]))>8)++changedAtPole;
            if(changedAtPole>1280u*720u/100u)
                throw std::runtime_error("World projection jumps at the pitch limit");
        }
        renderer.menuBackground(1280,720,0);
        const auto panorama=pixels();std::size_t black=0;
        for(std::size_t i=0;i<panorama.size();i+=3)if(panorama[i]==0&&panorama[i+1]==0&&panorama[i+2]==0)++black;
        if(black>1280u*720u/100u)throw std::runtime_error("Panorama has uncovered black/void pixels");
        if(!renderer.screenshot("build/menu-fidelity.ppm",1280,720,error))throw std::runtime_error(error);
        if(std::ifstream("build/panorama_reference/case0.trace"))
            for(int i=0;i<4;++i)verifyPanoramaJava(renderer,"build/panorama_reference/case"+std::to_string(i)+".trace");
        for(int shader=0;shader<24;++shader){
            renderer.options.secretShader=shader;
            renderer.render(world,camera,1280,720,0,6000,entities,{},tiles);
            if(!renderer.stats().secretShaderError.empty()||!renderer.stats().secretShaderPasses)
                throw std::runtime_error("Super Secret Settings shader "+std::to_string(shader)+": "+renderer.stats().secretShaderError);
        }
        renderer.options.secretShader=-1;
        renderer.render(world,camera,1280,720,0,6000,entities,{},tiles);
        if(pixels()!=baseline)throw std::runtime_error("Super Secret Settings leaked into the normal world frame");
        std::cout<<"All 24 original Super Secret Settings pipelines rendered; normal frame restored exactly\n";
        auto markedSkin=std::make_shared<mc::PlayerSkin>();
        markedSkin->textureKey="test:inventory-direction";markedSkin->revision=1;
        markedSkin->image.pixels.resize(64*64*4,0);
        auto colorSkin=[&](int x,int y,int width,int height,unsigned char r,unsigned char g,unsigned char b){
            for(int row=y;row<y+height;++row)for(int col=x;col<x+width;++col){const auto at=(row*64+col)*4;
                markedSkin->image.pixels[at]=r;markedSkin->image.pixels[at+1]=g;markedSkin->image.pixels[at+2]=b;markedSkin->image.pixels[at+3]=255;}
        };
        colorSkin(0,0,32,16,180,180,180);colorSkin(0,16,64,16,180,180,180);
        colorSkin(8,0,8,8,0,255,0);colorSkin(16,0,8,8,0,0,255);colorSkin(8,8,8,8,255,0,0);
        renderer.setPlayerPreview(markedSkin,{});
        glClearColor(.1f,.1f,.1f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        renderer.beginUI(1280,720);
        const auto up=mc::visual::inventoryPreviewRotation(0,40),down=mc::visual::inventoryPreviewRotation(0,-40);
        renderer.previewEntity(340,560,250,up.yaw,up.pitch);renderer.previewEntity(940,560,250,down.yaw,down.pitch);
        renderer.text("Cursor above",260,200,2);renderer.text("Cursor below",860,200,2);
        renderer.endUI();
        const auto previews=pixels();unsigned green[2]{},blue[2]{};
        for(int y=0;y<720;++y)for(int x=0;x<1280;++x){const auto at=(y*1280+x)*3;const int side=x<640?0:1;
            if(previews[at+1]>64&&previews[at+1]>previews[at]*2&&previews[at+1]>previews[at+2]*2)++green[side];
            if(previews[at+2]>64&&previews[at+2]>previews[at]*2&&previews[at+2]>previews[at+1]*2)++blue[side];
        }
        if(green[1]<=green[0]+100||blue[0]<=blue[1]+100)throw std::runtime_error("Inventory preview vertical mouse direction is inverted");
        if(!renderer.screenshot("build/inventory-direction.ppm",1280,720,error))throw std::runtime_error(error);
        renderer.setPlayerPreview({},{});
        renderer.setFirstPerson(hand);
        renderer.render(world, camera, 1280, 720, 0, 6000, entities, {}, tiles);
        auto &s = renderer.stats();
        if (!renderer.screenshot(argc > 1 ? argv[1] : "build/render-smoke.ppm", 1280, 720, error))
            throw std::runtime_error(error);
        std::cout << "GPU=" << s.gpu << "\nFrames=" << frames << " visible=" << s.visibleSections
                  << " loaded=" << s.loadedSections << " triangles=" << s.triangles << " pending=" << s.pendingMeshes
                  << " CPU frame ms=" << s.frameMs << '\n';
        std::cout << "First-person changed pixels=" << changed << "; next world frame restored exactly\n";
        std::cout << "Both pitch poles continuous; panorama has no uncovered area\n";
        std::cout << "Inventory preview follows vertical mouse direction (colored head face regression)\n";
        GLenum glError = glGetError();
        if (glError != GL_NO_ERROR)
            throw std::runtime_error("OpenGL error " + std::to_string(glError));
        if (s.triangles == 0 || s.pendingMeshes != 0)
            throw std::runtime_error("Terrain did not finish building");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
