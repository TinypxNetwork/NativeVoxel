// Included in namespace mc by game.cpp. Visual/audio state only; never predicts damage.
namespace {
struct LivingSound {const char* hurt;const char* death;float volume;};
LivingSound livingSound(const Entity& entity,const World& world) {
    if(entity.kind==EntityKind::Player)return {"game.player.hurt","game.player.die",1};
    switch(entity.type) {
    case 50:return {"mob.creeper.say","mob.creeper.death",1};
    case 51:return {"mob.skeleton.hurt","mob.skeleton.death",1};
    case 52:case 59:return {"mob.spider.say","mob.spider.death",1};
    case 54:return {"mob.zombie.hurt","mob.zombie.death",1};
    case 55:case 62:{const int size=std::max(1,visual::metadataInt(entity,16,1));const char* name=size>1?"mob.slime.big":"mob.slime.small";return {name,name,float(size)*.4f};}
    case 56:return {"mob.ghast.scream","mob.ghast.death",10};
    case 57:return {"mob.zombiepig.zpighurt","mob.zombiepig.zpigdeath",1};
    case 58:return {"mob.endermen.hit","mob.endermen.death",1};
    case 60:case 67:return {"mob.silverfish.hit","mob.silverfish.kill",1};
    case 61:return {"mob.blaze.hit","mob.blaze.death",1};
    case 63:return {"mob.enderdragon.hit","game.neutral.die",5};
    case 64:return {"mob.wither.hurt","mob.wither.death",1};
    case 65:return {"mob.bat.hurt","mob.bat.death",.1f};
    case 66:case 94:case 97:return {nullptr,nullptr,1};
    case 68:{const auto material=blockId(world.getBlock(floorBlock(entity.x),floorBlock(entity.y),floorBlock(entity.z)));
        if(material!=8&&material!=9)return {"mob.guardian.land.hit","mob.guardian.land.death",1};
        return (visual::metadataInt(entity,16)&4)?LivingSound{"mob.guardian.elder.hit","mob.guardian.elder.death",1}:LivingSound{"mob.guardian.hit","mob.guardian.death",1};}
    case 90:return {"mob.pig.say","mob.pig.death",1};
    case 91:return {"mob.sheep.say","mob.sheep.say",1};
    case 92:case 96:return {"mob.cow.hurt","mob.cow.hurt",.4f};
    case 93:return {"mob.chicken.hurt","mob.chicken.hurt",1};
    case 95:return {"mob.wolf.hurt","mob.wolf.death",.4f};
    case 98:return {"mob.cat.hitt","mob.cat.hitt",.4f};
    case 99:return {"mob.irongolem.hit","mob.irongolem.death",1};
    case 100:{switch(visual::metadataInt(entity,19)) {
        case 1:case 2:return {"mob.horse.donkey.hit","mob.horse.donkey.death",.8f};
        case 3:return {"mob.horse.zombie.hit","mob.horse.zombie.death",.8f};
        case 4:return {"mob.horse.skeleton.hit","mob.horse.skeleton.death",.8f};
        default:return {"mob.horse.hit","mob.horse.death",.8f};}}
    case 101:return {"mob.rabbit.hurt","mob.rabbit.death",1};
    case 120:return {"mob.villager.hit","mob.villager.death",1};
    default:return {"game.neutral.hurt","game.neutral.die",1};
    }
}
}
bool Game::handleEntityStatus(int id,int statusCode) {
    const auto found=entities.find(id);
    if(id!=entityId&&found==entities.end())return true;
    if(statusCode!=2&&statusCode!=3)return false;
    Entity local;local.kind=EntityKind::Player;local.x=player.x;local.y=player.y;local.z=player.z;
    auto& entity=id==entityId?local:found->second;
    if(entity.kind!=EntityKind::Player&&entity.kind!=EntityKind::Mob)return false;
    if(statusCode==2){
        if(id==entityId){hurtTime=maxHurtTime=10;attackedAtYaw=0;}
        else{entity.hurtTime=entity.maxHurtTime=10;entity.attackedAtYaw=0;}
    }else{
        if(id==entityId){health=0;player.dead=true;}
        else{entity.dying=true;auto& hp=entity.metadata[6];hp.type=3;hp.number=0;}
    }
    const auto definition=livingSound(entity,world);const auto* name=statusCode==2?definition.hurt:definition.death;
    if(name){
        auto random=[&](){pickupSoundRandomState_=(pickupSoundRandomState_*0x5deece66dULL+11)&((1ULL<<48)-1);return float(pickupSoundRandomState_>>24)/16777216.f;};
        const float a=random(),b=random();
        soundEvents.push_back({name,entity.x,entity.y,entity.z,definition.volume,(a-b)*.2f+1.f});
        if(soundEvents.size()>256)soundEvents.pop_front();
    }
    return true;
}
void Game::tickLivingEffects() {
    if(hurtTime>0)--hurtTime;
    if(health<=0&&spawned){if(deathTime<1000000)++deathTime;}else deathTime=0;
    for(auto it=entities.begin();it!=entities.end();){
        auto& entity=it->second;
        if(entity.hurtTime>0)--entity.hurtTime;
        if(entity.kind!=EntityKind::Player&&entity.kind!=EntityKind::Mob){++it;continue;}
        const auto healthValue=entity.metadata.find(6);
        if(healthValue!=entity.metadata.end()&&healthValue->second.type==3)entity.dying=healthValue->second.number<=0;
        if(!entity.dying){entity.deathTime=0;++it;continue;}
        // Dragon's separate 200-tick death sequence is server-authoritative.
        if(entity.type==63&&entity.kind==EntityKind::Mob){++entity.deathTime;++it;continue;}
        if(++entity.deathTime<20){++it;continue;}
        const auto size=visual::entitySize(entity);
        deathEvents.push_back({entity.x,entity.y,entity.z,size.width,size.height});
        if(deathEvents.size()>512)deathEvents.pop_front();
        it=entities.erase(it);
    }
}
