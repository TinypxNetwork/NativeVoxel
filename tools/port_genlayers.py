#!/usr/bin/env python3
"""Mechanically adapt original GenLayer cell rules into private C++ classes.

Seed arithmetic and graph ownership live in worldgen_biomes.cpp. This generator
retains Java branch/evaluation order and emits reviewable native source.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
from verify_physics import java_block

ORDER=["GenLayerIsland","GenLayerZoom","GenLayerFuzzyZoom","GenLayerAddIsland","GenLayerRemoveTooMuchOcean","GenLayerAddSnow","GenLayerEdge","GenLayerAddMushroomIsland","GenLayerDeepOcean","GenLayerRiverInit","GenLayerBiome","GenLayerBiomeEdge","GenLayerHills","GenLayerRareBiome","GenLayerShore","GenLayerRiver","GenLayerSmooth","GenLayerRiverMix","GenLayerVoronoiZoom"]

def transform(text:str)->str:
    text=re.sub(r"BiomeGenBase\.getBiome\(([^()]*)\) instanceof BiomeGenMesa",r"isMesa(\1)",text)
    text=re.sub(r"BiomeGenBase\.(\w+)\.biomeID",r"biome_ids::\1",text)
    text=text.replace("BiomeGenBase.getBiome(","biomeDefinition(")
    text=re.sub(r"biomeDefinition\(([^()]*)\)\.",r"biomeDefinition(\1)->",text)
    text=text.replace("BiomeGenJungle.class","BiomeClass::BiomeGenJungle")
    text=text.replace("BiomeGenBase.TempCategory","TemperatureCategory")
    text=re.sub(r"TemperatureCategory\.(\w+)",r"TemperatureCategory::\1",text)
    text=re.sub(r"BiomeGenBase (biomegenbase\w*)",r"const BiomeDescription* \1",text)
    text=re.sub(r"(biomegenbase\w*)\.",r"\1->",text)
    text=text.replace("null","nullptr").replace("boolean","bool").replace("$","_")
    # Coordinate shifts can operate on negative values. Multiply after widening
    # so C++ never left-shifts a negative signed integer.
    text=re.sub(r"\(long\)\(([^()]*) << ([12])\)",lambda m:"(std::int64_t("+m[1]+") * "+str(1<<int(m[2]))+")",text)
    text=text.replace("(long)","(std::int64_t)").replace("long seed","std::int64_t seed")
    text=re.sub(r"area([XY]) >> ([12])",lambda m:"floorPower2(area"+m[1]+","+m[2]+")",text)
    text=text.replace("super.initWorldGenSeed","GenLayer::initWorldGenSeed")
    text=text.replace("this.","this->")
    for field in ["parent","field_151628_d","biomePatternGeneratorChain","riverPatternGeneratorChain"]:
        text=text.replace("this->"+field+".","this->"+field+"->")
    text=text.replace("IntCache.getIntCache(","makeArray(").replace("System.arraycopy(","copyArray(")
    text=re.sub(r"new int\[\]\s*", "",text)
    text=text.replace("int[]","IntArray").replace(".length",".size()").replace("].biomeID","]")
    text=re.sub(r"(?<=\d)D\b","",text)
    text=text.replace("k - 1 << 1","(k - 1) << 1").replace("l - 1 << 1","(l - 1) << 1")
    text=text.replace("k - 1 << 2","(k - 1) << 2").replace("l - 1 << 2","(l - 1) << 2")
    text=text.replace("k2 != 0 || k1 == 0 && l1 == 0 && i2 == 0 && j2 == 0","k2 != 0 || (k1 == 0 && l1 == 0 && i2 == 0 && j2 == 0)")
    text=text.replace("1 + this->nextInt(15) << 8 & 3840","((1 + this->nextInt(15)) << 8) & 3840")
    text=text.replace("double d0 = 3.6;","")
    for mode in ["COOL_WARM","HEAT_ICE","SPECIAL"]:text=text.replace("case "+mode+":","case Mode::"+mode+":")
    return text


def main()->None:
    root=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--mcp",type=Path,default=root.parent/"MCP-919-main");args=parser.parse_args()
    source=args.mcp.resolve()/"src/minecraft/net/minecraft/world/gen/layer";out=["// Generated from original GenLayer cell rules by tools/port_genlayers.py."]
    for name in ORDER:
        java=(source/(name+".java")).read_text(encoding="utf-8")
        base="GenLayerZoom" if name=="GenLayerFuzzyZoom" else "GenLayer"
        out += [f"class {name} final : public {base} {{" if name!="GenLayerZoom" else f"class {name} : public {base} {{","public:"]
        if name=="GenLayerIsland":out.append(f"    explicit {name}(std::int64_t seed):GenLayer(seed){{}}")
        elif name=="GenLayerFuzzyZoom":out.append(f"    {name}(std::int64_t seed,Layer parent):GenLayerZoom(seed,std::move(parent)){{}}")
        elif name=="GenLayerHills":out += ["    Layer field_151628_d;",f"    {name}(std::int64_t seed,Layer parent,Layer river):GenLayer(seed,std::move(parent)),field_151628_d(std::move(river)){{}}"]
        elif name=="GenLayerRiverMix":out += ["    Layer biomePatternGeneratorChain,riverPatternGeneratorChain;",f"    {name}(std::int64_t seed,Layer biomes,Layer river):GenLayer(seed),biomePatternGeneratorChain(std::move(biomes)),riverPatternGeneratorChain(std::move(river)){{}}"]
        elif name=="GenLayerEdge":out += ["    enum class Mode {COOL_WARM,HEAT_ICE,SPECIAL};","    Mode field_151627_c;",f"    {name}(std::int64_t seed,Layer parent,Mode mode):GenLayer(seed,std::move(parent)),field_151627_c(mode){{}}"]
        elif name=="GenLayerBiome":
            for field,values in re.findall(r"private BiomeGenBase\[\] (\w+) = new BiomeGenBase\[\] \{([^}]*)\};",java):
                ids=re.findall(r"BiomeGenBase\.(\w+)",values);out.append(f"    std::array<int,{len(ids)}> {field}{{{{"+",".join("biome_ids::"+id for id in ids)+"}};")
            out.append(f"    {name}(std::int64_t seed,Layer parent,WorldType):GenLayer(seed,std::move(parent)){{}}")
        else:out.append(f"    {name}(std::int64_t seed,Layer parent):GenLayer(seed,std::move(parent)){{}}")
        pattern=r"(?:public|private|protected) (?:final )?(int\[\]|int|boolean|void) (\w+)\(([^()]*)\)\s*\{"
        for match in re.finditer(pattern,java):
            kind,method,params=match.groups();body=java_block(java,match[0][:-1].rstrip());body=body[body.index('{'):]
            if name=="GenLayerBiome":
                at=body.index("if (this.field_175973_g");old=java_block(body,"if (this.field_175973_g");tail=body[at+len(old):];tail=re.sub(r"^\s*else\s*","",tail,count=1);body=body[:at]+tail
            if name=="GenLayerHills":
                old=java_block(body,"if (k > 255)");body=body.replace(old,"")
            params=re.sub(r"int\[\] (\w+)",r"IntArray& \1",params)
            signature=transform(kind+" "+method+"("+params+")")
            if method in ["getInts","initWorldGenSeed","selectModeOrRandom"]:signature+=" override"
            out += ["    "+signature+" "+transform(body)]
        out += ["};",""]
    target=root/"src/worldgen_layers.inc";target.write_text("\n".join(out),encoding="utf-8")
    report={"layers":len(ORDER),"source_sha256":{name:hashlib.sha256((source/(name+".java")).read_bytes()).hexdigest() for name in ORDER},"generated_sha256":hashlib.sha256(target.read_bytes()).hexdigest()}
    reportpath=root/"build/worldgen_reference/layer_source_report.json";reportpath.parent.mkdir(parents=True,exist_ok=True);reportpath.write_text(json.dumps(report,indent=2))
    print(f"Emitted {len(ORDER)} original GenLayer rule classes to {target}")


if __name__=="__main__":main()
