"""Compare native GUI wrapping with the actual MCP FontRenderer method bodies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def method(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, default=ROOT / "build/agent-ui/gui_hud_tests.exe")
    parser.add_argument("--mcp", type=Path, default=ROOT.parent / "MCP-919-main")
    args = parser.parse_args()
    build = ROOT / "build/gui_text_reference"
    build.mkdir(parents=True, exist_ok=True)
    path = args.mcp / "src/minecraft/net/minecraft/client/gui/FontRenderer.java"
    source = path.read_text(encoding="utf-8")
    signatures = ["public List<String> listFormattedStringToWidth", "String wrapFormattedStringToWidth",
                  "private int sizeStringToWidth", "private static boolean isFormatColor",
                  "private static boolean isFormatSpecial", "public static String getFormatFromString"]
    java = """import java.io.*;import java.nio.file.*;import java.nio.charset.*;import java.util.*;
public class HudFontReference {
byte[] widths;boolean unicode;
int getCharWidth(char c){int v=widths[(unicode?65536:0)+c]&255;return v==255?-1:v;}
""" + "\n".join(method(source, signature) for signature in signatures) + """
static String read(DataInputStream in)throws Exception {int n=in.readInt();byte[] b=new byte[n];in.readFully(b);return new String(b,StandardCharsets.UTF_8);}
static void write(DataOutputStream out,String s)throws Exception {byte[] b=s.getBytes(StandardCharsets.UTF_8);out.writeInt(b.length);out.write(b);}
public static void main(String[] args)throws Exception {
HudFontReference font=new HudFontReference();font.widths=Files.readAllBytes(Paths.get(args[0]));
try(DataInputStream in=new DataInputStream(new FileInputStream(args[1]));DataOutputStream out=new DataOutputStream(new FileOutputStream(args[2]))){
int count=in.readInt();out.writeInt(count);for(int i=0;i<count;i++){int width=in.readInt();font.unicode=in.readByte()!=0;String text=read(in);List<String> lines=font.listFormattedStringToWidth(text,width);out.writeInt(lines.size());for(String line:lines)write(out,line);}}
}}
"""
    (build / "HudFontReference.java").write_text(java, encoding="utf-8")
    texts = ["", "a", "a\nb\n", "\n", "a  b c", "hello world", "\u00a7aGreen text wraps in green",
             "\u00a7lBold \u00a7cRed \u00a7rReset", "\u00a7a\u00a7lBold with colour",
             "\u00a7kHidden \u00a7mStrike \u00a7nUnderline \u00a7oItalic", "中文测试 自动换行 和颜色",
             "\u00a79第一行\n第二行", "\u00a7a", "trailing newline\n\u00a7r"]
    randomizer = random.Random(189)
    alphabet = list("abcXYZ 0123456789中文测试") + ["\u00a7a", "\u00a7l", "\u00a7r", "\n"]
    texts += ["".join(randomizer.choice(alphabet) for _ in range(randomizer.randrange(1, 55))) for _ in range(100)]
    cases = [(width, unicode, text) for text in texts for width in (12, 18, 24, 40, 70, 150, 305) for unicode in (False, True)]
    data = bytearray(struct.pack(">i", len(cases)))
    for width, unicode, text in cases:
        raw = text.encode("utf-8")
        data += struct.pack(">iBi", width, unicode, len(raw)) + raw
    fixture = build / "fixtures.bin"
    fixture.write_bytes(data)
    subprocess.run(["javac", "-encoding", "UTF-8", str(build / "HudFontReference.java")], check=True)
    java_output, native_output = build / "java.bin", build / "native.bin"
    subprocess.run(["java", "-cp", str(build), "HudFontReference", str(ROOT / "tools/reference/font_width_reference.bin"), str(fixture), str(java_output)], check=True)
    env = dict(os.environ)
    env["PATH"] = "D:/Strawberry/c/bin" + os.pathsep + env.get("PATH", "")
    subprocess.run([str(args.native.resolve()), str(ROOT / "assets"), "--wrap-fixtures", str(fixture), str(native_output)], check=True, env=env)
    def decode(path):
        data = memoryview(path.read_bytes());pos = 0
        def integer():
            nonlocal pos
            value, = struct.unpack_from(">i", data, pos);pos += 4;return value
        result = []
        for _ in range(integer()):
            lines = []
            for _ in range(integer()):
                size = integer();lines.append(bytes(data[pos:pos+size]).decode("utf-8"));pos += size
            result.append(lines)
        assert pos == len(data)
        return result
    expected, actual = decode(java_output), decode(native_output)
    mismatches = [{"case": cases[i], "java": a, "native": b} for i, (a, b) in enumerate(zip(expected, actual)) if a != b]
    report = {"cases": len(cases), "mismatches": len(mismatches), "font_renderer_sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "scope": "Original FontRenderer GUI string wrapping with the existing original 131072 font-width fixture"}
    (build / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (build / "mismatches.json").write_text(json.dumps(mismatches, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))
    if mismatches:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
