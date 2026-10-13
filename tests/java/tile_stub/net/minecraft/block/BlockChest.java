package net.minecraft.block;
import net.minecraft.world.World;
import net.minecraft.util.BlockPos;
import net.minecraft.block.state.IBlockState;
public final class BlockChest extends Block {public final int chestType;public BlockChest(int type){chestType=type;}public IBlockState checkForSurroundingChests(World w,BlockPos p,IBlockState state){return state;}}
