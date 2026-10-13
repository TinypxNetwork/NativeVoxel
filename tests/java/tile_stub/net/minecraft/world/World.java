package net.minecraft.world;
import java.util.Random;
import net.minecraft.util.BlockPos;
import net.minecraft.block.state.IBlockState;
public class World {public boolean isRemote=true;public final Random rand=new Random(0);public long time;public long getTotalWorldTime(){return time;}public IBlockState getBlockState(BlockPos pos){return null;}public void playSoundEffect(double x,double y,double z,String name,float volume,float pitch){} }
