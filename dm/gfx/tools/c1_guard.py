#!/usr/bin/env python3
import datetime
import os
import re
import sys


ACCESS = re.compile(r"(?:fp|vp)_c1\)\._m0\[([^\]]+)\]")
DEFINITION = re.compile(r"\b(?:u?int|long|short)\s+(_\d+)\s*=\s*([^;]+);")
VARIABLE = re.compile(r"\b(_\d+)\b")


def bounded(index, definitions):
    names = VARIABLE.findall(index)
    current = names[-1] if names else index.strip()
    for _ in range(8):
        expression = definitions.get(current)
        if expression is None:
            return False
        if re.search(r"\bmin\s*\(|\bclamp\s*\(|&\s*(?:0x[0-9a-fA-F]+|\d+)", expression):
            return True
        names = VARIABLE.findall(expression)
        if not names:
            return False
        current = names[-1]
    return False


def main():
    if len(sys.argv) != 3:
        raise SystemExit(f"用法：{sys.argv[0]} <dmsh_b 版本目录> <输出 tsv>")
    version_dir, output = map(os.path.abspath, sys.argv[1:])
    keys = []
    for name in sorted(os.listdir(version_dir)):
        if not name.endswith(".metal"):
            continue
        path = os.path.join(version_dir, name)
        with open(path, encoding="utf-8") as stream:
            source = stream.read()
        accesses = ACCESS.findall(source)
        if not accesses:
            continue
        definitions = dict(DEFINITION.findall(source))
        if any(not index.strip().isdigit() and not bounded(index, definitions) for index in accesses):
            keys.append(name[:-len("_b.metal")] if name.endswith("_b.metal") else name[:-6])
    os.makedirs(os.path.dirname(output), exist_ok=True)
    with open(output, "w", encoding="utf-8", newline="") as stream:
        stamp = datetime.datetime.now().astimezone().isoformat(timespec="seconds")
        stream.write(f"# chain {os.path.basename(version_dir)} {stamp}\n")
        for key in keys:
            stream.write(key + "\n")
    print(f"未证有界：{len(keys)} 个着色器")


if __name__ == "__main__":
    main()
