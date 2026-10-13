package net.minecraft.tileentity;
import net.minecraft.world.World;
import net.minecraft.block.Block;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.util.BlockPos;
public class TileEntity {
    public World worldObj;public BlockPos pos=new BlockPos(0,0,0);public Block block;public int metadata=2;
    public boolean hasWorldObj(){return worldObj!=null;}public World getWorld(){return worldObj;}public BlockPos getPos(){return pos;}public Block getBlockType(){return block;}public int getBlockMetadata(){return metadata;}
    public void readFromNBT(NBTTagCompound nbt){}public void writeToNBT(NBTTagCompound nbt){}public void markDirty(){}public boolean receiveClientEvent(int id,int value){return false;}
}
