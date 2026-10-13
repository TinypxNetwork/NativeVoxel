#!/usr/bin/env python3
"""Development-only export of exact original RenderItem model/tint registrations.

Normal native builds consume the checked-in JSON result and do not need Java.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess


def main():
    root=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcp",type=Path,default=root.parent/"MCP-919-main")
    parser.add_argument("--assets",type=Path,default=root/"assets")
    args=parser.parse_args();mcp=args.mcp.resolve()
    build=root/"build/render_assets_reference";classes=build/"classes";classes.mkdir(parents=True,exist_ok=True)
    registry_classes=root/"build/registry_export/classes"
    cp=os.pathsep.join(str(p) for p in [classes,registry_classes,mcp/"bin/minecraft",mcp/"jars/versions/1.8.9/1.8.9.jar",*sorted((mcp/"jars/libraries").rglob("*.jar"))])
    source=mcp/"src/minecraft/net/minecraft"
    sources=[source/"client/renderer/entity/RenderItem.java",source/"client/renderer/ItemModelMesher.java",
             source/"client/renderer/ItemMeshDefinition.java",source/"client/resources/model/ModelManager.java",
             source/"client/resources/model/ModelResourceLocation.java",source/"client/renderer/BlockModelShapes.java",
             source/"potion/PotionHelper.java",source/"client/gui/FontRenderer.java",root/"tools/reference/RenderItemExport.java"]
    arguments=["-encoding","UTF-8","--release","8","-cp",cp,"-d",str(classes),*map(str,sources)]
    argfile=build/"javac.args";argfile.write_text("\n".join('"'+a.replace("\\","/").replace('"','\\"')+'"' for a in arguments),encoding="utf-8")
    subprocess.run(["javac","@"+str(argfile)],check=True,cwd=build)
    target=root/"tools/reference/item_render_registry.json"
    font_target=target.parent/"font_width_reference.bin"
    subprocess.run(["java","-Xmx1g","-Djava.awt.headless=true","-cp",cp,"RenderItemExport",str(target),str(args.assets.resolve()),str(font_target)],check=True,cwd=build)
    data=json.loads(target.read_text(encoding="utf-8"))
    provenance={"source":"Original RenderItem registrations and Item.getColorFromItemStack, invoked without OpenGL",
                "mapping_entries":len(data["mappings"]),"items":len(data["items"]),"dynamic_definitions":len(data["dynamic"]),
                "json_sha256":hashlib.sha256(target.read_bytes()).hexdigest(),
                "font_width_reference_sha256":hashlib.sha256(font_target.read_bytes()).hexdigest(),
                "java_dependencies":"Fresh registry_export classes and provided MCP bin/minecraft; used only by the development exporter",
                "source_sha256":{str(p.relative_to(mcp) if p.is_relative_to(mcp) else p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}}
    (target.parent/"item_render_registry.provenance.json").write_text(json.dumps(provenance,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({k:v for k,v in provenance.items() if k!="source_sha256"},indent=2))


if __name__=="__main__":main()
