#include "mc/entity_visuals.hpp"
#include "mc/item_entities.hpp"
#include "mc/nbt.hpp"
#include "mc/skins.hpp"
#include "mc/world.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks=0;
void require(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
void near(float a,float b,const char* what){require(std::isfinite(a)&&std::abs(a-b)<2e-5f,what);}
mc::nbt::Tag compound(){mc::nbt::Tag t;t.type=10;return t;}
mc::nbt::Tag list(){mc::nbt::Tag t;t.type=9;t.subtype=10;return t;}
std::array<float,3> transformed(const mc::FirstPersonMatrix& m,float x,float y,float z){return {{m[0]*x+m[4]*y+m[8]*z+m[12],m[1]*x+m[5]*y+m[9]*z+m[13],m[2]*x+m[6]*y+m[10]*z+m[14]}};}
void animations(){
    mc::EntityAnimationTracker tracker;mc::EntityAnimationTick t;t.id=1;t.tick=1;t.yaw=t.headYaw=179;tracker.tick(t);
    ++t.tick;t.yaw=t.headYaw=-179;t.pitch=20;tracker.tick(t);auto middle=tracker.sample(1,.5f);
    near(std::abs(middle.headYaw),180,"Head crosses the angle boundary by the short path");near(middle.pitch,10,"Pitch interpolates between game ticks");
    const auto before=tracker.sample(1,1);t.x=100;tracker.tick(t);near(tracker.sample(1,1).pose.limbSwing,before.pose.limbSwing,"Replay does not advance limb state");
    tracker.reset();t={};t.id=2;t.tick=1;tracker.tick(t);++t.tick;t.x=.2;tracker.tick(t);
    auto strafe=tracker.sample(2,1);near(strafe.bodyYaw,-27,"A/D movement turns renderYawOffset without changing head yaw");near(strafe.headYaw,0,"Strafing retains camera head yaw");require(strafe.pose.limbSwingAmount>0,"Strafing animates the limbs");
    ++t.tick;t.swingProgress=.5f;t.previousSwingProgress=.4f;tracker.tick(t);require(tracker.sample(2,1).bodyYaw>strafe.bodyYaw,"Arm swing pulls body back toward facing");
    t.previousSwingProgress=.9f;t.swingProgress=0;++t.tick;tracker.tick(t);near(tracker.sample(2,.5f).pose.swingProgress,.95f,"Swing wraps forward through the final tick");
    tracker.erase(2);require(!tracker.sample(2,0).valid,"Destroyed entities release animation state");
}
void chests(){
    mc::World world;world.setBlock(0,64,0,mc::blockState(54,3));world.setBlock(1,64,0,mc::blockState(54,3));mc::ChestAnimationTracker tracker;
    require(tracker.action(world,1,64,0,54,1,1,1),"Second-half chest event accepted");tracker.tick(world,1);
    auto a=tracker.sample(world,0,64,0),b=tracker.sample(world,1,64,0);near(a.current,.1f,"Lid advances by Java's per-tick step");near(b.current,a.current,"Double chest halves share a lid");
    require(mc::TileEntityVisuals::lidRadians(a,.5f)<0&&mc::TileEntityVisuals::lidRadians(a,.5f)>mc::TileEntityVisuals::lidRadians(a,1),"Lid is interpolated within each tick");
    require(!tracker.action(world,0,64,0,54,1,0,1),"Replayed event cannot close the chest");tracker.tick(world,1);near(tracker.sample(world,0,64,0).current,.1f,"Repeated rendering cannot advance chest ticks");
    tracker.action(world,0,64,0,54,1,0,2);tracker.tick(world,2);near(tracker.sample(world,0,64,0).current,0,"Close event reverses lid movement");
    world.setBlock(0,64,0,0);world.setBlock(1,64,0,0);tracker.tick(world,3);require(tracker.states().empty(),"Destroyed chest animation is removed");
}
void equipment(const std::string& root){
    std::string error;mc::EntityModels models;mc::FirstPersonAssets items;mc::TileEntityVisuals tiles;
    require(models.load(root+"/assets",error),error.c_str());require(items.load(root+"/assets",error),error.c_str());require(tiles.load(root+"/assets",error),error.c_str());
    auto data=compound();data.compound["ench"]=list();auto bytes=mc::nbt::encode(data);mc::VisualItem sword{276,0,1,&bytes};
    require(mc::EntityVisuals::enchanted(sword)&&mc::EntityVisuals::hasEffect(sword),"Ordinary enchantment-tag behavior");
    require(mc::EntityVisuals::hasEffect({403})&&!mc::EntityVisuals::enchanted({403}),"Enchanted book hasEffect differs from isItemEnchanted");
    require(mc::EntityVisuals::hasEffect({373,8193})&&!mc::EntityVisuals::hasEffect({373,0}),"Potion effects determine the glint");
    auto potion=compound();potion.compound["CustomPotionEffects"]=list();auto potionBytes=mc::nbt::encode(potion);
    require(!mc::EntityVisuals::hasEffect({373,8193,1,&potionBytes}),"Empty custom effects override potion metadata");
    auto effect=compound();effect.compound["Id"]=mc::nbt::integer(1,1);effect.compound["Amplifier"]=mc::nbt::integer(1,0);potion.compound["CustomPotionEffects"].list.push_back(effect);potionBytes=mc::nbt::encode(potion);
    require(mc::EntityVisuals::hasEffect({373,0,1,&potionBytes}),"Custom potion effects produce a glint");
    near(float(mc::EntityVisuals::primaryItemColor({373,0,1,&potionBytes})),8171462.f,"Potion tint uses source liquid color");
    auto ground=mc::EntityVisuals::item(items,sword,mc::ItemModelTransform::Ground,&tiles,nullptr,-1,1234);
    require(ground.draws.size()==3&&ground.draws[1].glint&&ground.draws[2].glint,"Ground item composes two moving glint passes");
    require(ground.draws[0].mesh==ground.draws[1].mesh,"Glint reuses exact base geometry for equal-depth rendering");
    mc::DroppedItemPose dropped;dropped.itemId=276;dropped.nbt=&bytes;dropped.colorOverride=0xabcdef;dropped.renderTimeMillis=1234;
    auto drop=mc::ItemEntityVisuals::dropped(items,dropped,&tiles);require(drop.draws.size()==3&&drop.draws[1].glint,"Dropped color override retains enchantment passes");
    mc::EntityPose pose;pose.ageTicks=21.5f;auto armor=models.playerArmor(299,pose,0x123456,true);require(armor.size()==4,"Dyed leather uses base, overlay and two glints");near(armor[0].color[0],18.f/255,"Dyed armor color");near(armor[2].color[2],.8f*.76f,"Armor glint blue matches LayerArmorBase");
    mc::PlayerVisualInput player;player.pose=pose;player.equipment[0]=sword;player.equipment[3]={299,0,1,&bytes};auto frame=mc::EntityVisuals::player(models,items,player,&tiles);
    require(frame.supported&&frame.layers.size()==5&&frame.held.size()==3,"Player body, worn armor and held enchantment compose together");
    player.pose.spectator=true;auto spectator=mc::EntityVisuals::player(models,items,player,&tiles);require(spectator.layers.size()==1&&spectator.held.empty(),"Spectator hides held items and armor");
    mc::EntityPose slim;slim.variant=1;auto slimHand=models.playerHand(slim),steveHand=models.playerHand({});require(slimHand!=steveHand,"Slim skin uses the original adjusted hand attachment");
    mc::EntityPose wither;wither.variant=1;auto normal=models.build(1,51),large=models.build(1,51,wither);require(normal.vertices.size()==large.vertices.size(),"Skeleton variant keeps topology");for(std::size_t i=0;i<normal.vertices.size();++i){near(large.vertices[i].x,normal.vertices[i].x*1.2f,"Wither skeleton renderer scale X");near(large.vertices[i].y,normal.vertices[i].y*1.2f,"Wither skeleton renderer scale Y");}
    auto normalHand=models.humanoidHand(1,51,{}),largeHand=models.humanoidHand(1,51,wither);for(unsigned i=0;i<16;++i)near(largeHand[i],i==15?normalHand[i]:normalHand[i]*1.2f,"Wither skeleton equipment shares body scale");
    auto owner=compound();owner.compound["Id"]=mc::nbt::text("00000000-0000-0000-0000-000000000001");auto skull=compound();skull.compound["SkullOwner"]=owner;auto skullBytes=mc::nbt::encode(skull);
    for(auto transform:{mc::ItemModelTransform::Gui,mc::ItemModelTransform::Ground,mc::ItemModelTransform::ThirdPerson}){auto head=mc::EntityVisuals::item(items,{397,3,1,&skullBytes},transform,&tiles);require(head.supported&&head.draws.size()==2&&head.draws[0].dynamicTexture&&head.draws[0].dynamicTexture->key=="minecraft:entity/alex","Skull owner skin survives all item transform paths");}
    mc::PlayerVisualInput hat;hat.equipment[4]={397,3,1,&skullBytes};auto worn=mc::EntityVisuals::player(models,items,hat,&tiles);require(worn.customHead.size()==2,"Worn player head carries skin and overlay");
    auto handHead=models.playerHead({});const auto& vertex=worn.customHead[0].mesh->vertices[0];auto actual=transformed(worn.customHead[0].matrix,vertex.x,vertex.y,vertex.z);auto expected=transformed(handHead,vertex.x*1.1875f,vertex.y*1.1875f,vertex.z*1.1875f);for(unsigned i=0;i<3;++i)near(actual[i],expected[i],"Worn skull cancels item centering and double scale");
    mc::FirstPersonPose first;first.itemId=397;first.metadata=3;auto firstHead=mc::EntityVisuals::firstPerson(items,first,{397,3,1,&skullBytes},&tiles);require(firstHead.draws.size()==2&&firstHead.draws[0].dynamicTexture,"First person head receives custom owner texture");
    mc::ArrowPose arrow;arrow.x=3;arrow.y=7;arrow.z=-2;auto arrowFrame=mc::EntityVisuals::arrow(arrow);require(arrowFrame.draws.size()==6,"Arrow uses two shaft faces and four rotated feather quads");arrow.shakeTicks=5;require(mc::EntityVisuals::arrow(arrow).draws[0].matrix!=arrowFrame.draws[0].matrix,"Impact shake rotates the complete arrow");
}
}
int main(int argc,char** argv){try{animations();chests();equipment(argc>1?argv[1]:".");
    mc::EntityAnimationTracker roaming;std::size_t peak=0;
    for(std::uint64_t tick=0;tick<10000;++tick){mc::EntityAnimationTick pose;pose.id=int(tick);pose.tick=tick;roaming.tick(pose);roaming.prune(tick);peak=std::max(peak,roaming.size());}
    require(peak==4&&roaming.size()==4,"10000 departing entities retain only the three-tick animation grace window");
    roaming.prune(10004);require(roaming.size()==0,"all departed entity animation state is released");
    std::cout<<checks<<" entity visual integration checks passed; roaming 10000 entities, peak states="<<peak<<", retained="<<roaming.size()<<"\n";return 0;}catch(const std::exception& e){std::cerr<<"Entity visuals failed: "<<e.what()<<'\n';return 1;}}
