package net.minecraft.tileentity;
import java.util.*;
import net.minecraft.item.EnumDyeColor;
public final class TileEntityBanner extends TileEntity {
    public enum EnumBannerPattern {BASE;public String getPatternName(){return "base";}}
    public String getPatternResourceLocation(){return "b0";}public List<EnumBannerPattern> getPatternList(){return Collections.singletonList(EnumBannerPattern.BASE);}public List<EnumDyeColor> getColorList(){return Collections.singletonList(EnumDyeColor.BLACK);}
}
