from pathlib import Path
import hashlib,json,os,subprocess

root=Path(__file__).resolve().parents[1];mcp=root.parent/"MCP-919-main";build=root/"build/skin_reference";classes=build/"classes";classes.mkdir(parents=True,exist_ok=True)
cp=os.pathsep.join(map(str,[classes,mcp/"bin/minecraft",mcp/"jars/versions/1.8.9/1.8.9.jar",*sorted((mcp/"jars/libraries").rglob("*.jar"))]))
source=mcp/"src/minecraft/net/minecraft/client/renderer/ImageBufferDownload.java"
subprocess.run(["javac","-encoding","UTF-8","--release","8","-cp",cp,"-d",str(classes),str(source),str(root/"tools/reference/SkinReference.java")],check=True)
target=root/"tools/reference/skin_reference.bin"
subprocess.run(["java","-Djava.awt.headless=true","-cp",cp,"SkinReference",str(target)],check=True)
(target.parent/"skin_reference.json").write_text(json.dumps({"source":"Original ImageBufferDownload.parseUserSkin; four opaque/alpha modern/legacy fixtures plus all 65,536 channel/alpha combinations","case_count":20,"source_sha256":hashlib.sha256(source.read_bytes()).hexdigest(),"reference_sha256":hashlib.sha256(target.read_bytes()).hexdigest()},indent=2)+"\n")
print(target)
