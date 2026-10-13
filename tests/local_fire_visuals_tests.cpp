#include "mc/local_fire_visuals.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks=0;
void require(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
void lifetimes(){
    mc::visual::LocalFireVisuals fire;mc::visual::LocalFireInput input;
    fire.tick(input);input.flammable=true;
    for(int i=0;i<19;++i){fire.tick(input);require(!fire.burning(),"Player has twenty ticks of fire contact resistance");}
    fire.tick(input);require(fire.remainingTicks()==160,"Fire contact ignites eight seconds at the source threshold");
    for(int i=0;i<10;++i)fire.tick(input);
    require(fire.remainingTicks()==160,"Standing in fire offsets the normal one-tick decrement");
    input.flammable=false;fire.tick(input);require(fire.remainingTicks()==159,"Leaving fire preserves the burning effect");
    for(int i=0;i<158;++i)fire.tick(input);
    require(fire.burning()&&fire.remainingTicks()==1,"The final burning tick remains visible");
    fire.tick(input);require(!fire.burning(),"Fire expires after its original lifetime");
    input.inLava=input.flammable=true;fire.tick(input);
    require(fire.remainingTicks()==301,"Lava sets fifteen seconds before moveEntity's contact increment");
    input.inLava=input.flammable=false;fire.tick(input);
    require(fire.remainingTicks()==300,"Leaving lava retains fifteen seconds of screen and body flames");
    for(int i=0;i<300;++i)fire.tick(input);
    require(!fire.burning(),"Lava's lingering effect expires");
    fire.reset();input.inLava=input.flammable=true;input.fireProtection=4;fire.tick(input);
    require(fire.remainingTicks()==121,"Maximum armor fire protection shortens the original fire duration");
    input.inLava=input.flammable=false;input.inWater=true;fire.tick(input);
    require(!fire.burning(),"Entering water extinguishes immediately");
    input.inWater=false;input.fireProtection=0;input.inLava=input.flammable=true;fire.tick(input);
    input.inLava=input.flammable=false;input.wet=true;fire.tick(input);
    require(!fire.burning(),"Exposed rain extinguishes lingering fire");
    input.wet=false;input.inLava=input.flammable=input.disableDamage=true;fire.tick(input);
    require(!fire.burning(),"Creative or invulnerable players extinguish at the end of the tick");
    input.disableDamage=false;fire.tick(input);require(fire.burning(),"Survival lava can ignite again");
    input.active=false;fire.tick(input);require(!fire.burning(),"Online, unspawned and spectator paths cannot retain local flames");
}
void environment(){
    mc::World world;world.setBlock(0,60,0,mc::blockState(1));
    mc::Player player;player.teleport(.5,61,.5);
    require(!mc::visual::touchesFire(world,player.bounds()),"Plain terrain is not flammable contact");
    world.setBlock(0,61,0,mc::blockState(51));
    require(mc::visual::touchesFire(world,player.bounds()),"Standing in a fire block starts contact");
    world.setBlock(0,61,0,0);world.setBlock(1,61,0,mc::blockState(51));
    player.teleport(.7,61,.5);
    require(!mc::visual::touchesFire(world,player.bounds()),"Contracted box excludes a merely touching fire neighbor");
    player.teleport(.702,61,.5);
    require(mc::visual::touchesFire(world,player.bounds()),"Crossing the fire block boundary ignites");
    require(mc::visual::rainReaches(world,0,1,0,62,0),"Rain reaches exposed plains");
    require(!mc::visual::rainReaches(world,0,.2f,0,62,0),"The original rain strength threshold is strict");
    require(!mc::visual::rainReaches(world,-1,1,0,62,0),"The Nether does not extinguish through rain");
    world.setBlock(0,65,0,mc::blockState(20));
    require(!mc::visual::rainReaches(world,0,1,0,62,0),"Transparent glass roofs still block precipitation");
    world.setBlock(0,65,0,0);
    require(mc::visual::rainReaches(world,0,1,0,62,0),"Removing the roof exposes rain immediately");
    auto& chunk=const_cast<mc::Chunk&>(*world.chunk(0,0));chunk.hasBiomes=true;
    for(int biome:{2,8,9,10,12,17,30,35,36,37,38,39,130,140,163,165}){
        chunk.biomes.fill(std::uint8_t(biome));
        require(!mc::visual::rainReaches(world,0,1,0,62,0),"Dry and snowy biomes do not extinguish through rain");
    }
    chunk.biomes.fill(3);
    require(mc::visual::rainReaches(world,0,1,0,62,0),"Low extreme hills have rain");
    require(!mc::visual::rainReaches(world,0,1,0,200,0),"High extreme hills use the original cold precipitation condition");
}
void fixtures(const char* source,const char* destination){
    std::ifstream input(source);std::ofstream output(destination);int reset,active,water,lava,wet,flammable,immune,protection;
    mc::visual::LocalFireVisuals fire;
    while(input>>reset>>active>>water>>lava>>wet>>flammable>>immune>>protection){
        if(reset)fire.reset();mc::visual::LocalFireInput state;
        state.active=active;state.inWater=water;state.inLava=lava;state.wet=wet;state.flammable=flammable;state.disableDamage=immune;state.fireProtection=protection;
        fire.tick(state);output<<fire.remainingTicks()<<'\n';
    }
    if(!output)throw std::runtime_error("Could not write fire reference output");
}
}
int main(int argc,char** argv){try{
    if(argc==4&&std::string(argv[1])=="--fixtures"){fixtures(argv[2],argv[3]);return 0;}
    lifetimes();environment();std::cout<<checks<<" local fire visual checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Local fire visual: "<<error.what()<<'\n';return 1;}}
