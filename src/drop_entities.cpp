#include "mc/drop_entities.hpp"
#include "mc/nbt.hpp"
#include "mc/registry.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace mc::drop_entities {
namespace {
bool sameTags(const ItemStack& a,const ItemStack& b){return a.nbt==b.nbt||nbt::parse(a.nbt)==nbt::parse(b.nbt);}
bool damageable(const ItemStack& item){
    if(item.empty()||registry::item(item.id).maxDamage<=0)return false;
    const auto tag=nbt::parse(item.nbt);const auto* immutable=tag.get("Unbreakable");
    return !immutable||immutable->type<1||immutable->type>4||static_cast<std::uint8_t>(immutable->integer)==0;
}
std::size_t nativeSlot(int javaSlot){return static_cast<std::size_t>(javaSlot<9?36+javaSlot:javaSlot);}
bool lava(unsigned id){return id==10||id==11;}
void hurt(State& state,int amount){
    // Entity.readFromNBT restores this flag before Item/XPOrb damage handling.
    // The retained compound also carries it through native sidecar saves.
    const auto* invulnerable=state.anvilData.get("Invulnerable");
    if(invulnerable&&invulnerable->type==1&&invulnerable->integer!=0)return;
    state.health-=amount;if(state.health<=0)state.dead=true;
}
void sound(Game& game,const char* name,float volume,float pitch){game.soundEvents.push_back({name,game.player.x,game.player.y,game.player.z,volume,pitch});if(game.soundEvents.size()>256)game.soundEvents.pop_front();}
bool loaded(const World& world,const Aabb& bounds){
    const int x0=floorDiv16(floorBlock(bounds.minX)),x1=floorDiv16(floorBlock(bounds.maxX+1));
    const int z0=floorDiv16(floorBlock(bounds.minZ)),z1=floorDiv16(floorBlock(bounds.maxZ+1));
    for(int x=x0;x<=x1;++x)for(int z=z0;z<=z1;++z)if(!world.hasChunk(x,z))return false;
    return true;
}
bool contains(const World& world,const Aabb& bounds,bool fireToo){
    for(int x=floorBlock(bounds.minX);x<floorBlock(bounds.maxX+1);++x)
        for(int y=floorBlock(bounds.minY);y<floorBlock(bounds.maxY+1);++y)
            for(int z=floorBlock(bounds.minZ);z<floorBlock(bounds.maxZ+1);++z){const auto id=blockId(world.getBlock(x,y,z));if(lava(id)||(fireToo&&id==51))return true;}
    return false;
}
void water(State& state,const World& world){
    const auto flow=world.waterAcceleration(state.bounds);
    if(!flow.loaded)state.warnings|=MissingChunks;
    if(flow.hasFlow){state.motionX+=flow.x;state.motionY+=flow.y;state.motionZ+=flow.z;}
    // EntityXPOrb's override intentionally never writes the inWater field.
    if(state.orb)return;
    if(flow.inside&&!state.inWater&&!state.firstUpdate){
        state.random.nextFloat();state.random.nextFloat();
        const float limit=1.0f+state.width*20.0f;
        for(int i=0;static_cast<float>(i)<limit;++i){state.random.nextFloat();state.random.nextFloat();state.random.nextFloat();}
        for(int i=0;static_cast<float>(i)<limit;++i){state.random.nextFloat();state.random.nextFloat();}
    }
    state.inWater=flow.inside;
}
bool fullCollision(const World& world,int x,int y,int z){
    std::vector<Aabb> boxes;world.blockCollisionBoxes(x,y,z,boxes);if(boxes.empty())return false;
    auto bounds=boxes.front();for(const auto& box:boxes){bounds.minX=std::min(bounds.minX,box.minX);bounds.minY=std::min(bounds.minY,box.minY);bounds.minZ=std::min(bounds.minZ,box.minZ);bounds.maxX=std::max(bounds.maxX,box.maxX);bounds.maxY=std::max(bounds.maxY,box.maxY);bounds.maxZ=std::max(bounds.maxZ,box.maxZ);}
    return ((bounds.maxX-bounds.minX)+(bounds.maxY-bounds.minY)+(bounds.maxZ-bounds.minZ))/3>=1;
}
bool pushOut(State& state,const World& world){
    const double midY=(state.bounds.minY+state.bounds.maxY)/2;
    const int x=floorBlock(state.x),y=floorBlock(midY),z=floorBlock(state.z);
    if(world.collisions(state.bounds).empty()&&!fullCollision(world,x,y,z))return false;
    const double dx=state.x-x,dy=midY-y,dz=state.z-z;int direction=3;double closest=9999;
    if(!fullCollision(world,x-1,y,z)&&dx<closest){closest=dx;direction=0;}
    if(!fullCollision(world,x+1,y,z)&&1-dx<closest){closest=1-dx;direction=1;}
    if(!fullCollision(world,x,y+1,z)&&1-dy<closest){closest=1-dy;direction=3;}
    if(!fullCollision(world,x,y,z-1)&&dz<closest){closest=dz;direction=4;}
    if(!fullCollision(world,x,y,z+1)&&1-dz<closest){direction=5;}
    const float speed=state.random.nextFloat()*.2f+.1f;
    if(direction==0)state.motionX=static_cast<double>(-speed);else if(direction==1)state.motionX=speed;else if(direction==3)state.motionY=speed;
    else if(direction==4)state.motionZ=static_cast<double>(-speed);else if(direction==5)state.motionZ=speed;
    return true;
}
void resetPosition(State& state){state.x=(state.bounds.minX+state.bounds.maxX)/2;state.y=state.bounds.minY;state.z=(state.bounds.minZ+state.bounds.maxZ)/2;}
void move(State& state,const World& world,bool rain){
    double dx=state.motionX,dy=state.motionY,dz=state.motionZ;
    if(state.noClip){state.bounds=state.bounds.offset(dx,dy,dz);resetPosition(state);return;}
    if(state.inWeb){state.inWeb=false;dx*=.25;dy*=.05000000074505806;dz*=.25;state.motionX=state.motionY=state.motionZ=0;}
    const auto& border=world.border();
    double minX=border.minX,maxX=border.maxX,minZ=border.minZ,maxZ=border.maxZ;
    if(state.outsideBorder){++minX;++minZ;--maxX;--maxZ;}else{--minX;--minZ;++maxX;++maxZ;}
    const bool inside=state.x>minX&&state.x<maxX&&state.z>minZ&&state.z<maxZ;
    if(state.outsideBorder&&inside)state.outsideBorder=false;else if(!state.outsideBorder&&!inside)state.outsideBorder=true;
    const auto boxes=world.collisions(state.bounds.addCoord(dx,dy,dz),!state.outsideBorder);
    const double originalX=dx,originalY=dy,originalZ=dz;
    for(const auto& box:boxes)dy=box.clipY(state.bounds,dy);
    state.bounds=state.bounds.offset(0,dy,0);
    for(const auto& box:boxes)dx=box.clipX(state.bounds,dx);
    state.bounds=state.bounds.offset(dx,0,0);
    for(const auto& box:boxes)dz=box.clipZ(state.bounds,dz);
    state.bounds=state.bounds.offset(0,0,dz);
    resetPosition(state);state.onGround=originalY!=dy&&originalY<0;
    if(state.onGround)state.fallDistance=0;else if(dy<0)state.fallDistance=static_cast<float>(static_cast<double>(state.fallDistance)-dy);
    if(originalX!=dx)state.motionX=0;
    if(originalZ!=dz)state.motionZ=0;
    if(originalY!=dy){const auto ground=blockId(world.getBlock(floorBlock(state.x),floorBlock(state.y-.20000000298023224),floorBlock(state.z)));
        if(ground==165&&state.motionY<0)state.motionY=-state.motionY;else state.motionY=0;
    }
    const auto near=state.bounds.expand(-.001,-.001,-.001);
    if(loaded(world,near)){
        for(int x=floorBlock(near.minX);x<=floorBlock(near.maxX);++x)for(int y=floorBlock(near.minY);y<=floorBlock(near.maxY);++y)for(int z=floorBlock(near.minZ);z<=floorBlock(near.maxZ);++z){
            const auto id=blockId(world.getBlock(x,y,z));
            if(id==30){state.inWeb=true;state.fallDistance=0;}
            if(id==88){state.motionX*=.4;state.motionZ*=.4;}
            if(id==81)hurt(state,1);
            if(id==90||id==119)state.warnings|=PortalTransferUnavailable;
            if(id==70||id==72||id==132||id==147||id==148)state.warnings|=BlockTriggerUnavailable;
            if(id==36)state.warnings|=MovingPistonUnavailable;
        }
    }else state.warnings|=MissingChunks;
    const bool wet=state.inWater||rain;
    if(loaded(world,near)&&contains(world,near,true)){
        hurt(state,1);if(!wet){++state.fire;if(state.fire==0)state.fire=160;}
    }else if(state.fire<=0)state.fire=-1;
    if(wet&&state.fire>0){state.random.nextFloat();state.random.nextFloat();state.fire=-1;}
}
void lavaBounce(State& state){state.motionY=.20000000298023224;
    const float x1=state.random.nextFloat(),x2=state.random.nextFloat(),z1=state.random.nextFloat(),z2=state.random.nextFloat();
    state.motionX=static_cast<double>((x1-x2)*.2f);state.motionZ=static_cast<double>((z1-z2)*.2f);state.random.nextFloat();}
}

void State::setPosition(double px,double py,double pz){
    if(!std::isfinite(px)||!std::isfinite(py)||!std::isfinite(pz)||std::abs(px)>30000512||std::abs(pz)>30000512||std::abs(py)>30000512)throw std::invalid_argument("Invalid dropped entity position");
    x=px;y=py;z=pz;const double radius=static_cast<double>(width)/2;bounds={x-radius,y,z-radius,x+radius,y+static_cast<double>(height),z+radius};
}
State item(int id,const ItemStack& stack,double x,double y,double z,worldgen::JavaRandom& mathRandom,std::int64_t entitySeed){
    State state;state.id=id;state.item=stack;state.random.setSeed(entitySeed);state.hoverStart=static_cast<float>(mathRandom.nextDouble()*3.14159265358979323846*2);
    state.setPosition(x,y,z);state.previousX=x;state.previousY=y;state.previousZ=z;state.yaw=static_cast<float>(mathRandom.nextDouble()*360);
    state.motionX=static_cast<double>(static_cast<float>(mathRandom.nextDouble()*.20000000298023224-.10000000149011612));state.motionY=.20000000298023224;
    state.motionZ=static_cast<double>(static_cast<float>(mathRandom.nextDouble()*.20000000298023224-.10000000149011612));state.pickupDelay=10;return state;
}
State experience(int id,int amount,double x,double y,double z,worldgen::JavaRandom& mathRandom,std::int64_t entitySeed){
    State state;state.id=id;state.orb=true;state.value=amount;state.width=state.height=.5f;state.random.setSeed(entitySeed);state.setPosition(x,y,z);state.previousX=x;state.previousY=y;state.previousZ=z;
    state.yaw=static_cast<float>(mathRandom.nextDouble()*360);
    state.motionX=static_cast<double>(static_cast<float>(mathRandom.nextDouble()*.20000000298023224-.10000000149011612)*2.f);
    state.motionY=static_cast<double>(static_cast<float>(mathRandom.nextDouble()*.2)*2.f);
    state.motionZ=static_cast<double>(static_cast<float>(mathRandom.nextDouble()*.20000000298023224-.10000000149011612)*2.f);return state;
}
void throwFromPlayer(State& state,const Player& player,bool around,bool trace,const std::string& playerName,worldgen::JavaRandom& random){
    const float eyeHeight=player.sneaking?1.54f:1.62f;
    state.setPosition(player.x,player.y-.30000001192092896+static_cast<double>(eyeHeight),player.z);
    state.previousX=state.x;state.previousY=state.y;state.previousZ=state.z;state.pickupDelay=40;
    if(trace)state.thrower=playerName;
    constexpr float pi=3.14159265358979323846f;
    if(around){const float speed=random.nextFloat()*.5f,angle=random.nextFloat()*pi*2.f;state.motionX=static_cast<double>(-javaSin(angle)*speed);state.motionZ=static_cast<double>(javaCos(angle)*speed);state.motionY=.20000000298023224;}
    else{const float yaw=player.yaw/180.f*pi,pitch=player.pitch/180.f*pi;state.motionX=static_cast<double>(-javaSin(yaw)*javaCos(pitch)*.3f);state.motionZ=static_cast<double>(javaCos(yaw)*javaCos(pitch)*.3f);state.motionY=static_cast<double>(-javaSin(pitch)*.3f+.1f);
        const float angle=random.nextFloat()*pi*2.f,speed=.02f*random.nextFloat();state.motionX+=std::cos(static_cast<double>(angle))*static_cast<double>(speed);
        const float first=random.nextFloat(),second=random.nextFloat();state.motionY+=static_cast<double>((first-second)*.1f);state.motionZ+=std::sin(static_cast<double>(angle))*static_cast<double>(speed);
    }
}
void tick(State& state,const World& world,const Player* attractPlayer,bool wetFromRain,const std::function<void(State&)>& searchForMerge){
    if(state.dead)return;
    if(!world.hasChunk(floorDiv16(floorBlock(state.x)),floorDiv16(floorBlock(state.z)))){state.warnings|=MissingChunks;return;}
    if(!std::isfinite(state.motionX)||!std::isfinite(state.motionY)||!std::isfinite(state.motionZ)||std::abs(state.motionX)>512||std::abs(state.motionY)>512||std::abs(state.motionZ)>512)throw std::invalid_argument("Invalid dropped entity motion");
    ++state.ticks;state.searchForMerge=false;state.previousX=state.x;state.previousY=state.y;state.previousZ=state.z;
    water(state,world);
    if(state.fire>0){if(state.fire%20==0)hurt(state,1);--state.fire;}
    if(contains(world,state.bounds.expand(-.10000000149011612,-.4000000059604645,-.10000000149011612),false)){hurt(state,4);state.fire=std::max(state.fire,300);state.fallDistance*=.5f;}
    if(state.y< -64)state.dead=true;
    state.firstUpdate=false;
    if(state.pickupDelay>0&&(state.orb||state.pickupDelay!=32767))--state.pickupDelay;
    state.motionY-=state.orb?.029999999329447746:.03999999910593033;
    if(state.orb){
        if(lava(blockId(world.getBlock(floorBlock(state.x),floorBlock(state.y),floorBlock(state.z)))))lavaBounce(state);
        pushOut(state,world);
        if(state.targetColor<state.color-20+state.id%100){
            const double dx=attractPlayer?attractPlayer->x-state.x:0,dy=attractPlayer?attractPlayer->y-state.y:0,dz=attractPlayer?attractPlayer->z-state.z:0;
            if(!state.hasTarget||dx*dx+dy*dy+dz*dz>64)state.hasTarget=attractPlayer&&!attractPlayer->spectator&&!attractPlayer->dead&&dx*dx+dy*dy+dz*dz<64;
            state.targetColor=state.color;
        }
        if(!attractPlayer||attractPlayer->spectator)state.hasTarget=false;
        if(state.hasTarget){const double dx=(attractPlayer->x-state.x)/8,dy=(attractPlayer->eyeY()-state.y)/8,dz=(attractPlayer->z-state.z)/8;
            const double length=std::sqrt(dx*dx+dy*dy+dz*dz);double strength=1-length;
            if(strength>0){strength*=strength;state.motionX+=dx/length*strength*.1;state.motionY+=dy/length*strength*.1;state.motionZ+=dz/length*strength*.1;}
        }
    }else state.noClip=pushOut(state,world);
    move(state,world,wetFromRain);
    if(!state.orb){const bool moved=static_cast<int>(state.previousX)!=static_cast<int>(state.x)||static_cast<int>(state.previousY)!=static_cast<int>(state.y)||static_cast<int>(state.previousZ)!=static_cast<int>(state.z);
        if(moved||state.ticks%25==0){if(lava(blockId(world.getBlock(floorBlock(state.x),floorBlock(state.y),floorBlock(state.z)))))lavaBounce(state);state.searchForMerge=true;if(searchForMerge)searchForMerge(state);}
    }
    float friction=.98f;if(state.onGround)friction=registry::block(blockId(world.getBlock(floorBlock(state.x),floorBlock(state.bounds.minY)-1,floorBlock(state.z)))).slipperiness*.98f;
    state.motionX*=static_cast<double>(friction);state.motionY*=.9800000190734863;state.motionZ*=static_cast<double>(friction);
    if(state.onGround)state.motionY*=state.orb?-.8999999761581421:-.5;
    if(state.orb){++state.color;++state.age;}else{if(state.age!=-32768)++state.age;water(state,world);}
    if(state.age>=6000)state.dead=true;
}
bool combine(State& self,State& other){
    if(&self==&other||self.orb||other.orb||self.dead||other.dead||self.pickupDelay==32767||other.pickupDelay==32767||self.age==-32768||other.age==-32768)return false;
    if(self.item.empty()||other.item.empty()||self.item.id!=other.item.id||!sameTags(self.item,other.item))return false;
    const auto& info=registry::item(other.item.id);if(!info.valid||(info.hasSubtypes&&self.item.damage!=other.item.damage))return false;
    if(other.item.count<self.item.count)return combine(other,self);
    if(static_cast<int>(self.item.count)+other.item.count>info.maxStack)return false;
    other.item.count=static_cast<std::int8_t>(other.item.count+self.item.count);other.pickupDelay=std::max(other.pickupDelay,self.pickupDelay);other.age=std::min(other.age,self.age);self.dead=true;return true;
}
bool insertInventory(std::array<ItemStack,45>& inventory,ItemStack& stack,bool creative){
    if(stack.empty()||!registry::item(stack.id).valid)return false;
    auto firstEmpty=[&](){for(int i=0;i<36;++i)if(inventory[nativeSlot(i)].empty())return i;return -1;};
    if(damageable(stack)&&stack.damage>0){const int slot=firstEmpty();if(slot>=0){inventory[nativeSlot(slot)]=stack;stack.count=0;return true;}if(creative){stack.count=0;return true;}return false;}
    const auto& info=registry::item(stack.id);int before=stack.count;
    do{
        before=stack.count;int slot=-1;
        for(int i=0;i<36;++i){const auto& current=inventory[nativeSlot(i)];if(!current.empty()&&current.id==stack.id&&info.maxStack>1&&(!damageable(current)||current.damage==0)&&current.count<info.maxStack&&current.count<64&&(!info.hasSubtypes||current.damage==stack.damage)&&sameTags(current,stack)){slot=i;break;}}
        if(slot<0)slot=firstEmpty();
        if(slot>=0){auto& current=inventory[nativeSlot(slot)];if(current.empty()){current=stack;current.count=0;}const int moved=std::min<int>(stack.count,std::min(info.maxStack-current.count,64-current.count));
            if(moved!=0){current.count=static_cast<std::int8_t>(current.count+moved);stack.count=static_cast<std::int8_t>(stack.count-moved);}
        }
    }while(stack.count>0&&stack.count<before);
    if(stack.count==before&&creative){stack.count=0;return true;}return stack.count<before;
}
int experienceBarCap(int level){return level>=30?112+(level-30)*9:level>=15?37+(level-15)*5:7+level*2;}
void addExperience(Game& game,int amount){
    if(amount<0||game.experienceLevel<0||game.experienceLevel>200000000||game.totalExperience<0)throw std::invalid_argument("Invalid experience state");
    amount=std::min(amount,std::numeric_limits<int>::max()-game.totalExperience);
    game.experienceProgress+=static_cast<float>(amount)/static_cast<float>(experienceBarCap(game.experienceLevel));game.totalExperience+=amount;
    while(game.experienceProgress>=1){game.experienceProgress=(game.experienceProgress-1.f)*static_cast<float>(experienceBarCap(game.experienceLevel));++game.experienceLevel;game.experienceProgress/=static_cast<float>(experienceBarCap(game.experienceLevel));}
}
bool collect(State& state,Game& game,int& xpCooldown,const std::string& playerName){
    if(game.online)throw std::logic_error("Offline entity pickup cannot mutate an online Game");
    if(state.dead||game.player.spectator||game.health<=0||state.pickupDelay!=0)return false;
    if(state.orb){if(xpCooldown!=0)return false;xpCooldown=2;
        const float first=state.random.nextFloat(),second=state.random.nextFloat();sound(game,"random.orb",.1f,.5f*((first-second)*.7f+1.8f));addExperience(game,state.value);state.dead=true;return true;
    }
    if(!state.owner.empty()&&6000-state.age>200&&state.owner!=playerName)return false;
    if(!insertInventory(game.inventory,state.item,game.creativeMode))return false;
    const float first=state.random.nextFloat(),second=state.random.nextFloat();sound(game,"random.pop",.2f,((first-second)*.7f+1.f)*2.f);
    if(state.item.count<=0)state.dead=true;
    return true;
}

} // namespace mc::drop_entities
