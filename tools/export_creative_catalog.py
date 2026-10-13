#!/usr/bin/env python3
"""Development-only CreativeTabs snapshot; native clients use the saved binary."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess

def main():
    root=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--mcp",type=Path,default=root.parent/"MCP-919-main")
    args=parser.parse_args();mcp=args.mcp.resolve();build=root/"build/creative_reference";classes=build/"classes";classes.mkdir(parents=True,exist_ok=True)
    cp=os.pathsep.join(map(str,[classes,root/"build/registry_export/classes",mcp/"bin/minecraft",mcp/"jars/versions/1.8.9/1.8.9.jar",*sorted((mcp/"jars/libraries").rglob("*.jar"))]))
    sources=[mcp/"src/minecraft/net/minecraft/creativetab/CreativeTabs.java",root/"tools/reference/CreativeCatalogExport.java"]
    subprocess.run(["javac","-encoding","UTF-8","--release","8","-cp",cp,"-d",str(classes),*map(str,sources)],check=True,cwd=build)
    target=root/"tools/reference/creative_catalog.bin";report=root/"tools/reference/creative_catalog.json"
    subprocess.run(["java","-Djava.awt.headless=true","-cp",cp,"CreativeCatalogExport",str(target),str(report)],check=True,cwd=build)
    data=json.loads(report.read_text());print(json.dumps(data,ensure_ascii=False,indent=2))
    (target.parent/"creative_catalog.provenance.json").write_text(json.dumps({"source":"Original CreativeTabs.displayAllReleventItems and Item.getSubItems; original NBT preserved", "tabs":len(data),"tab_entries":sum(t["items"] for t in data),"sha256":hashlib.sha256(target.read_bytes()).hexdigest(),"java_is_development_only":True,"source_sha256":{str(s):hashlib.sha256(s.read_bytes()).hexdigest() for s in sources}},indent=2)+"\n")

if __name__=="__main__":main()
