from pathlib import Path
import importlib.util
import io
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
ASSETS=Path(os.environ.get("NATIVEVOXEL_ASSETS", ROOT/"assets"))
sys.path.insert(0,str(ROOT/"tools"))
import prepare_assets as base
import prepare_render_assets as render


class RenderAssetsTests(unittest.TestCase):
    def test_translucent_atlas_padding_preserves_exact_rgba(self):
        image=Image.new("RGBA",(2,2),(53,79,244,191));png=io.BytesIO();image.save(png,format="PNG")
        with tempfile.TemporaryDirectory() as temporary:
            out=Path(temporary)
            mapping,width,height,_=base.build_atlas({"assets/minecraft/textures/blocks/test.png":png.getvalue()},["minecraft:blocks/test"],out)
            atlas=Image.frombytes("RGBA",(width,height),(out/"atlas.rgba").read_bytes());region=mapping["minecraft:blocks/test"]
            for dx,dy in ((-2,0),(2,0),(0,-2),(0,2),(-2,-2)):
                self.assertEqual(atlas.getpixel((region["x"]+dx,region["y"]+dy)),(53,79,244,191))

    def test_gui_exports_and_unicode_pages_preserve_original_pixels(self):
        aliases={"minecraft":"title/minecraft","mojang":"title/mojang","options_background":"options_background",
                 "crafting":"container/crafting_table","chest":"container/generic_54","furnace":"container/furnace",
                 "brewing":"container/brewing_stand","enchanting":"container/enchanting_table","beacon":"container/beacon"}
        aliases.update({f"panorama_{i}":f"title/background/panorama_{i}" for i in range(6)})
        for target,original in aliases.items():
            source=Image.open(ASSETS/"resources/assets/minecraft/textures/gui"/(original+".png")).convert("RGBA")
            self.assertEqual((ASSETS/"runtime/gui"/(target+".rgba")).read_bytes(),render.nvtx(source),target)
        pages=json.loads((ASSETS/"runtime/font/index.json").read_text())
        self.assertEqual(len(pages),222)
        for page in pages:
            name=f"unicode_page_{page['page']:02x}"
            image=Image.open(ASSETS/"resources/assets/minecraft/textures/font"/(name+".png")).convert("RGBA")
            self.assertEqual((ASSETS/"runtime/font"/page["path"]).read_bytes(),render.nvtx(image),name)
        self.assertEqual((ASSETS/"runtime/font/glyph_sizes.bin").read_bytes(),(ASSETS/"resources/assets/minecraft/font/glyph_sizes.bin").read_bytes())
        galactic=Image.open(ASSETS/"resources/assets/minecraft/textures/font/ascii_sga.png").convert("RGBA")
        self.assertEqual((ASSETS/"runtime/gui/enchant_font.rgba").read_bytes(),render.nvtx(galactic))
        self.assertEqual(len((ASSETS/"runtime/font/sga-widths.bin").read_bytes()),256)

    def test_item_model_display_inheritance_matches_189(self):
        models={"assets/minecraft/models/block/base.json":json.dumps({**base.cube("blocks/stone"),"display":{"gui":{"rotation":[0,90,0]}}}).encode(),
                "assets/minecraft/models/item/child.json":json.dumps({"parent":"block/base","display":{"thirdperson":{"scale":[.5,.5,.5]},"gui":{}}}).encode()}
        model=render.ItemModelResolver(models).resolve("item/child")
        self.assertEqual(model["display"]["gui"]["rotation"],[0,90,0])
        self.assertEqual(model["display"]["thirdperson"]["scale"],[.5,.5,.5])

    def test_item_coverage_and_unmodified_generated_icon_pixels(self):
        report=json.loads((ASSETS/"runtime/items/report.json").read_text())
        self.assertEqual(report["registry_items"],337)
        self.assertEqual(report["mapped_item_ids"],335)
        self.assertEqual(report["errors"],[])
        self.assertEqual({row["id"] for row in report["unmapped_registry_ids"]},{60,62})
        self.assertGreaterEqual(report["metadata_mappings"],65536)
        atlas=Image.open(ASSETS/"runtime/items/atlas.png").convert("RGBA")
        regions=json.loads((ASSETS/"runtime/items/regions.json").read_text())
        for name in ("iron_pickaxe","diamond_sword","leather_helmet","spawn_egg","potion_overlay"):
            image=Image.open(ASSETS/"resources/assets/minecraft/textures/items"/(name+".png")).convert("RGBA")
            uv=regions["minecraft:items/"+name]
            x=round(uv[0]*atlas.width-.01);y=round(uv[1]*atlas.height-.01)
            self.assertEqual(atlas.crop((x,y,x+image.width,y+image.height)).tobytes(),image.tobytes(),name)

    def test_native_font_item_and_animation_apis(self):
        compiler=shutil.which("g++") or "D:/Strawberry/c/bin/g++.exe"
        if not Path(compiler).exists() and not shutil.which(compiler):self.skipTest("No C++17 compiler")
        program=r'''
#include "mc/render_assets.hpp"
#include "mc/assets.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
void check(bool value,const std::string& reason){if(!value)throw std::runtime_error(reason);}
int main(int argc,char** argv){try{
    check(argc==4,"args");std::string dir=argv[1],fixture=argv[2],oracle=argv[3],error;
    mc::UnicodeFont font;check(font.load(dir,error),error);
    std::ifstream input(oracle,std::ios::binary);std::vector<unsigned char> widths((std::istreambuf_iterator<char>(input)),{});
    check(widths.size()==131072,"font width oracle length");
    for(int unicode=0;unicode<2;++unicode)for(int code=0;code<65536;++code){int expected=widths[unicode*65536+code];if(expected==255)expected=-1;
        check(font.charWidth(static_cast<std::uint16_t>(code),unicode!=0)==expected,"Original FontRenderer width differs for "+std::to_string(code)+" unicode="+std::to_string(unicode));}
    check(font.charWidth(32)==4 && font.charWidth(167)==-1,"space and formatting widths");
    auto glyph=font.glyph(0x4e2d,true);unsigned b=font.glyphSizes()[0x4e2d];
    check(glyph.page==0x4e&&std::abs(glyph.advance-(float((b&15)+1-(b>>4))/2+1))<.0001f,"CJK page and render advance");
    check(std::abs(glyph.u0-(float(13*16+(b>>4))/256))<.00001f,"original CJK glyph U origin");
    for(int code=0;code<65536;++code){auto g=font.glyph(static_cast<std::uint16_t>(code),true);if(g.page>=0)
        check(g.u0>=0&&g.u1<=1&&g.v0>=0&&g.v1<=1&&g.width>0,"valid logical Unicode glyph bounds");}
    mc::ItemIconAtlas icons;check(icons.load(dir,error),error);check(icons.iconCount()>=600,"item models");
    check(icons.icon(1,2).model=="minecraft:granite_smooth","original inventory metadata name");
    check(icons.icon(35,14).model=="minecraft:red_wool","wool metadata");
    check(icons.icon(278,1000).model=="minecraft:diamond_pickaxe","damageable item normalizes metadata");
    auto armor=icons.icon(298);check(armor.layers.size()==2&&armor.layers[0].color==0xffa06540u&&armor.layers[1].color==0xffffffffu,"leather base and overlay colors");
    mc::ItemAppearance dyed;dyed.color=0x123456;check(icons.icon(298,0,dyed).layers[0].color==0xff123456u,"NBT primary color override");
    auto egg=icons.icon(383,50),pig=icons.icon(383,90);check(egg.layers.size()==2&&egg.layers[0].color!=pig.layers[0].color,"egg primary/overlay palette");
    check(icons.icon(373,8193).layers.size()==2&&icons.icon(373,16385).model=="minecraft:bottle_splash","potion generated layers and splash mapping");
    check(icons.icon(373,8193).layers[0].color!=icons.icon(373,8194).layers[0].color,"potion metadata palette");
    check(icons.icon(358,9999).valid&&icons.icon(403,255).valid,"dynamic shaper metadata fallback");
    check(!icons.icon(60).valid&&!icons.icon(62).valid,"two original unregistered icons stay missing");
    mc::ItemAppearance frame;frame.frame=1;check(icons.icon(261,500,frame).model=="minecraft:bow_pulling_0","bow pose independent of damage");
    auto clock0=icons.icon(347);frame.frame=12;auto clock12=icons.icon(347,0,frame);
    check(clock0.layers[0].u0!=clock12.layers[0].u0||clock0.layers[0].v0!=clock12.layers[0].v0,"clock frames selectable");
    mc::AnimationAtlasCache animations;check(animations.load(dir,error),error);check(animations.animationCount()==9,"original terrain animation count");
    mc::Assets terrain;check(terrain.load(dir,error),error);
    const auto& updates=animations.sample(0);check(updates.size()==9,"initial animation rectangles");
    for(const auto& rectangle:updates)for(unsigned y=0;y<rectangle.height;++y)for(unsigned x=0;x<rectangle.width*4;++x)
        check(rectangle.pixels[y*rectangle.width*4+x]==terrain.atlasPixels()[(rectangle.y+y)*terrain.atlasWidth()*4+rectangle.x*4+x],"padded animation initial frame matches terrain atlas");
    check(animations.sample(0).empty(),"unchanged animation tick emits no upload");
    mc::AnimationAtlasCache test;check(test.load(fixture,error),error);
    auto first=test.sample(0);check(first.size()==1&&first[0].pixels[0]==100&&first[0].pixels[3]==40,"fixture first frame");
    auto half=test.sample(1);check(half.size()==1&&half[0].pixels[0]==50&&half[0].pixels[1]==30&&half[0].pixels[2]==100&&half[0].pixels[3]==40,"RGB interpolates; alpha stays at current frame");
    auto boundary=test.sample(2);check(boundary.size()==1&&boundary[0].pixels[0]==0&&boundary[0].pixels[3]==220,"frame boundary changes alpha");
    check(test.sample(3).empty()&&test.sample(4).empty()&&test.sample(5).empty(),"duplicate frame indices stay unchanged");
    check(test.sample(6).size()==1&&test.sample(6).empty(),"cycle boundary and repeated sampling");
    check(!test.load(fixture,error,"../escape"),"invalid target rejected");
    std::cout<<"131072 original Java font widths, item icons, padded animation/interpolation checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
'''
        with tempfile.TemporaryDirectory() as temporary:
            temp=Path(temporary);fixture=temp/"fixture/runtime/animations";fixture.mkdir(parents=True)
            data=io.BytesIO();data.write(b"NVANIM01");data.write(struct.pack("<III",4,4,1));render.string(data,"test:animation")
            data.write(struct.pack("<6IB3x",1,1,2,2,2,3,1))
            for entry in ((0,2),(1,3),(1,1)):data.write(struct.pack("<II",*entry))
            data.write(bytes((100,20,0,40))*4);data.write(bytes((0,40,200,220))*4)
            (fixture/"terrain.bin").write_bytes(data.getvalue())
            source=temp/"render_test.cpp";source.write_text(program)
            binary=temp/("render_test.exe" if sys.platform=="win32" else "render_test")
            result=subprocess.run([compiler,"-std=c++17","-O2","-Wall","-Wextra","-Wpedantic","-I",str(ROOT/"include"),str(source),str(ROOT/"src/render_assets.cpp"),str(ROOT/"src/assets.cpp"),"-o",str(binary)],capture_output=True,text=True,timeout=90)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(binary),str(ASSETS),str(temp/"fixture"),str(ROOT/"tools/reference/font_width_reference.bin")],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)


if __name__=="__main__":unittest.main(verbosity=2)
