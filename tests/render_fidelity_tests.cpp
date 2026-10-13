#include "mc/render_fidelity.hpp"
#include "mc/fov.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void close(float a,float b){require(std::abs(a-b)<2e-4f,"camera basis/projection lost orthogonality");}
}
int main(){
    try {
        using namespace mc::gfx;
        unsigned cases=0;
        for(float yaw:{-1080.f,-180.f,-90.f,0.f,37.f,90.f,180.f,1080.f})
            for(float pitch:{-90.f,-89.999f,-45.f,0.f,45.f,89.999f,90.f}) {
                const auto basis=cameraBasis(yaw,pitch);
                close(dot(basis.forward,basis.forward),1);close(dot(basis.right,basis.right),1);close(dot(basis.up,basis.up),1);
                close(dot(basis.forward,basis.right),0);close(dot(basis.forward,basis.up),0);close(dot(basis.right,basis.up),0);
                const Vec3 eye{13.25f,7.5f,-5.75f};const auto view=cameraView(eye,basis);
                const auto vp=perspective(70,16.f/9,.05f,256)*view;
                for(float entry:vp.v)require(std::isfinite(entry),"non-finite camera matrix at pitch limit");
                const auto center=eye+basis.forward*5.f;
                const auto project=[&](int row){return vp.v[row]*center.x+vp.v[4+row]*center.y+vp.v[8+row]*center.z+vp.v[12+row];};
                close(project(0),0);close(project(1),0);close(project(3),5);
                const Frustum frustum(vp);
                require(frustum.visible(center-Vec3{.1f,.1f,.1f},center+Vec3{.1f,.1f,.1f}),"visible object culled at pitch limit");
                const auto behind=eye-basis.forward*5.f;
                require(!frustum.visible(behind-Vec3{.1f,.1f,.1f},behind+Vec3{.1f,.1f,.1f}),"object behind camera is visible");
                ++cases;
            }
        close(mc::visual::brightness(0,0),0);close(mc::visual::brightness(15,0),1);
        close(mc::visual::brightness(0,-1),.1f);
        auto daylight=mc::visual::lightmap(1,0),night=mc::visual::lightmap(.2f,0),bright=mc::visual::lightmap(.2f,1);
        for(int i=0;i<256;++i)for(int c=0;c<3;++c){require(bright[i*4+c]>=night[i*4+c],"gamma reduces brightness");require(daylight[i*4+c]>=night[i*4+c],"daylight darker than night");}
        require(mc::visual::neighborBrightness(44)&&mc::visual::neighborBrightness(126)&&mc::visual::neighborBrightness(182),"slabs omit neighbour light sampling");
        require(!mc::visual::neighborBrightness(1),"opaque stone must retain own light sampling");
        for(float anchor:{15.f,25.f}) {
            const auto above=mc::visual::inventoryPreviewRotation(0,anchor-(anchor-40));
            const auto below=mc::visual::inventoryPreviewRotation(0,anchor-(anchor+40));
            require(above.pitch<0&&below.pitch>0,"inventory preview looks away from vertical mouse direction");
            close(above.pitch,-15.707963f);close(below.pitch,15.707963f);
        }
        close(mc::visual::inventoryPreviewRotation(0,0).pitch,0);
        mc::visual::FovTracker fov;
        const float sprint=mc::visual::playerFovMultiplier(double(.1f),.1f,true,false,false,0);
        close(sprint,1.15f);fov.tick(sprint);
        close(fov.multiplier(0),1);close(fov.multiplier(1),1.075f);close(fov.multiplier(.5f),1.0375f);
        for(int i=0;i<24;++i)fov.tick(sprint);
        close(fov.angle(70,1,true,false,false,0),80.5f);
        close(fov.angle(110,.5f,false,false,false,0),70);
        close(fov.angle(110,.5f,false,true,false,0),60);
        close(mc::visual::playerFovMultiplier(.14,.1f,false,false,false,0),1.2f);
        close(mc::visual::playerFovMultiplier(.1,.1f,false,false,true,20),.85f);
        close(mc::visual::playerFovMultiplier(.1,0,false,false,false,0),1);
        require(fov.angle(70,.5f,true,false,true,20)<fov.angle(70,.5f,true,false,false,0),"death FOV unchanged");
        std::cout<<"PASS: "<<cases<<" camera orientations including both pitch poles; original brightness bounds and slab sampling\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
