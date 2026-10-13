#include "mc/projectiles.hpp"
#include "mc/entity_dimensions.hpp"
#include "mc/potions.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace mc::projectiles {
namespace {
constexpr double Pi=3.14159265358979323846;
std::uint32_t randomBits(ProjectileState& state,int bits){state.randomState=(state.randomState*0x5deece66dULL+11)&((1ULL<<48)-1);return std::uint32_t(state.randomState>>(48-bits));}
float randomFloat(ProjectileState& state){return float(randomBits(state,24))/16777216.f;}
double randomDouble(ProjectileState& state){const auto high=randomBits(state,26),low=randomBits(state,27);return double((std::uint64_t(high)<<27)+low)/9007199254740992.;}
int randomInt(ProjectileState& state,int bound){if((bound&-bound)==bound)return int((std::uint64_t(bound)*randomBits(state,31))>>31);for(;;){const auto bits=randomBits(state,31),value=bits%unsigned(bound);if(std::uint64_t(bits)-value+unsigned(bound-1)<0x80000000ULL)return int(value);}}
double javaAtan2(double y,double x){
    const double length=x*x+y*y;if(std::isnan(length))return length;
    const bool negativeY=y<0,negativeX=x<0;if(negativeY)y=-y;if(negativeX)x=-x;const bool swapped=y>x;if(swapped)std::swap(x,y);
    double value=length;std::uint64_t bits;std::memcpy(&bits,&value,8);bits=6910469410427058090ULL-(bits>>1);std::memcpy(&value,&bits,8);value=value*(1.5-.5*length*value*value);x*=value;y*=value;
    const std::uint64_t biasBits=4805340802404319232ULL;double bias;std::memcpy(&bias,&biasBits,8);const double rounded=bias+y;std::memcpy(&bits,&rounded,8);const unsigned index=unsigned(bits);
    static const auto tables=[] {std::array<std::array<double,257>,2> out{};for(int i=0;i<257;++i){out[0][i]=std::asin(double(i)/256.);out[1][i]=std::cos(out[0][i]);}return out;}();
    if(index>256)return std::atan2(negativeY?-y:y,negativeX?-x:x);
    const double residual=y*tables[1][index]-x*(rounded-bias);double angle=tables[0][index]+(6+residual*residual)*residual*.16666666666666666;
    if(swapped)angle=Pi/2-angle;if(negativeX)angle=Pi-angle;if(negativeY)angle=-angle;return angle;
}
void angles(Entity& entity,bool initialize){
    auto& state=*entity.projectile;const float horizontal=float(std::sqrt(entity.motionX*entity.motionX+entity.motionZ*entity.motionZ));
    const float yaw=float(javaAtan2(entity.motionX,entity.motionZ)*180./Pi),pitch=float(javaAtan2(entity.motionY,double(horizontal))*180./Pi);
    if(initialize){entity.yaw=state.previousYaw=yaw;entity.pitch=state.previousPitch=pitch;return;}
    while(pitch-state.previousPitch< -180)state.previousPitch-=360;
    while(pitch-state.previousPitch>=180)state.previousPitch+=360;
    while(yaw-state.previousYaw< -180)state.previousYaw-=360;
    while(yaw-state.previousYaw>=180)state.previousYaw+=360;
    entity.yaw=state.previousYaw+(yaw-state.previousYaw)*.2f;entity.pitch=state.previousPitch+(pitch-state.previousPitch)*.2f;
}
Aabb bounds(const Entity& entity){const double width=entity.type==60?.5:.25;return {entity.x-width/2,entity.y,entity.z-width/2,entity.x+width/2,entity.y+width,entity.z+width/2};}
bool water(const World& world,const Aabb& box){for(int x=floorBlock(box.minX);x<floorBlock(box.maxX+1);++x)for(int y=floorBlock(box.minY);y<floorBlock(box.maxY+1);++y)for(int z=floorBlock(box.minZ);z<floorBlock(box.maxZ+1);++z){const auto id=blockId(world.getBlock(x,y,z));if(id==8||id==9)return true;}return false;}
double intercept(const Aabb& box,double x,double y,double z,double dx,double dy,double dz){
    double near=0,far=1;const double from[]={x,y,z},direction[]={dx,dy,dz},low[]={box.minX,box.minY,box.minZ},high[]={box.maxX,box.maxY,box.maxZ};
    for(int axis=0;axis<3;++axis){if(std::abs(direction[axis])<1e-12){if(from[axis]<low[axis]||from[axis]>high[axis])return 2;continue;}double a=(low[axis]-from[axis])/direction[axis],b=(high[axis]-from[axis])/direction[axis];if(a>b)std::swap(a,b);near=std::max(near,a);far=std::min(far,b);if(near>far)return 2;}return near;
}
void sound(Game& game,const Entity& entity,const char* name,float volume,float pitch){game.soundEvents.push_back({name,entity.x,entity.y,entity.z,volume,pitch});if(game.soundEvents.size()>256)game.soundEvents.pop_front();}
void particle(Game& game,int type,double x,double y,double z,double vx=0,double vy=0,double vz=0,int item=0){ParticleEvent event;event.type=event.wireType=type;event.x=float(x);event.y=float(y);event.z=float(z);event.offsetX=float(vx);event.offsetY=float(vy);event.offsetZ=float(vz);event.speed=1;event.tick=game.ticks;if(item)event.data={item,0};game.particleEvents.push_back(std::move(event));if(game.particleEvents.size()>4096)game.particleEvents.pop_front();}
int enchantment(const ItemStack& item,int id){try{const auto root=nbt::parse(item.nbt);const auto* list=root.get("ench");if(list&&list->type==9&&list->subtype==10)for(const auto& value:list->list){const auto* key=value.get("id"),*level=value.get("lvl");if(key&&level&&key->integer==id)return int(level->integer);}}catch(const net::ProtocolError&){}return 0;}
void damage(Game& game,int id,float amount){
    auto status=[&](double before,double after){if(after<before)game.consumePacket({0x1a,net::Writer().i32(id).i8(after<=0?3:2).data});};
    if(id==game.entityId){const auto before=game.health;if(!game.invulnerable)game.health=std::max(0.f,game.health-amount);status(before,game.health);return;}
    const auto found=game.entities.find(id);if(found==game.entities.end())return;auto& hp=found->second.metadata[6];if(hp.type!=3){hp.type=3;hp.number=20;}const auto before=hp.number;hp.number=std::max(0.,hp.number-amount);status(before,hp.number);
}
}

bool supported(int type)noexcept{return type==60||type==61||type==62||type==65||type==73||type==75||type==90;}
void initialize(Entity& entity){if(!supported(entity.type)||entity.kind!=EntityKind::Object||entity.projectile)return;entity.projectile.emplace();entity.projectile->ownerId=entity.type==60||entity.type==90?entity.objectData:-1;if(entity.type==90)entity.projectile->blockState=-1;}
void velocity(Entity& entity,double x,double y,double z){initialize(entity);entity.motionX=x;entity.motionY=y;entity.motionZ=z;if(!entity.projectile)return;auto& state=*entity.projectile;
    if(entity.type==90){state.clientMotionX=x;state.clientMotionY=y;state.clientMotionZ=z;}
    else if(state.previousPitch==0&&state.previousYaw==0){angles(entity,true);state.groundTicks=0;}
}
void correctPosition(Entity& entity){if(!entity.projectile)return;auto& state=*entity.projectile;if(entity.type==90){entity.motionX=state.clientMotionX;entity.motionY=state.clientMotionY;entity.motionZ=state.clientMotionZ;return;}
    entity.x=entity.targetX;entity.y=entity.targetY;entity.z=entity.targetZ;entity.yaw=entity.targetYaw;entity.pitch=entity.targetPitch;entity.interpolationTicks=0;
}
int fishingHook(const Game& game,int owner){for(const auto& pair:game.entities)if(pair.second.kind==EntityKind::Object&&pair.second.type==90&&pair.second.objectData==owner&&(!pair.second.projectile||!pair.second.projectile->dead))return pair.first;return -1;}

Entity launch(const Game& game,int id,const ItemStack& stack,float bowCharge,std::uint64_t seed){
    Entity entity;entity.id=id;entity.kind=EntityKind::Object;
    switch(stack.id){case 261:entity.type=60;break;case 332:entity.type=61;break;case 344:entity.type=62;break;case 368:entity.type=65;break;case 373:entity.type=73;break;case 384:entity.type=75;break;case 346:entity.type=90;break;default:throw std::invalid_argument("Unsupported projectile item");}
    entity.objectData=entity.type==73?stack.damage:game.entityId;initialize(entity);auto& state=*entity.projectile;state.ownerId=game.entityId;state.randomState=(seed^0x5deece66dULL)&((1ULL<<48)-1);
    entity.equipment[0]=entity.type==60?ItemStack{262,1,0,{0}}:stack;entity.equipment[0].count=1;
    entity.yaw=game.player.yaw;entity.pitch=game.player.pitch;const float yaw=entity.yaw/180.f*float(Pi),pitch=entity.pitch/180.f*float(Pi);
    entity.x=game.player.x-double(javaCos(yaw)*.16f);entity.y=game.player.eyeY()-.10000000149011612;entity.z=game.player.z-double(javaSin(yaw)*.16f);
    const float scale=entity.type==60?1.f:.4f;const float offset=entity.type==73||entity.type==75?-20.f:0.f;
    double x=double(-javaSin(yaw)*javaCos(pitch)*scale),y=double(-javaSin((entity.pitch+offset)/180.f*float(Pi))*scale),z=double(javaCos(yaw)*javaCos(pitch)*scale);
    const float length=float(std::sqrt(x*x+y*y+z*z));x/=double(length);y/=double(length);z/=double(length);
    std::array<double,3> gaussian{};int at=0;while(at<3){double a,b,square;do{a=2*randomDouble(state)-1;b=2*randomDouble(state)-1;square=a*a+b*b;}while(square>=1||square==0);const double factor=std::sqrt(-2*std::log(square)/square);gaussian[at++]=a*factor;if(at<3)gaussian[at++]=b*factor;}
    const float speed=entity.type==60?std::clamp(bowCharge,0.f,1.f)*3.f:entity.type==73?.5f:entity.type==75?.7f:1.5f;
    entity.motionX=(x+gaussian[0]*.007499999832361937)*speed;entity.motionY=(y+gaussian[1]*.007499999832361937)*speed;entity.motionZ=(z+gaussian[2]*.007499999832361937)*speed;
    angles(entity,true);entity.previousX=entity.targetX=entity.x;entity.previousY=entity.targetY=entity.y;entity.previousZ=entity.targetZ=entity.z;
    state.clientMotionX=entity.motionX;state.clientMotionY=entity.motionY;state.clientMotionZ=entity.motionZ;
    if(entity.type==60){const int power=enchantment(stack,48);if(power>0)state.damage+=power*.5+.5;state.knockback=enchantment(stack,49);state.pickupMode=game.creativeMode||enchantment(stack,51)>0?2:1;entity.metadata[16].type=0;entity.metadata[16].integer=bowCharge>=1?1:0;if(enchantment(stack,50)>0)entity.metadata[0].integer|=1;}
    return entity;
}

void tick(Game& game,Entity& entity){
    if(!entity.projectile||entity.projectile->dead)return;auto& state=*entity.projectile;
    // Entity.onEntityUpdate kills a void entity before this update's motion,
    // but its subclass still completes the current update.
    if(entity.y< -64)state.dead=true;
    state.previousYaw=entity.yaw;state.previousPitch=entity.pitch;
    if(entity.type==90&&entity.interpolationTicks>0)return;
    if(entity.type==90&&!game.online){const double dx=entity.x-game.player.x,dy=entity.y-game.player.y,dz=entity.z-game.player.z;
        if(game.health<=0||game.inventory[36+std::clamp(game.selectedSlot,0,8)].id!=346||dx*dx+dy*dy+dz*dz>1024){state.dead=true;return;}
        if(state.caughtEntityId>=0){const auto caught=game.entities.find(state.caughtEntityId);if(caught!=game.entities.end()){const auto& target=caught->second;entity.x=target.x;entity.y=target.y+1.44;entity.z=target.z;return;}state.caughtEntityId=-1;}
    }
    if(state.shake>0)--state.shake;
    if(state.inGround){const auto block=game.world.getBlock(state.blockX,state.blockY,state.blockZ);if(blockId(block)==blockId(BlockState(state.blockState))&&(entity.type!=60||block==state.blockState)){if(++state.groundTicks>=1200)state.dead=true;return;}
        state.inGround=false;entity.motionX*=double(randomFloat(state)*.2f);entity.motionY*=double(randomFloat(state)*.2f);entity.motionZ*=double(randomFloat(state)*.2f);state.groundTicks=state.airTicks=0;
    }
    ++state.airTicks;
    const bool wet=game.world.waterAcceleration(bounds(entity).expand(0,-.4000000059604645,0).expand(-.001,-.001,-.001)).inside;
    auto hit=game.world.raycast(entity.x,entity.y,entity.z,entity.x+entity.motionX,entity.y+entity.motionY,entity.z+entity.motionZ,false,entity.type==60,false);
    double closest=hit.hit?intercept(Aabb{hit.hitX-1e-9,hit.hitY-1e-9,hit.hitZ-1e-9,hit.hitX+1e-9,hit.hitY+1e-9,hit.hitZ+1e-9},entity.x,entity.y,entity.z,entity.motionX,entity.motionY,entity.motionZ):2;
    int hitEntity=-1;
    if(!game.online){
        for(const auto& pair:game.entities){const auto& target=pair.second;if(pair.first==entity.id||(pair.first==state.ownerId&&state.airTicks<5)||(target.kind!=EntityKind::Mob&&target.kind!=EntityKind::Player))continue;
            const auto size=entityDimensions(target.kind==EntityKind::Player?0:1,target.type);const Aabb box{target.x-size.width/2-.3,target.y-.3,target.z-size.width/2-.3,target.x+size.width/2+.3,target.y+size.height+.3,target.z+size.width/2+.3};
            const double distance=intercept(box,entity.x,entity.y,entity.z,entity.motionX,entity.motionY,entity.motionZ);if(distance<closest){closest=distance;hitEntity=target.id;}
        }
        if(state.ownerId!=game.entityId||state.airTicks>=5){const double distance=intercept(game.player.bounds().expand(.3,.3,.3),entity.x,entity.y,entity.z,entity.motionX,entity.motionY,entity.motionZ);if(distance<closest){closest=distance;hitEntity=game.entityId;}}
    }
    if(hitEntity>=0){state.hitEntityId=hitEntity;state.impactPending=true;if(entity.type==90)state.caughtEntityId=hitEntity;}
    else if(hit.hit&&blockId(game.world.getBlock(hit.x,hit.y,hit.z))!=90){
        if(entity.type==60||entity.type==90){state.inGround=true;
            // EntityFishHook only changes inGround here. Its legacy inTile
            // field stays null; retaining the arrow's block would pin it forever.
            if(entity.type==60){state.blockX=hit.x;state.blockY=hit.y;state.blockZ=hit.z;state.blockState=game.world.getBlock(hit.x,hit.y,hit.z);state.shake=7;entity.motionX=double(float(hit.hitX-entity.x));entity.motionY=double(float(hit.hitY-entity.y));entity.motionZ=double(float(hit.hitZ-entity.z));const float length=float(std::sqrt(entity.motionX*entity.motionX+entity.motionY*entity.motionY+entity.motionZ*entity.motionZ));if(length>0){entity.x-=entity.motionX/double(length)*.05000000074505806;entity.y-=entity.motionY/double(length)*.05000000074505806;entity.z-=entity.motionZ/double(length)*.05000000074505806;}entity.metadata[16].integer&=~1;sound(game,entity,"random.bowhit",1,1.2f/(randomFloat(state)*.2f+.9f));}
        }else if(!game.online){state.impactPending=true;state.hitEntityId=-1;}
    }
    if(entity.type==90){
        if(state.inGround||state.caughtEntityId>=0)return;
        auto box=bounds(entity);const auto collisions=game.world.collisions(box.addCoord(entity.motionX,entity.motionY,entity.motionZ));double x=entity.motionX,y=entity.motionY,z=entity.motionZ;
        for(const auto& block:collisions)y=block.clipY(box,y);box=box.offset(0,y,0);for(const auto& block:collisions)x=block.clipX(box,x);box=box.offset(x,0,0);for(const auto& block:collisions)z=block.clipZ(box,z);
        box=box.offset(0,0,z);entity.x=(box.minX+box.maxX)/2;entity.y=box.minY;entity.z=(box.minZ+box.maxZ)/2;entity.onGround=y!=entity.motionY&&entity.motionY<0;const bool horizontal=x!=entity.motionX||z!=entity.motionZ;
        if(x!=entity.motionX)entity.motionX=0;if(y!=entity.motionY)entity.motionY=0;if(z!=entity.motionZ)entity.motionZ=0;angles(entity,false);
        float drag=entity.onGround||horizontal?.5f:.92f;double immersion=0;const auto current=bounds(entity);for(int slice=0;slice<5;++slice){auto sample=current;sample.minY=current.minY+.25*slice/5.;sample.maxY=current.minY+.25*(slice+1)/5.;if(water(game.world,sample))immersion+=.2;}
        entity.motionY+=.03999999910593033*(immersion*2-1);if(immersion>0){drag=float(double(drag)*.9);entity.motionY*=.8;}entity.motionX*=double(drag);entity.motionY*=double(drag);entity.motionZ*=double(drag);return;
    }
    if(entity.type==60&&(entity.metadata[16].integer&1))for(int i=0;i<4;++i)particle(game,9,entity.x+entity.motionX*i/4.,entity.y+entity.motionY*i/4.,entity.z+entity.motionZ*i/4.,-entity.motionX,-entity.motionY+.2,-entity.motionZ);
    entity.x+=entity.motionX;entity.y+=entity.motionY;entity.z+=entity.motionZ;angles(entity,false);
    const float drag=wet?(entity.type==60?.6f:.8f):.99f;const float gravity=entity.type==60||entity.type==73?.05f:entity.type==75?.07f:.03f;
    if(wet)for(int i=0;i<4;++i)particle(game,4,entity.x-entity.motionX*.25,entity.y-entity.motionY*.25,entity.z-entity.motionZ*.25,entity.motionX,entity.motionY,entity.motionZ);
    entity.motionX*=double(drag);entity.motionY*=double(drag);entity.motionZ*=double(drag);entity.motionY-=double(gravity);
}

void applyPotion(Game& game,const ItemStack& stack,double strength,int targetId){
    if(game.online)return;const bool local=targetId<0||targetId==game.entityId;Entity* target=nullptr;if(!local){const auto found=game.entities.find(targetId);if(found==game.entities.end())return;target=&found->second;}
    const bool undead=target&&target->kind==EntityKind::Mob&&(target->type==51||target->type==54||target->type==57||target->type==64);
    for(const auto& effect:potions::effects(stack)){
        if(effect.id==6||effect.id==7){const bool heals=(effect.id==6)!=undead;const float amount=float(std::floor(strength*std::ldexp(heals?4.:6.,std::clamp(effect.amplifier,0,15))+.5));if(heals){if(local)game.health=std::min(game.maxHealth,game.health+amount);else{auto& hp=target->metadata[6];if(hp.type!=3){hp.type=3;hp.number=20;}hp.number=std::min(20.,hp.number+amount);}}else damage(game,local?game.entityId:targetId,amount);}
        else {const int duration=int(strength*effect.duration+.5);if(strength<1&&duration<=20)continue;auto& effects=local?game.effects:target->effects;auto found=effects.find(effect.id);if(found==effects.end()||found->second.amplifier<effect.amplifier||(found->second.amplifier==effect.amplifier&&found->second.duration<duration))effects[effect.id]={effect.amplifier,duration,!effect.showParticles};}
    }
}

void finishTick(Game& game){
    struct Experience {int amount;double x,y,z;};std::vector<Experience> experience;
    for(auto& pair:game.entities){auto& entity=pair.second;if(!entity.projectile)continue;auto& state=*entity.projectile;
        if(!game.online&&entity.type==60&&state.inGround&&state.shake==0&&game.player.bounds().expand(1,.5,1).intersects(bounds(entity))){
            bool picked=state.pickupMode==2&&game.creativeMode;
            if(state.pickupMode==1){for(int pass=0;pass<2&&!picked;++pass)for(int slot=0;slot<36&&!picked;++slot){const int index=slot<9?slot+36:slot;auto& stack=game.inventory[index];if(pass==0&&stack.id==262&&stack.count>0&&stack.count<64){++stack.count;++game.inventorySlotRevision[index];picked=true;}else if(pass==1&&stack.empty()){stack={262,1,0,{0}};++game.inventorySlotRevision[index];picked=true;}}}
            if(picked){game.queuePickup(entity.id,game.entityId);state.dead=true;sound(game,entity,"random.pop",.2f,2);}
        }
        if(!state.impactPending)continue;state.impactPending=false;
        if(game.online)continue;
        if(entity.type==90)continue;
        if(entity.type==73){ItemStack potion=entity.equipment[0];if(potion.id!=373)potion={373,1,static_cast<std::int16_t>(entity.objectData),{0}};
            const double x=entity.previousX,y=entity.previousY,z=entity.previousZ;
            auto apply=[&](int id,double px,double py,double pz){const double dx=x-px,dy=y-py,dz=z-pz,distance=dx*dx+dy*dy+dz*dz;if(distance<16)applyPotion(game,potion,id==state.hitEntityId?1:1-std::sqrt(distance)/4,id);};
            apply(game.entityId,game.player.x,game.player.y,game.player.z);for(const auto& target:game.entities)if(target.second.kind==EntityKind::Player||target.second.kind==EntityKind::Mob)apply(target.first,target.second.x,target.second.y,target.second.z);
            game.worldEffectEvents.push_back({2002,floorBlock(x),floorBlock(y),floorBlock(z),entity.objectData,false,game.ticks});
        }else if(entity.type==75){game.worldEffectEvents.push_back({2002,floorBlock(entity.previousX),floorBlock(entity.previousY),floorBlock(entity.previousZ),0,false,game.ticks});experience.push_back({3+randomInt(state,5)+randomInt(state,5),entity.previousX,entity.previousY,entity.previousZ});}
        else if(entity.type==60){if(state.hitEntityId>=0){const float speed=float(std::sqrt(entity.motionX*entity.motionX+entity.motionY*entity.motionY+entity.motionZ*entity.motionZ));int amount=int(std::ceil(speed*state.damage));if(entity.metadata[16].integer&1)amount+=randomInt(state,amount/2+2);damage(game,state.hitEntityId,float(amount));sound(game,entity,"random.bowhit",1,1.2f/(randomFloat(state)*.2f+.9f));}}
        else if(entity.type==65&&state.ownerId==game.entityId){game.player.teleport(entity.previousX,entity.previousY,entity.previousZ);game.player.fallDistance=0;damage(game,game.entityId,5);}
        else if(entity.type==61||entity.type==62){for(int i=0;i<8;++i)particle(game,36,entity.previousX,entity.previousY,entity.previousZ,0,0,0,entity.type==61?332:344);}
        state.dead=true;
    }
    for(auto it=game.entities.begin();it!=game.entities.end();)if(it->second.projectile&&it->second.projectile->dead)it=game.entities.erase(it);else ++it;
    if(game.offlineProjectileExperience)for(const auto& drop:experience)game.offlineProjectileExperience(drop.amount,drop.x,drop.y,drop.z);
    while(game.worldEffectEvents.size()>1024)game.worldEffectEvents.pop_front();
}
} // namespace mc::projectiles
