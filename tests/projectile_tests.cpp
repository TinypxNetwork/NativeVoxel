#include "mc/projectiles.hpp"
#include "mc/controller.hpp"
#include "mc/simulation.hpp"
#include "mc/item_names.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace mc;
namespace {
std::size_t checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
void near(double value,double expected,const char* message,double tolerance=1e-9){require(std::abs(value-expected)<tolerance,message);}
ItemStack item(int id,int count=1,int metadata=0){return {std::int16_t(id),std::int8_t(count),std::int16_t(metadata),{0}};}
void spawn(Game& game,int type,int id,int data,double x=.5,double y=80,double z=.5){net::Writer packet;packet.varInt(id).i8(std::int8_t(type)).i32(int(x*32)).i32(int(y*32)).i32(int(z*32)).i8(0).i8(0).i32(data);if(data>0)packet.i16(0).i16(0).i16(0);game.consumePacket({0x0e,packet.data});}
void velocity(Game& game,int id,int x,int y,int z){game.consumePacket({0x12,net::Writer().varInt(id).i16(std::int16_t(x)).i16(std::int16_t(y)).i16(std::int16_t(z)).data});}
void floor(Game& game){for(int x=-3;x<=3;++x)for(int z=-3;z<=3;++z)game.world.setBlock(x,63,z,blockState(1));game.player.teleport(.5,64,.5);game.player.onGround=true;}
void packets(){
    for(int type:{60,61,62,65,73,75,90}){
        Game game;game.online=true;game.entityId=42;spawn(game,type,101,type==73?16421:42);
        require(game.entities.count(101)==1&&game.entities.at(101).projectile.has_value(),"every regular S0E projectile is initialized");
        require(game.entities.at(101).objectData==(type==73?16421:42),"S0E object data is retained");
        velocity(game,101,8000,4000,2000);const auto before=game.entities.at(101);game.tick({});const auto& after=game.entities.at(101);
        near(after.x,before.x+1,"projectile moves during a tick without a new server position");
        near(after.y,before.y+.5,"projectile vertical velocity is integrated");near(after.z,before.z+.25,"projectile forward velocity is integrated");
        game.consumePacket({0x13,net::Writer().varInt(1).varInt(101).data});require(game.entities.empty(),"S13 remains authoritative for projectile removal");
    }
    Game arrow;arrow.online=true;arrow.entityId=42;spawn(arrow,60,5,42);velocity(arrow,5,8000,8000,0);arrow.tick({});arrow.tick({});
    near(arrow.entities.at(5).x,1.5+double(.99f),"arrow keeps moving on the second packet-free tick");
    near(arrow.entities.at(5).motionY,(double(.99f)-double(.05f))*double(.99f)-double(.05f),"arrow uses original drag and gravity");
    arrow.consumePacket({0x18,net::Writer().varInt(5).i32(128).i32(2560).i32(16).i8(0).i8(0).boolean(false).data});
    require(arrow.entities.at(5).x==4&&arrow.entities.at(5).interpolationTicks==0,"arrow correction applies directly like EntityArrow.setPositionAndRotation2");
    Game hook;hook.online=true;hook.entityId=42;spawn(hook,90,5,42,0,80,0);velocity(hook,5,4000,0,0);
    hook.consumePacket({0x15,net::Writer().varInt(5).i8(96).i8(0).i8(0).boolean(false).data});
    for(int tick=1;tick<=3;++tick){hook.tick({});near(hook.entities.at(5).x,tick,"fish correction interpolates for exactly three ticks");}
    hook.tick({});near(hook.entities.at(5).x,3.5,"fish resumes velocity after correction interpolation");
    Game ground;ground.online=true;floor(ground);spawn(ground,60,5,42,.5,66,.5);velocity(ground,5,0,-32000,0);ground.tick({});
    require(ground.entities.at(5).projectile->inGround&&ground.entities.at(5).projectile->shake==7,"arrow embeds into terrain and starts impact shake");near(ground.entities.at(5).y,64.05000000074506,"embedded arrow stops just outside the collision face",1e-7);
    const auto y=ground.entities.at(5).y;ground.tick({});near(ground.entities.at(5).y,y,"embedded arrow is not integrated through terrain");
}
void local(){
    for(const auto& test:{std::pair<int,int>{332,61},{344,62},{368,65},{373,73},{384,75},{346,90}}){
        Game game;floor(game);Simulation simulation;simulation.start(game,189);game.offlineProjectileUse=[&](const ItemStack& stack,float charge){return simulation.useProjectile(game,stack,charge);};
        game.inventory[36]=item(test.first,test.first==346?1:3,test.first==373?16421:0);Controller controller;ControllerInput click;click.usePressed=click.useHeld=click.focused=true;controller.tick(game,click);
        require(game.entities.size()==1&&game.entities.begin()->second.type==test.second,"offline use creates the appropriate visible projectile entity");
        require(game.inventory[36].count==(test.first==346?1:2),"throw consumption matches source and fishing rod remains held");
        const int id=game.entities.begin()->first;const double oldZ=game.entities.at(id).z;simulation.tick(game,{});require(game.entities.count(id)&&game.entities.at(id).z!=oldZ,"offline projectile flies after creation");
        if(test.first==346){require(simulation.useProjectile(game,game.inventory[36],1)>=0&&game.entities.empty(),"second fishing use retracts the same hook");}
    }
    Game rejected;floor(rejected);rejected.joined=rejected.spawned=true;rejected.inventory[36]=item(332,3);Controller controller;ControllerInput click;click.usePressed=click.useHeld=click.focused=true;controller.tick(rejected,click);require(rejected.inventory[36].count==3&&rejected.entities.empty(),"missing offline authority cannot silently consume a throwable");
    Game bow;floor(bow);Simulation simulation;simulation.start(bow,189);bow.offlineProjectileUse=[&](const ItemStack& stack,float charge){return simulation.useProjectile(bow,stack,charge);};bow.inventory[36]=item(261);bow.inventory[37]=item(262,3);controller.reset();controller.tick(bow,click);
    click.usePressed=false;for(int i=0;i<20;++i){simulation.tick(bow,{});controller.tick(bow,click);}click.useHeld=false;simulation.tick(bow,{});controller.tick(bow,click);
    require(bow.entities.size()==1&&bow.entities.begin()->second.type==60&&bow.inventory[37].count==2,"releasing a charged offline bow creates an arrow and consumes ammunition");
    require(bow.inventory[36].damage==1,"shooting damages the bow once");
    Game potion;floor(potion);Simulation potionsSimulation;potionsSimulation.start(potion,1);potion.player.pitch=90;potion.health=5;const auto heal=item(373,1,16389);require(potionsSimulation.useProjectile(potion,heal,1)==0,"offline splash potion launches");
    for(int tick=0;tick<20&&!potion.entities.empty();++tick)potionsSimulation.tick(potion,{});
    require(potion.entities.empty()&&potion.health>5&&!potion.worldEffectEvents.empty()&&potion.worldEffectEvents.back().effect==2002,"splash collision applies nearby effect and queues original break particles");
    projectiles::applyPotion(potion,item(373,1,8194));potion.tick({});require(potion.player.movementSpeed>.1&&potion.effects.count(1),"offline speed potion changes movement and the shared FOV input");
}
void names(){I18n english,chinese;require(english.load("assets","en_US")&&chinese.load("assets","zh_CN"),"load original potion translations");
    require(variantItemName(item(373),english)=="Water Bottle","water name");
    require(variantItemName(item(373,1,8194),english)=="Potion of Swiftness","speed potion effect name");
    require(variantItemName(item(373,1,16386),english)=="Splash Potion of Swiftness","splash prefix and effect name");
    require(variantItemName(item(383,1,50),english)=="Spawn Creeper"&&variantItemName(item(383,1,54),english)=="Spawn Zombie","spawn eggs include distinct creature names");
    require(variantItemName(item(383,1,50),chinese)!=variantItemName(item(383,1,54),chinese),"localized spawn eggs remain distinguishable");
    auto custom=item(373,1,8193);nbt::Tag root;root.type=10;nbt::Tag list;list.type=9;list.subtype=10;nbt::Tag effect;effect.type=10;effect.compound["Id"]=nbt::integer(1,1);effect.compound["Duration"]=nbt::integer(3,123);list.list.push_back(effect);root.compound["CustomPotionEffects"]=list;custom.nbt=nbt::encode(root);
    require(variantItemName(custom,english)=="Potion of Swiftness"&&potions::effects(custom)[0].duration==123,"custom potion effects override metadata name and duration");
}
void impacts(){
    for(int id:{332,344,368,384}){
        Game game;floor(game);game.entityId=42;game.player.pitch=90;Simulation simulation;simulation.start(game,189);
        int experience=0;game.offlineProjectileExperience=[&](int value,double,double,double){experience+=value;};
        require(simulation.useProjectile(game,item(id),1)==0,"offline collision fixture launches");
        for(int tick=0;tick<30&&!game.entities.empty();++tick)simulation.tick(game,{});
        require(game.entities.empty(),"throwable is removed on offline terrain impact");
        if(id==332||id==344)require(game.particleEvents.size()>=8,"snowball and egg collisions emit item fragments");
        if(id==368)require(game.health==15&&game.hurtTime>0,"pearl teleport applies original fall damage and hit effects");
        if(id==384)require(experience>=3&&experience<=11&&!game.worldEffectEvents.empty(),"XP bottle collision creates original experience range and splash effect");
    }
    Game game;floor(game);game.entityId=42;game.player.teleport(3.5,64,.5);Simulation simulation;simulation.start(game,189);
    Entity arrow;arrow.id=100;arrow.kind=EntityKind::Object;arrow.type=60;arrow.objectData=42;arrow.x=arrow.previousX=.5;arrow.y=arrow.previousY=66;arrow.z=arrow.previousZ=.5;
    projectiles::initialize(arrow);projectiles::velocity(arrow,0,-1,0);game.entities.emplace(100,arrow);
    for(int tick=0;tick<12;++tick)simulation.tick(game,{});
    require(game.entities.count(100)&&game.entities.at(100).projectile->inGround,"offline arrow remains embedded until collected");
    game.player.teleport(.5,64,.5);simulation.tick(game,{});
    require(!game.entities.count(100)&&game.inventory[36].id==262&&game.inventory[36].count==1&&game.pickupEvents.size()==1,"embedded arrow recovery inserts one arrow and animates pickup");
    auto bow=item(261);nbt::Tag root;root.type=10;nbt::Tag enchantments;enchantments.type=9;enchantments.subtype=10;nbt::Tag enchantment;enchantment.type=10;enchantment.compound["id"]=nbt::integer(2,51);enchantment.compound["lvl"]=nbt::integer(2,1);enchantments.list.push_back(enchantment);root.compound["ench"]=enchantments;bow.nbt=nbt::encode(root);
    require(projectiles::launch(game,101,bow,1,0).projectile->pickupMode==2,"Infinity bow cannot create recoverable survival ammunition");
}
std::uint64_t bits(double value){std::uint64_t out;std::memcpy(&out,&value,8);return out;}
void trace(const char* path){
    std::ifstream input(path);if(!input)throw std::runtime_error("missing projectile fixture");
    for(std::string line;std::getline(input,line);){if(line.empty()||line[0]=='#')continue;std::istringstream row(line);int type,scenario,count;double x,y,z,mx,my,mz;row>>type>>scenario>>x>>y>>z>>mx>>my>>mz>>count;
        Game game;game.online=true;game.entityId=42;Entity entity;entity.id=5;entity.type=type;entity.kind=EntityKind::Object;entity.objectData=42;entity.x=entity.previousX=x;entity.y=entity.previousY=y;entity.z=entity.previousZ=z;projectiles::initialize(entity);projectiles::velocity(entity,mx,my,mz);
        if(scenario)for(int bx=-4;bx<=4;++bx)for(int bz=-4;bz<=4;++bz)for(int by=63;by<=(scenario==2?70:63);++by)game.world.setBlock(bx,by,bz,blockState(scenario==2?9:1));
        for(int tick=0;tick<count;++tick){entity.previousX=entity.x;entity.previousY=entity.y;entity.previousZ=entity.z;projectiles::tick(game,entity);std::cout<<type<<' '<<tick<<' '<<entity.projectile->inGround;for(double value:{entity.x,entity.y,entity.z,entity.motionX,entity.motionY,entity.motionZ})std::cout<<' '<<bits(value);std::cout<<'\n';}
    }
}
void potionTrace(){I18n tr;tr.load("assets","en_US");for(int metadata=0;metadata<32768;++metadata){const auto stack=item(373,1,metadata);const auto effects=potions::effects(stack);std::cout<<metadata<<' '<<(effects.empty()?0:effects[0].id)<<' '<<(effects.empty()?0:effects[0].amplifier)<<' '<<(effects.empty()?0:effects[0].duration)<<' '<<*variantItemName(stack,tr)<<'\n';}}
}
int main(int argc,char** argv){try{if(argc==3&&std::string(argv[1])=="--trace"){trace(argv[2]);return 0;}if(argc==2&&std::string(argv[1])=="--potions-trace"){potionTrace();return 0;}packets();local();names();impacts();std::cout<<"Projectile use, network motion, collision and names: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
