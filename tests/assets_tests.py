"""Asset integrity and source-derived rendering tests. Run after prepare_assets.py."""
from __future__ import annotations

import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import zipfile

from PIL import Image

PROJECT = Path(__file__).resolve().parents[1]
ASSETS = Path(os.environ.get("NATIVEVOXEL_ASSETS", PROJECT / "assets"))
MCP = PROJECT.parent / "MCP-919-main"
SPEC = importlib.util.spec_from_file_location("prepare_assets", PROJECT / "tools/prepare_assets.py")
P = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(P)


def resource_map(models):
    return {"assets/" + name.replace(":", "/models/", 1) + ".json": json.dumps(model).encode() for name, model in models.items()}


class ModelConversionTests(unittest.TestCase):
    def setUp(self):
        self.atlas = {"minecraft:blocks/stone": {"u0":0.0,"v0":0.0,"u1":1.0,"v1":1.0}}
        self.flags = {"layer":0,"full_cube":True,"occludes":True,"approximate":False}

    def test_parent_chain_and_child_texture_override(self):
        base = P.cube("#stone")
        base["textures"]["stone"] = "blocks/dirt"
        models = resource_map({"minecraft:block/base": base,
                               "minecraft:block/middle": {"parent":"block/base","textures":{"stone":"blocks/gravel"}},
                               "minecraft:block/child": {"parent":"block/middle","textures":{"stone":"blocks/stone"}}})
        resolver = P.ModelResolver(models)
        model = resolver.resolve("block/child")
        self.assertEqual(len(model["elements"]),1)
        self.assertEqual(P.ModelResolver.texture(model,"#all"),"minecraft:blocks/stone")

    def test_parent_ambient_occlusion_inherited_like_java_189(self):
        base = P.cube("blocks/stone"); base["ambientocclusion"] = False
        models = resource_map({"minecraft:block/base":base,
                               "minecraft:block/child":{"parent":"block/base","ambientocclusion":True}})
        self.assertFalse(P.ModelResolver(models).resolve("block/child")["ambientocclusion"])

    def test_parent_and_elements_rejected_like_java_189(self):
        models = resource_map({"minecraft:block/base":P.cube("blocks/stone"),
                               "minecraft:block/child":{"parent":"block/base","elements":P.cube("blocks/stone")["elements"]}})
        with self.assertRaises(P.AssetError): P.ModelResolver(models).resolve("block/child")

    def test_parent_and_texture_cycles_reported(self):
        models = resource_map({"minecraft:block/a":{"parent":"block/b"},"minecraft:block/b":{"parent":"block/a"}})
        with self.assertRaisesRegex(P.AssetError,"parent cycle"): P.ModelResolver(models).resolve("block/a")
        with self.assertRaisesRegex(P.AssetError,"Cyclic texture"):
            P.ModelResolver.texture({"textures":{"a":"#b","b":"#a"}},"#a")

    def test_unqualified_resource_location_defaults_to_minecraft(self):
        models = resource_map({"minecraft:block/base":P.cube("blocks/stone"),
                               "custom:block/child":{"parent":"block/base"}})
        resolved = P.ModelResolver(models).resolve("custom:block/child")
        self.assertEqual(P.ModelResolver.texture(resolved,"#all"),"minecraft:blocks/stone")

    def test_cube_winding_normals_and_cull_faces(self):
        model = P.bake_model(P.cube("blocks/stone"),{},self.atlas,self.flags)
        self.assertEqual(len(model["quads"]),6)
        for face, quad in enumerate(model["quads"]):
            self.assertEqual(quad["face"],face)
            self.assertEqual(quad["cull_face"],face)
            self.assertEqual(quad["normal"],P.NORMALS[face])
            self.assertEqual(set(tuple(v[:3]) for v in quad["vertices"]),set(P.CORNERS[face]))

    def test_y90_rotates_north_face_east_with_uv_attached(self):
        raw = P.cube("blocks/stone"); raw["elements"][0]["faces"] = {"north":{"texture":"#all","cullface":"north"}}
        quad = P.bake_model(raw,{"y":90},self.atlas,self.flags)["quads"][0]
        self.assertEqual(quad["face"],5)
        self.assertEqual(quad["cull_face"],5)
        self.assertTrue(all(v[0]==1.0 for v in quad["vertices"]))
        self.assertEqual([tuple(v[3:]) for v in quad["vertices"]],[(0,0),(0,1),(1,1),(1,0)])

    def test_uvlock_top_preserves_world_texture_orientation(self):
        raw = P.cube("blocks/stone"); raw["elements"][0]["faces"] = {"up":{"texture":"#all"}}
        quad = P.bake_model(raw,{"y":90,"uvlock":True},self.atlas,self.flags)["quads"][0]
        for x,y,z,u,v in quad["vertices"]:
            self.assertAlmostEqual(u,x)
            self.assertAlmostEqual(v,z)

    def test_face_uv_rotation_and_partial_bounds(self):
        raw = P.cube("blocks/stone",end=(16,8,16))
        raw["elements"][0]["faces"] = {"south":{"texture":"#all","rotation":90,"tintindex":2}}
        quad = P.bake_model(raw,{},self.atlas,self.flags)["quads"][0]
        self.assertEqual([tuple(v[3:]) for v in quad["vertices"]],[(0,1),(1,1),(1,.5),(0,.5)])
        self.assertEqual(quad["tint_index"],2)
        self.assertEqual(quad["cull_face"],-1)

    def test_element_rotation_rescale(self):
        point = P.rotation((1,.5,.5),"y",45,(.5,.5,.5),True)
        self.assertAlmostEqual(point[0],1)
        self.assertAlmostEqual(point[2],0)
        self.assertAlmostEqual(point[1],.5)

    def test_metadata_rules_use_original_189_names(self):
        examples = [(1,2,"stone","minecraft:smooth_granite","normal"),
                    (17,5,"log","minecraft:spruce_log","axis=x"),
                    (44,11,"stone_slab","minecraft:cobblestone_slab","half=top"),
                    (53,6,"oak_stairs","minecraft:oak_stairs","facing=south,half=top,shape=straight"),
                    (64,0,"wooden_door","minecraft:wooden_door","facing=east,half=lower,hinge=left,open=false"),
                    (155,3,"quartz_block","minecraft:quartz_column","axis=x"),
                    (175,4,"double_plant","minecraft:double_rose","half=lower")]
        for number,meta,name,resource,props in examples:
            with self.subTest(number=number,meta=meta):
                self.assertEqual(P.state_mapping(number,meta,name),(resource,props,False))

    def test_invalid_metadata_has_explicit_fallback(self):
        self.assertTrue(P.state_mapping(59,15,"wheat")[2])
        self.assertEqual(P.state_mapping(59,15,"wheat")[1],"age=0")
        self.assertFalse(P.state_mapping(1,15,"stone")[2]) # source EnumType.byMetadata legally falls back to stone


class ResourcePackTests(unittest.TestCase):
    def test_sound_pack_keeps_base_and_override_json_layers(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);mcp=root/"mcp";out=root/"out";pack=root/"pack"
            version=mcp/"jars/versions/1.8.9";version.mkdir(parents=True)
            jar=version/"1.8.9.jar"
            with zipfile.ZipFile(jar,"w") as z:z.writestr("pack.png",b"fixture")
            base=b'{"base":{"sounds":["a"]}}';digest=P.sha1(base)
            index_data=json.dumps({"objects":{"minecraft/sounds.json":{"hash":digest,"size":len(base)}}}).encode()
            index=mcp/"jars/assets/indexes/1.8.json";index.parent.mkdir(parents=True);index.write_bytes(index_data)
            obj=mcp/"jars/assets/objects"/digest[:2]/digest;obj.parent.mkdir(parents=True);obj.write_bytes(base)
            (version/"1.8.9.json").write_text(json.dumps({"downloads":{"client":{"sha1":P.sha1(jar.read_bytes()),"size":jar.stat().st_size}},
                                                       "assetIndex":{"sha1":P.sha1(index_data)}}))
            pack.mkdir();(pack/"pack.mcmeta").write_text('{"pack":{"pack_format":1,"description":"sound layer test"}}')
            override=pack/"assets/minecraft/sounds.json";override.parent.mkdir(parents=True)
            override.write_bytes(b'{"base":{"sounds":["b"]},"new":{"sounds":["c"]}}')
            resources,report=P.import_resources(mcp,out,[pack])
            layers=(out/"runtime/audio/sounds-layers.txt").read_text().splitlines()
            self.assertEqual(layers,["minecraft\tvanilla/index/minecraft/sounds.json","minecraft\tresourcepacks/0/assets/minecraft/sounds.json"])
            self.assertEqual((out/"vanilla/index/minecraft/sounds.json").read_bytes(),base)
            self.assertEqual(resources["assets/minecraft/sounds.json"],override.read_bytes())
            self.assertEqual(report["errors"],[])

    def test_zip_and_folder_packs_preserve_exact_names_and_bytes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); folder = root/"pack"; folder.mkdir()
            (folder/"pack.mcmeta").write_text('{"pack":{"pack_format":1,"description":"test"}}')
            image = folder/"assets/minecraft/textures/blocks/stone.png"; image.parent.mkdir(parents=True)
            image.write_bytes(b"unchanged bytes")
            archive = root/"pack.zip"
            with zipfile.ZipFile(archive,"w") as z:
                for file in folder.rglob("*"):
                    if file.is_file():z.write(file,file.relative_to(folder).as_posix())
            self.assertEqual(P.read_pack(folder.resolve()),P.read_pack(archive))
            self.assertEqual(P.read_pack(archive)["assets/minecraft/textures/blocks/stone.png"],b"unchanged bytes")

    def test_archive_paths_cannot_escape_output_or_use_windows_devices(self):
        for path in ("../escape", "/escape", "a/../../escape", "C:/escape", "a\\escape", "a/CON.txt", "a/file. "):
            with self.subTest(path=path),self.assertRaises(P.AssetError):P.safe_resource_name(path)

    def test_duplicate_case_paths_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive=Path(tmp)/"pack.zip"
            with zipfile.ZipFile(archive,"w") as z:
                z.writestr("assets/test/one",b"1");z.writestr("assets/test/ONE",b"2")
            with self.assertRaisesRegex(P.AssetError,"Duplicate"):P.read_pack(archive)

    def test_atlas_padding_and_first_animation_frame(self):
        im=Image.new("RGBA",(2,4),(255,0,0,255));im.paste((0,255,0,128),(0,2,2,4))
        data=io.BytesIO();im.save(data,format="PNG")
        resources={"assets/minecraft/textures/blocks/test.png":data.getvalue(),
                   "assets/minecraft/textures/blocks/test.png.mcmeta":b'{"animation":{"frames":[1,0],"frametime":2}}'}
        with tempfile.TemporaryDirectory() as tmp:
            out=Path(tmp);tiles,w,h,animations=P.build_atlas(resources,["minecraft:blocks/test"],out,2)
            rgba=Image.frombytes("RGBA",(w,h),(out/"atlas.rgba").read_bytes());tile=tiles["minecraft:blocks/test"]
            for dx,dy in ((0,0),(-2,0),(3,0),(0,-2),(0,3),(-2,-2),(3,3)):
                self.assertEqual(rgba.getpixel((tile["x"]+dx,tile["y"]+dy)),(0,255,0,128))
            self.assertEqual(animations[0]["first_frame"],1)


@unittest.skipUnless((ASSETS/"assets.bin").exists(),"Run tools/prepare_assets.py before generated cache tests")
class PreparedAssetsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.states=json.loads((ASSETS/"state-models.json").read_text(encoding="utf-8"))
        cls.variants=json.loads((ASSETS/"model-variants.json").read_text(encoding="utf-8"))

    def test_original_and_effective_asset_copies_match_manifest_hashes(self):
        records=json.loads((ASSETS/"vanilla-manifest.json").read_text(encoding="utf-8"))
        report=json.loads((ASSETS/"import-report.json").read_text(encoding="utf-8"))
        self.assertEqual(report["errors"],[])
        self.assertEqual(report["jar_sha1"],report["jar_expected_sha1"])
        self.assertEqual(report["index_sha1"],report["index_expected_sha1"])
        self.assertEqual(report["index_entries"],report["index_imported_entries"])
        for entry in records:
            relative=entry["path"] if entry["source"]=="jar" else entry["logical_index_path"]
            data=(ASSETS/"vanilla"/entry["source"]/relative).read_bytes()
            self.assertEqual(len(data),entry["size"],relative)
            self.assertEqual(P.sha1(data),entry["sha1"],relative)
        for entry in json.loads((ASSETS/"effective-manifest.json").read_text(encoding="utf-8")):
            self.assertEqual(P.sha1((ASSETS/"resources"/entry["path"]).read_bytes()),entry["sha1"],entry["path"])

    def test_all_registry_states_have_existing_variant(self):
        self.assertEqual([s["state"] for s in self.states],list(range(198*16)))
        for state in self.states:
            variant=self.variants[state["variant_index"]]
            self.assertEqual((state["resource"],state["properties"]),(variant["resource"],variant["properties"]))
        report=json.loads((ASSETS/"model-report.json").read_text(encoding="utf-8"))
        self.assertEqual(report["unresolved_models"],[])
        self.assertEqual(report["unresolved_states"],[])

    def test_source_registry_classes_and_render_layers(self):
        registry,classes=P.read_registry(MCP/"src/minecraft")
        self.assertEqual(len(registry),198)
        self.assertEqual(registry[5]["java_class"],"BlockPlanks")
        self.assertEqual(registry[54]["java_class"],"BlockChest")
        self.assertTrue(P.block_flags(registry[2],classes)["occludes"])
        self.assertFalse(P.block_flags(registry[20],classes)["occludes"])
        self.assertEqual(P.block_flags(registry[95],classes)["layer"],2)
        self.assertTrue(P.block_flags(registry[43],classes)["occludes"])
        self.assertFalse(P.block_flags(registry[44],classes)["occludes"])

    def test_runtime_texture_header_sizes_and_atlas_bounds(self):
        for name in ("steve","gui/icons","gui/widgets","gui/inventory"):
            data=(ASSETS/"runtime"/(name+".rgba")).read_bytes()
            magic,w,h=struct.unpack("<4sII",data[:12])
            self.assertEqual(magic,b"NVTX");self.assertEqual(len(data),12+w*h*4)
        atlas=json.loads((ASSETS/"atlas.json").read_text(encoding="utf-8"))
        self.assertEqual(len((ASSETS/"atlas.rgba").read_bytes()),atlas["width"]*atlas["height"]*4)
        for name,tile in atlas["textures"].items():
            self.assertTrue(0<tile["u0"]<tile["u1"]<1,name)
            self.assertTrue(0<tile["v0"]<tile["v1"]<1,name)

    def test_audio_inventory_and_bundled_native_library_provenance(self):
        report=json.loads((ASSETS/"runtime/audio/openal-provenance.json").read_text(encoding="utf-8"))
        self.assertTrue(report["available"])
        dll=(ASSETS/"runtime/OpenAL64.dll").read_bytes()
        self.assertEqual(P.sha1(dll),report["dll_sha1"])
        archive=Path(report["source_archive"])
        self.assertEqual(P.sha1(archive.read_bytes()),report["archive_sha1"])
        with zipfile.ZipFile(archive) as native:
            self.assertEqual(dll,native.read("OpenAL64.dll"))
        sounds=(ASSETS/"runtime/audio/ogg-files.txt").read_text().splitlines()
        self.assertEqual(len(sounds),578)
        self.assertEqual(len(set(sounds)),578)

    def test_native_loader_queries_and_corruption_rejection(self):
        compiler=shutil.which("g++")
        if not compiler:
            candidate=Path("D:/Strawberry/c/bin/g++.exe")
            compiler=str(candidate) if candidate.exists() else None
        if not compiler:self.skipTest("C++17 compiler unavailable")
        program=r'''
#include "mc/assets.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
int main(int argc,char** argv) {
    assert(argc==3); mc::Assets assets; std::string error;
    assert(assets.load(argv[1],error)); assert(error.empty());
    assert(assets.atlasWidth()>0 && assets.atlasPixels().size()==assets.atlasWidth()*assets.atlasHeight()*4ull);
    assert(assets.model(0).quads.empty()); assert(assets.model(16).quads.size()==6);
    assert(assets.model(2<<4).occludes); assert(!assets.model(20<<4).occludes);
    assert(assets.model(95<<4).layer==mc::RenderLayer::Translucent);
    float low=2,high=-2;
    for(auto& q:assets.model(44<<4).quads)for(auto& v:q.vertices){low=std::min(low,v.y);high=std::max(high,v.y);}
    assert(low==0 && high==0.5f);
    assert(assets.modelWithProperties(85<<4,{{"north","true"}}).quads.size()>assets.model(85<<4).quads.size());
    assert(assets.modelWithProperties(2<<4,{{"snowy","true"}}).quads.size()==6);
    assert(assets.model(9<<4).approximate);
    auto count=assets.modelCount(); assert(!assets.load(argv[2],error)); assert(!error.empty());
    assert(assets.modelCount()==count); // failed reload must retain the prior valid data
    std::cout<<"native asset loader checks passed\n";
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            tmp=Path(tmp);source=tmp/"asset_test.cpp";source.write_text(program)
            bad=tmp/"corrupt";bad.mkdir();(bad/"assets.bin").write_bytes(b"NVASSET1"+b"\xff"*28)
            executable=tmp/("asset_test.exe" if sys_platform_windows() else "asset_test")
            run=subprocess.run([compiler,"-std=c++17","-O2","-I",str(PROJECT/"include"),str(source),str(PROJECT/"src/assets.cpp"),"-o",str(executable)],capture_output=True,text=True,timeout=60)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            run=subprocess.run([str(executable),str(ASSETS),str(bad)],capture_output=True,text=True,timeout=30)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_native_entity_meshes_uv_orientation_animation_and_index(self):
        compiler=shutil.which("g++")
        if not compiler:
            candidate=Path("D:/Strawberry/c/bin/g++.exe")
            compiler=str(candidate) if candidate.exists() else None
        if not compiler:self.skipTest("C++17 compiler unavailable")
        program=r'''
#include "mc/entity_models.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
int main(int argc,char** argv) {
    assert(argc==3);mc::EntityModels models;std::string error;assert(models.load(argv[1],error));
    assert(models.textures().size()>=142);
    const int types[]={30,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,90,91,92,93,94,95,96,97,98,99,100,101,120};
    for(int type:types) {
        mc::EntityPose pose;pose.limbSwing=2.3f;pose.limbSwingAmount=.6f;pose.headYaw=.3f;pose.headPitch=.1f;pose.ageTicks=21;
        auto mesh=models.build(1,type,pose);assert(mesh.supported && !mesh.vertices.empty() && mesh.vertices.size()%3==0);
        assert(mesh.texture>=0 && std::size_t(mesh.texture)<models.textures().size());
        for(const auto& v:mesh.vertices) {
            assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z));
            assert(v.u>=0 && v.u<=1 && v.v>=0 && v.v<=1);
            assert(std::abs(v.nx*v.nx+v.ny*v.ny+v.nz*v.nz-1)<.0001f);
        }
    }
    auto player=models.build(0,0);assert(player.supported&&player.vertices.size()==432);
    // ModelBox's head front has UV (16,8) -> (8,16), faces native +Z.
    // EntityModels also applies RenderPlayer.preRenderCallback's 0.9375 scale.
    const auto& front=player.vertices[24];
    assert(front.nz>.999f && front.z==.25f*.9375f && front.u==.25f && front.v==.125f);
    assert(models.build(1,50).vertices.size()==216);
    assert(models.build(1,52).vertices.size()==396);
    assert(models.build(1,90).vertices.size()==252);
    assert(models.build(1,92).vertices.size()==324);
    assert(models.build(1,120).vertices.size()==324);
    auto movingPose=mc::EntityPose{};movingPose.limbSwing=1;movingPose.limbSwingAmount=.8f;
    auto moving=models.build(0,0,movingPose);bool differs=false;
    for(std::size_t i=0;i<player.vertices.size();++i)if(std::abs(player.vertices[i].z-moving.vertices[i].z)>.01f)differs=true;
    assert(differs);
    auto look=mc::EntityPose{};look.headYaw=1.f;auto looked=models.build(1,120,look);
    // Nose is a true child: it follows the villager's rotated head.
    assert(std::abs(looked.vertices[36].x-models.build(1,120).vertices[36].x)>.05f);
    auto child=mc::EntityPose{};child.child=true;
    float adultTop=-100,childTop=-100;
    for(auto& v:player.vertices)adultTop=std::max(adultTop,v.y);
    for(auto& v:models.build(0,0,child).vertices)childTop=std::max(childTop,v.y);
    assert(childTop<adultTop);
    auto alex=mc::EntityPose{};alex.variant=1;assert(models.build(0,0,alex).texture!=player.texture);
    assert(models.build(1,63).supported && models.build(1,63).vertices.size()>2000);
    assert(models.build(1,95).vertices.size()==396);
    assert(models.build(1,98).vertices.size()==396);
    assert(models.build(1,101).vertices.size()==432);
    assert(models.build(2,1).supported&&models.build(2,1).vertices.size()==180);
    assert(models.build(2,10).supported&&models.build(2,10).vertices.size()==216);
    assert(models.build(2,78).supported&&models.build(2,78).vertices.size()==288);
    mc::EntityPose horse;horse.variant=4|(2<<8);horse.horseArmor=3;horse.flags=mc::PoseSaddled|mc::PoseChested|mc::PoseHasRider;
    assert(models.build(1,100,horse).supported&&models.build(1,100,horse).vertices.size()>models.build(1,100).vertices.size());
    mc::EntityPose wolf;wolf.flags=mc::PoseTamed|mc::PoseSitting;wolf.collarColor=4;
    assert(models.build(1,95,wolf).texture!=models.build(1,95).texture);
    assert(!models.build(2,60).supported);
    auto bad=mc::EntityPose{};bad.headYaw=std::numeric_limits<float>::quiet_NaN();assert(!models.build(0,0,bad).supported);
    auto count=models.textures().size();assert(!models.load(argv[2],error));assert(models.textures().size()==count);
    std::cout<<"33 mob IDs, boat/minecart/armorstand and Steve/Alex entity checks passed\n";
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            tmp=Path(tmp);source=tmp/"entity_test.cpp";source.write_text(program)
            bad=tmp/"corrupt";index=bad/"runtime/entities/index.txt";index.parent.mkdir(parents=True)
            index.write_text("minecraft:entity/steve\t../../outside.rgba\n")
            executable=tmp/("entity_test.exe" if sys_platform_windows() else "entity_test")
            run=subprocess.run([compiler,"-std=c++17","-O2","-I",str(PROJECT/"include"),str(source),str(PROJECT/"src/entity_models.cpp"),"-o",str(executable)],capture_output=True,text=True,timeout=60)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            run=subprocess.run([str(executable),str(ASSETS),str(bad)],capture_output=True,text=True,timeout=30)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_decoded_entity_pixels_match_original_pngs(self):
        records=json.loads((ASSETS/"runtime/entities/index.json").read_text(encoding="utf-8"))
        self.assertGreaterEqual(len(records),142)
        for entry in records:
            namespace,name=entry["resource"].split(":",1)
            source=Image.open(ASSETS/"resources/assets"/namespace/"textures"/(name+".png")).convert("RGBA")
            data=(ASSETS/"runtime/entities"/entry["path"]).read_bytes()
            self.assertEqual(data[:12],b"NVTX"+struct.pack("<II",source.width,source.height),entry["resource"])
            self.assertEqual(data[12:],source.tobytes(),entry["resource"])


def sys_platform_windows():
    import os
    return os.name=="nt"


if __name__=="__main__":unittest.main(verbosity=2)
