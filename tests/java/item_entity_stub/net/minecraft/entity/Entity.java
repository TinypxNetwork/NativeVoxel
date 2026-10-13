package net.minecraft.entity;
import net.minecraft.world.World;
public abstract class Entity {
    public World worldObj;
    public double posX,posY,posZ;
    public static int ordinaryLight;
    public Entity(World world){worldObj=world;}
    public int getBrightnessForRender(float partialTicks){return ordinaryLight;}
}
