#include "mc/picking.hpp"
#include "mc/game.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    mc::Game game;mc::PickView eye{.5,65.62,.5,0,0};
    mc::Entity player;player.id=10;player.kind=mc::EntityKind::Player;player.uuid="test";player.x=.5;player.y=64;player.z=2;game.entities[10]=player;
    auto pick=mc::pickEntity(game,eye,{});check(pick.id==10,"ordinary player is hittable");
    double expected=2-(double(.6f/2.f)+double(.1f))-.5;
    check(std::abs(pick.distance-expected)<1e-12,"exact Java float width and collision border are used");
    game.world.setBlock(0,65,1,mc::blockState(1));auto wall=mc::pickBlock(game.world,eye,4.5);
    check(wall.hit&&wall.face==2,"block ray faces the north surface");check(mc::pickEntity(game,eye,wall).id==-1,"wall occludes entity interaction");
    game.world.setBlock(0,65,1,0);game.entities[10].z=4.1;
    check(mc::pickEntity(game,eye,{}).id==-1,"survival entity reach is three blocks");
    game.gameMode=1;check(mc::pickEntity(game,eye,{}).id==10,"creative extended entity reach");
    game.playerList["test"].gameMode=3;check(mc::pickEntity(game,eye,{}).id==-1,"spectator players are excluded");game.playerList.clear();
    game.entities[10].z=.5;check(mc::pickEntity(game,eye,{}).distance==0,"eye inside a player box selects zero distance");
    game.entities.clear();mc::Entity item;item.id=20;item.kind=mc::EntityKind::Object;item.type=2;item.x=.5;item.y=64;item.z=2;game.entities[20]=item;
    check(mc::pickEntity(game,eye,{}).id==-1,"dropped items are not attack targets");
    game.entities.clear();mc::Entity zombie=player;zombie.id=30;zombie.kind=mc::EntityKind::Mob;zombie.type=54;game.entities[30]=zombie;
    check(mc::pickEntity(game,eye,{}).id==30,"adult zombie hitbox");game.entities[30].metadata[12].integer=1;
    check(mc::pickEntity(game,eye,{}).id==-1,"baby zombie uses half-height hitbox");
    game.entities.clear();mc::Entity stand=zombie;stand.id=40;stand.type=30;stand.metadata.clear();stand.metadata[10].integer=16;game.entities[40]=stand;
    check(mc::pickEntity(game,eye,{}).id==-1,"marker armor stands are not targets");
    std::cout<<"entity picking tests passed (occlusion, reach, dimensions, metadata and eligibility)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
