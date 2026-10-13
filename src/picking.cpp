#include "mc/picking.hpp"
#include "mc/game.hpp"
#include "mc/entity_dimensions.hpp"
#include <algorithm>
#include <cmath>
namespace mc {
BlockPick pickBlock(const mc::World& world,const PickView& camera,double reach) {
    const float yaw=-camera.yaw*.017453292f-3.1415927f;
    const float pitch=-camera.pitch*.017453292f;
    const float h=-mc::javaCos(pitch);
    const double dx=double(mc::javaSin(yaw)*h),dy=double(mc::javaSin(pitch)),dz=double(mc::javaCos(yaw)*h);
    auto hit=world.raycast(camera.x,camera.y,camera.z,camera.x+dx*reach,camera.y+dy*reach,camera.z+dz*reach,false,false,false);
    if(!hit.hit)return {};
    double distance=std::sqrt((hit.hitX-camera.x)*(hit.hitX-camera.x)+(hit.hitY-camera.y)*(hit.hitY-camera.y)+(hit.hitZ-camera.z)*(hit.hitZ-camera.z));
    return {true,hit.x,hit.y,hit.z,hit.face,distance,float(hit.hitX-hit.x),float(hit.hitY-hit.y),float(hit.hitZ-hit.z)};
}
EntityPick pickEntity(const mc::Game& game,const PickView& camera,const BlockPick& block) {
    double reach=game.gameMode==1?6.:3.;
    const float yaw=-camera.yaw*.017453292f-3.1415927f,pitch=-camera.pitch*.017453292f;
    const float h=-mc::javaCos(pitch);
    const double direction[]={double(mc::javaSin(yaw)*h)*reach,double(mc::javaSin(pitch))*reach,double(mc::javaCos(yaw)*h)*reach};
    const double origin[]={camera.x,camera.y,camera.z};
    EntityPick result;result.distance=block.hit?std::min(block.distance,reach):reach;
    for(const auto& entry:game.entities){const auto& e=entry.second;if(e.id==game.entityId)continue;
        int kind=e.kind==mc::EntityKind::Player?0:e.kind==mc::EntityKind::Mob?1:e.kind==mc::EntityKind::Object?2:-1;
        if(kind<0)continue;
        auto dims=mc::entityDimensions(kind,e.type);if(!dims.valid||!dims.collidable||dims.multipart||dims.customBounds)continue;
        auto metadata=[&](int key){auto m=e.metadata.find(std::uint8_t(key));return m==e.metadata.end()?0:int(m->second.integer);};
        if(kind==0){auto info=game.playerList.find(e.uuid);if(info!=game.playerList.end()&&info->second.gameMode==3)continue;}
        if(kind==1){
            if((e.type==54||e.type==57)?metadata(12)!=0:metadata(12)<0){dims.width*=.5f;dims.height*=.5f;}
            if(e.type==55||e.type==62)dims.width=dims.height=.51000005f*std::max(1,metadata(16));
            if(e.type==51&&metadata(13)==1){dims.width=.72f;dims.height=2.535f;}
            if(e.type==68&&(metadata(16)&4))dims.width=dims.height=1.9975f;
            if(e.type==30){int flags=metadata(10);if(flags&16)continue;if(flags&1){dims.width*=.5f;dims.height*=.5f;}}
        }
        const double half=double(dims.width/2.f),margin=double(dims.collisionBorder);
        const double lower[]={e.x-half-margin,e.y-margin,e.z-half-margin},upper[]={e.x+half+margin,e.y+double(dims.height)+margin,e.z+half+margin};
        bool inside=true;for(int axis=0;axis<3;++axis)inside=inside&&origin[axis]>lower[axis]&&origin[axis]<upper[axis];
        if(inside){result={e.id,camera.x-e.x,camera.y-e.y,camera.z-e.z,0};continue;}
        for(int axis=0;axis<3;++axis){if(direction[axis]*direction[axis]<1.0000000116860974e-7)continue;
            for(int side=0;side<2;++side){double t=((side?upper[axis]:lower[axis])-origin[axis])/direction[axis];if(t<0||t>1)continue;
                double point[3];for(int i=0;i<3;++i)point[i]=origin[i]+direction[i]*t;
                bool valid=true;for(int i=0;i<3;++i)if(i!=axis&&(point[i]<lower[i]||point[i]>upper[i]))valid=false;
                double distance=std::sqrt((point[0]-camera.x)*(point[0]-camera.x)+(point[1]-camera.y)*(point[1]-camera.y)+(point[2]-camera.z)*(point[2]-camera.z));
                if(valid&&distance<result.distance)result={e.id,point[0]-e.x,point[1]-e.y,point[2]-e.z,distance};
            }
        }
    }
    return result;
}
}
