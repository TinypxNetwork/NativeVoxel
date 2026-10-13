#pragma once
#include "mc/worldgen.hpp"
#include "mc/worldgen_noise.hpp"

namespace mc::worldgen::detail {
struct Position {
    int x{},y{},z{};
    Position add(int dx,int dy,int dz) const {return {x+dx,y+dy,z+dz};}
};
bool generateOre(World& world,JavaRandom& random,Position at,BlockState ore,int count,unsigned replaceId=1);
void generateDefaultOres(World& world,JavaRandom& random,Position origin);
int topSolidOrLiquid(const World& world,int x,int z);
bool generateSpike(World& world,JavaRandom& random,Position at,unsigned requiredBase,PopulationOutput& output);
void populateEnd(World& world,JavaRandom& random,int chunkX,int chunkZ,PopulationOutput& output);
enum class ScatterKind { Flowers, TallGrass, DeadBush, DoublePlant, Cactus, Reed, Pumpkin, Melon, Waterlily, Mushroom, Fire, Glowstone };
// Callers must provide up-to-date world light (or its exact query callback) for
// mushroom placement. These functions do not advance simulation/light queues.
bool generateScatter(World& world,JavaRandom& random,Position at,ScatterKind kind,BlockState state=0,bool noSky=false,const std::function<int(Position)>& light={});
bool generateSediment(World& world,JavaRandom& random,Position at,BlockState state,int radius,bool clay=false);
bool generateBlob(World& world,JavaRandom& random,Position at,BlockState state,int radius);
enum class TreeKind { Oak, Birch, TallBirch, Pine, Spruce, Swamp, Shrub, Acacia, DarkOak, MegaJungle, MegaPine, MegaSpruce };
struct TreeOptions {int minimumHeight{4};BlockState log{blockState(17)},leaves{blockState(18)};bool vines{};};
bool generateTree(World& world,JavaRandom& random,Position at,TreeKind kind,TreeOptions options={});
void decorateTreeBase(World& world,JavaRandom& random,Position at,TreeKind kind);
struct BigTreeState {int heightLimit{},leafDistance{4};};
bool generateBigTree(World& world,JavaRandom& random,Position at,BigTreeState& state,bool prepareForDecoration=false);
// A default WorldGenBigMushroom chooses its type on its first call (including a
// failed call) and retains that choice. Use one selectedType per original object.
bool generateHugeMushroom(World& world,JavaRandom& random,Position at,unsigned& selectedType);
bool generateIceSpike(World& world,JavaRandom& random,Position at);
bool generateIcePath(World& world,JavaRandom& random,Position at,int width=4);
bool generateDesertWell(World& world,Position at);
bool generateVines(World& world,JavaRandom& random,Position at);
struct LakeHooks {
    std::function<int(Position)> skyLight;
    std::function<BlockState(Position)> biomeTop;
    std::function<bool(Position)> canFreezeWater;
};
bool generateLake(World& world,JavaRandom& random,Position at,unsigned liquidBlock,const LakeHooks& hooks);
bool generateDungeon(World& world,JavaRandom& random,Position at,PopulationOutput& output);
}
