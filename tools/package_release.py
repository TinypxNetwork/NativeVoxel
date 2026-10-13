"""Create a local portable build and a fixed-name playable snapshot.

Only explicit runtime/source paths are included. Private credentials, captures,
world saves, build outputs and the original MCP tree are never traversed.
"""
from __future__ import annotations
from datetime import datetime
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

root=Path(__file__).resolve().parents[1]
release=root/"releases"
release.mkdir(exist_ok=True)
stamp=datetime.now().strftime("%Y%m%d-%H%M%S")
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--executable",type=Path,default=root/"bin/NativeVoxel.exe")
args=parser.parse_args()
exe=args.executable.resolve()
if not exe.is_file(): raise SystemExit("Build the requested executable first")
snapshot=release/f"NativeVoxel-{stamp}.exe"
if snapshot.exists(): raise SystemExit("A release with this timestamp already exists")
shutil.copy2(exe,snapshot)
for dll in (root/"bin").glob("*.dll"):
    target=release/dll.name
    if not target.exists(): shutil.copy2(dll,target)
    elif hashlib.sha256(target.read_bytes()).digest()!=hashlib.sha256(dll.read_bytes()).digest():
        raise SystemExit(f"Runtime library differs from existing snapshots: {dll.name}")

files=[]
def add_file(path: Path):
    if path.is_symlink(): raise RuntimeError(f"Symlinks are not packaged: {path}")
    if path.is_file(): files.append(path)
def add_tree(path: Path):
    for child in sorted(path.rglob("*")):
        if "__pycache__" not in child.parts and "skin-cache" not in child.parts and child.suffix not in (".pyc",".o",".exe",".dll"):
            add_file(child)

add_file(exe)
for dll in sorted((root/"bin").glob("*.dll")): add_file(dll)
for name in ("assets.bin","atlas.rgba","font.rgba","atlas.json","atlas.png","block-registry.json",
             "effective-manifest.json","entity-model-preview.png","entity-model-report.json","import-report.json",
             "model-errors.json","model-report.json","model-variants.json","source-inventory.json",
             "state-models.json","vanilla-manifest.json"):
    add_file(root/"assets"/name)
for tree in ("assets/resources","assets/runtime","assets/vanilla","src","include","tools","tests","docs","third_party"):
    add_tree(root/tree)
# The native audio DLL is an intentional exception to the generic source-tree filter.
add_file(root/"assets/runtime/OpenAL64.dll")
for name in ("run.cmd","build.ps1","test.ps1","CMakeLists.txt","README.md","README.zh-CN.md","THIRD_PARTY_NOTICES.md","requirements-dev.txt",".gitignore",".gitattributes"):
    add_file(root/name)

archive=release/f"NativeVoxel-{stamp}-portable.zip"
manifest=[]
with zipfile.ZipFile(archive,"x",compression=zipfile.ZIP_DEFLATED,compresslevel=1) as package:
    for path in sorted(set(files)):
        relative="bin/NativeVoxel.exe" if path==exe else path.relative_to(root).as_posix()
        if relative.startswith(("private/","build/","saves/","releases/")):
            raise RuntimeError(f"Disallowed package path: {relative}")
        data=path.read_bytes()
        package.writestr("NativeVoxel/"+relative,data)
        manifest.append({"path":relative,"bytes":len(data),"sha256":hashlib.sha256(data).hexdigest()})
    package.writestr("NativeVoxel/release-manifest.json",json.dumps({"created":stamp,"files":manifest},ensure_ascii=False,indent=2))
with zipfile.ZipFile(archive) as package:
    bad=package.testzip()
    if bad: raise RuntimeError("ZIP verification failed: "+bad)
    if any("/private/" in n or n.endswith(".nvpc") for n in package.namelist()):
        raise RuntimeError("Private files unexpectedly reached package")
    packaged_names=set(package.namelist())
    layers=root/"assets/runtime/audio/sounds-layers.txt"
    if layers.is_file():
        for line in layers.read_text(encoding="utf-8").splitlines():
            if not line: continue
            domain,relative=line.split("\t",1)
            if "NativeVoxel/assets/"+relative not in packaged_names:
                raise RuntimeError("A referenced sound definition was not packaged: "+relative)
result={"snapshot":str(snapshot),"portable_zip":str(archive),"files":len(files),"zip_bytes":archive.stat().st_size,
        "exe_sha256":hashlib.sha256(exe.read_bytes()).hexdigest()}
(release/"latest.json").write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding="utf-8")
print(json.dumps(result,ensure_ascii=False,indent=2))
