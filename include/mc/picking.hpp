#pragma once
namespace mc {
class World;
class Game;
struct PickView {double x=0,y=0,z=0;float yaw=0,pitch=0;};
struct BlockPick {bool hit=false;int x=0,y=0,z=0,face=1;double distance=0;float hitX=0,hitY=0,hitZ=0;};
struct EntityPick {int id=-1;double x=0,y=0,z=0,distance=0;};
BlockPick pickBlock(const World&,const PickView&,double reach);
EntityPick pickEntity(const Game&,const PickView&,const BlockPick&);
}
