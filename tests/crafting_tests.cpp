#include "mc/crafting.hpp"
#include "mc/controller.hpp"
#include "mc/registry.hpp"

#include <iostream>
#include <stdexcept>

namespace {
std::size_t checks=0;
void require(bool test,const char* message) { ++checks; if(!test)throw std::runtime_error(message); }
mc::ItemStack item(int id,int count=1,int damage=0) {
    mc::ItemStack result;result.id=static_cast<std::int16_t>(id);result.count=static_cast<std::int8_t>(count);result.damage=static_cast<std::int16_t>(damage);return result;
}
void slot(mc::Game& game,int index,const mc::ItemStack& item) {
    mc::net::Writer body;body.i8(static_cast<std::int8_t>(game.window.open?game.window.id:0)).i16(static_cast<std::int16_t>(index));mc::Game::writeItem(body,item);game.consumePacket({0x2f,body.data});
}
}
int main() {
    try {
        using namespace mc;
        require(crafting::ordinaryRecipeCount()==365,"exported recipe count");
        std::vector<ItemStack> grid(4);grid[3]=item(17,2,2);
        auto recipe=crafting::match(grid,2,2);
        require(recipe.matched&&recipe.output.id==5&&recipe.output.count==4&&recipe.output.damage==2,"offset single log preserves species");
        grid={item(5,1,1),ItemStack{},item(5,1,2),ItemStack{}};
        recipe=crafting::match(grid,2,2);require(recipe.output.id==280&&recipe.output.count==4,"metadata wildcard accepts mixed wood sticks");
        grid={item(339),item(334),item(339),item(339)};
        recipe=crafting::match(grid,2,2);require(recipe.output.id==340,"shapeless book permutation");
        grid={item(257,1,200),ItemStack{},item(257,1,180),ItemStack{}};
        recipe=crafting::match(grid,2,2);require(recipe.output.id==257&&recipe.output.damage==118,"repair recipe five percent durability bonus");
        grid={item(335),item(335),item(335),item(353),item(344),item(353),item(296),item(296),item(296)};
        recipe=crafting::match(grid,3,3);require(recipe.output.id==354,"cake recipe");
        require(recipe.remaining[0].id==325&&recipe.remaining[1].id==325&&recipe.remaining[2].id==325,"milk bucket crafting remainders");
        require(crafting::smeltingResult(item(349,1,1)).id==350&&crafting::smeltingResult(item(349,1,1)).damage==1,"salmon smelting metadata");
        require(crafting::smeltingResult(item(349,1,2)).empty(),"clownfish cannot smelt");
        require(crafting::fuelBurnTime(item(327))==20000&&crafting::fuelBurnTime(item(263))==1600,"actual furnace fuel getters");
        require(crafting::potionIngredient(item(372))&&!crafting::potionIngredient(item(1)),"actual potion ingredient predicates");
        require(crafting::repairIngredient(278,264)&&!crafting::repairIngredient(278,265),"diamond tool repair material");

        Game game;game.online=game.joined=game.spawned=true;game.entityId=47;
        std::vector<net::Packet> sent;game.outgoingObserver=[&](const net::Packet& packet){sent.push_back(packet);};Controller controller;
        slot(game,1,item(17,2));require(game.inventory[0].id==5&&game.inventory[0].count==4,"server grid update recomputes crafting result");
        require(controller.clickSlot(game,0,1),"right-click crafting result");
        require(game.cursor.count==4&&game.inventory[1].count==1&&game.inventory[0].count==4,"result inventory ignores requested half amount");
        controller.clickSlot(game,0,0);require(game.cursor.count==8&&game.inventory[1].empty()&&game.inventory[0].empty(),"second result consumes final input");
        game.cursor=ItemStack{};slot(game,1,item(17,6));controller.clickSlot(game,0,0,1);
        require(game.inventory[1].empty()&&game.inventory[44].id==5&&game.inventory[44].count==24,"shift crafting repeats with regenerated output");
        game.consumePacket({0x2d,net::Writer().u8(2).string("minecraft:crafting_table").string("{\"text\":\"Crafting\"}").u8(0).data});
        for(int i=0;i<9;++i)slot(game,i+1,grid[static_cast<std::size_t>(i)]);
        require(game.window.slots[0].id==354,"workbench result from nine inputs");
        controller.clickSlot(game,0,0);require(game.cursor.id==354,"workbench result pickup");
        require(game.window.slots[1].id==325&&game.window.slots[2].id==325&&game.window.slots[3].id==325,"remaining buckets return to empty matrix slots");
        std::cout<<"crafting tests passed ("<<checks<<" checks)\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
