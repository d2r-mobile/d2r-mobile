#!/usr/bin/env python3
"""给资源直接下载（Sources/asset_dl.m）出下载站目录：asset_manifest.py <资源文件夹（含 data/）> <下载站里的 assets 目录>
生成 <assets>/manifest.json（每个文件的名字、字节数、SHA-256；.holes 区段表当普通文件一起列）并把 <assets>/data 软链到资源正本（不拷第二份）。
校验值按（字节数, 修改时刻）缓存在 <assets> 旁的 .asset_sha.json，资源没变时重出清单不重算 4 GB。
Produce the download-site directory for direct asset download (Sources/asset_dl.m): asset_manifest.py <asset folder (containing data/)> <assets directory in the download site>
Generates <assets>/manifest.json (each file's name, byte count, SHA-256; the .holes extent table is listed as an ordinary file) and symlinks <assets>/data to the master copy of the assets (no second copy).
Checksums are cached by (byte count, modification time) in .asset_sha.json next to <assets>, so regenerating the manifest when the assets are unchanged does not recompute 4 GB."""
import hashlib, json, os, sys
src, out = os.path.realpath(os.path.join(sys.argv[1], "data")), sys.argv[2]
os.makedirs(out, exist_ok=True)
cache_p = os.path.join(os.path.dirname(os.path.abspath(out)), ".asset_sha.json")
try: cache = json.load(open(cache_p))
except Exception: cache = {}
files, total = [], 0
for nm in sorted(os.listdir(src)):
    p = os.path.join(src, nm)
    if nm.startswith(".") or not os.path.isfile(p): continue
    st = os.stat(p); key = f"{p}|{st.st_size}|{st.st_mtime_ns}"
    if key not in cache:
        h = hashlib.sha256()
        with open(p, "rb") as f:
            while b := f.read(1 << 22): h.update(b)
        cache[key] = h.hexdigest()
    files.append({"name": nm, "size": st.st_size, "sha256": cache[key]}); total += st.st_size
json.dump({k: v for k, v in cache.items() if k.split("|")[0].startswith(src)}, open(cache_p, "w"))
json.dump({"version": 1, "total": total, "files": files}, open(os.path.join(out, "manifest.json"), "w"), indent=0)
link = os.path.join(out, "data")
if os.path.islink(link): os.unlink(link)
os.symlink(src, link)
print(f"清单 {len(files)} 个文件共 {total} 字节 → {out}/manifest.json；data → {src}")
