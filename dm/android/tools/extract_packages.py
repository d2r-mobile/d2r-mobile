#!/usr/bin/env python3
"""将本体 NSP 与更新 NSP 合并提取，不做游戏版本或摘要校验。
Extract a base NSP with its update, without game version or digest validation.
"""
import argparse
from pathlib import Path
import re
import struct
import subprocess
import os


def run(hactool, keys, args, titlekey=None):
    command = [str(hactool), "-k", str(keys), "--disablekeywarns", "--suppresskeys"]
    if titlekey:
        command.append("--titlekey=" + titlekey.hex())
    result = subprocess.run(command + list(map(str, args)), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    # 不回显命令/底层输出，避免异常携带密钥。
    # Do not echo commands or raw tool output: errors must not expose keys.
    if result.returncode or b"Error:" in result.stdout or b"is corrupted" in result.stdout:
        raise RuntimeError("hactool failed; check tool/key compatibility and available disk space")
    return result.stdout.decode(errors="replace")


def ticket_key(directory, keys):
    tickets = list(directory.glob("*.tik"))
    if not tickets:
        return None
    ticket = tickets[0].read_bytes()
    signature = struct.unpack_from("<I", ticket)[0]
    lengths = {0x10000: 0x200, 0x10001: 0x100, 0x10002: 0x3c,
               0x10003: 0x200, 0x10004: 0x100, 0x10005: 0x3c}
    if signature not in lengths:
        raise RuntimeError("Unsupported ticket signature")
    base = (4 + lengths[signature] + 0x3f) & ~0x3f
    # hactool 的 --titlekey 接受票据里的加密密钥，工具内部解密。
    # hactool --titlekey expects the encrypted ticket key and decrypts it internally.
    return ticket[base + 0x40:base + 0x50]


def minimal_keyset(source, directory):
    # 旧 hactool 会拒绝新版文件中无关的不同长度字段。只保留提取所需字段。
    # Older hactool rejects unrelated fields with newer sizes; keep extraction-relevant fields only.
    fields = dict(re.findall(r"^\s*(\w+)\s*=\s*([0-9a-fA-F]+)\s*$", source.read_text(), re.M))
    private = directory / "private"
    private.mkdir(parents=True, exist_ok=True); private.chmod(0o700)
    target = private / "hactool.keys"
    fd = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as file:
        for key, value in fields.items():
            allowed = key == "header_key" or re.fullmatch(r"(master_key|titlekek|key_area_key_application)_[0-9a-f]{2}", key) or key in ["aes_kek_generation_source", "aes_key_generation_source", "titlekek_source", "key_area_key_application_source"]
            if allowed and len(value) == (64 if key == "header_key" else 32): file.write(key + " = " + value + "\n")
    target.chmod(0o600)
    return target


def program(directory, tool, keys, titlekey):
    for file in directory.glob("*.nca"):
        info = run(tool, keys, ["-t", "nca", file], titlekey)
        if re.search(r"Content Type:\s*Program", info) and re.search(r"Title ID:\s*0100726014352000", info):
            return file
    raise RuntimeError("Game program NCA not found")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("base", type=Path)
    p.add_argument("update", type=Path)
    p.add_argument("keys", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--hactool", type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    cache = a.output.parent / "package-ncas"
    a.keys = minimal_keyset(a.keys, cache)
    for name, package in [("base", a.base), ("update", a.update)]:
        directory = cache / name
        directory.mkdir(parents=True, exist_ok=True)
        if not (directory / ".extracted").exists():
            print("Extracting", name, flush=True)
            run(a.hactool, a.keys, ["-t", "pfs0", "--outdir=" + str(directory), package])
            (directory / ".extracted").touch()
    base_key = ticket_key(cache / "base", a.keys)
    update_key = ticket_key(cache / "update", a.keys)
    base = program(cache / "base", a.hactool, a.keys, base_key)
    update = program(cache / "update", a.hactool, a.keys, update_key)
    # 本体和更新可能使用不同的标题密钥；解密本体到本机缓存供 BKTR 叠加。
    # Base and update may use different title keys; decrypt the base locally for BKTR layering.
    plain = cache / "base-plain.nca"
    if not plain.exists():
        print("Preparing base content", flush=True)
        run(a.hactool, a.keys, ["-t", "nca", "--plaintext=" + str(plain), base], base_key)
    print("Extracting updated ExeFS and RomFS", flush=True)
    run(a.hactool, a.keys, ["-t", "nca", "--basenca=" + str(plain),
                            "--exefsdir=" + str(a.output / "ExeFS"),
                            "--romfsdir=" + str(a.output / "RomFS"), update], update_key)
    if not (a.output / "ExeFS/main").is_file() or not (a.output / "RomFS/data").is_dir():
        raise RuntimeError("hactool did not produce the required extraction outputs")
    print("Ready:", a.output, flush=True)
