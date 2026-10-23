# 发布壳（.hap 或 .app）的零游戏内容自检，三项同安卓出包自检，有一项不过就返回 1：
#   ① 包里没有运行包：resources/resfile/dm/…、.xgs、.patched.bin、overlay/、derived.tsv（都由游戏文件生成，只许进调试版或玩家的导入包）；
#   ② 整个包解开查不到本机路径（/Users/…、/home/…、C:\Users\…；编译时 -ffile-prefix-map 已映射掉，见 native/CMakeLists.txt）；
#   ③ 给了 --gen <生成目录> 时：真表（dm_vault_tab.h、dm_hud_exp.h、dm_touch_native_art.c）里源码中本来没有的字符串（物品键、图标路径等，
#      至少取 100 个），在每个 .so 里都是 0 处——壳编的是替身，真表不能漏进去。
# 用法：python -I scripts/check_release_clean.py <壳.hap|.app> [--gen <生成目录>]
# Zero-game-content check of the release shell (.hap or .app); the three checks of Android's package self-check, exits 1 if any fails:
#   (1) no runtime pack in the package: resources/resfile/dm/..., .xgs, .patched.bin, overlay/, derived.tsv (all generated from game files; debug builds
#       or the player's import pack only);
#   (2) no machine paths anywhere in the unpacked package (/Users/..., /home/..., C:\Users\...; mapped away at compile time by -ffile-prefix-map, see
#       native/CMakeLists.txt);
#   (3) with --gen <generated dir>: strings of the real tables (dm_vault_tab.h, dm_hud_exp.h, dm_touch_native_art.c) that the sources do not already
#       contain (item keys, icon paths and so on, at least 100 of them) occur 0 times in every .so: the shell compiles the stubs, the real tables must not leak in.
# Usage: python -I scripts/check_release_clean.py <shell.hap|.app> [--gen <generated dir>]
import hashlib
import io
import os
import re
import subprocess
import sys
import zipfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))
PATHS = [re.compile(rb'/Users/[A-Za-z0-9_.-]+/'), re.compile(rb'/home/[A-Za-z0-9_.-]+/'), re.compile(rb'[A-Za-z]:[\\/]+Users[\\/]+', re.I)]
TABLES = ('dm_vault_tab.h', 'dm_hud_exp.h', 'dm_touch_native_art.c')


def entries(zf, prefix=''):
    """逐个给出 (包内路径, 字节)，.hap 套在 .app 里的也展开 | Yield (path in package, bytes), unpacking .hap files nested in an .app"""
    for name in zf.namelist():
        data = zf.read(name)
        if name.lower().endswith('.hap'):
            with zipfile.ZipFile(io.BytesIO(data)) as inner:
                yield from entries(inner, prefix + name + '!')
        else:
            yield prefix + name, data


def runtime_pack(name):
    low = name.lower().split('!')[-1]
    return (low.startswith('resources/resfile/dm/') or low.endswith('.xgs') or low.endswith('.patched.bin')
            or '/overlay/' in '/' + low or low.endswith('derived.tsv'))


def table_strings(gen):
    """真表里的字符串字面量，去掉源码里本来就有的 | String literals of the real tables, minus those the sources already contain"""
    found = set()
    for t in TABLES:
        p = os.path.join(gen, t)
        if os.path.isfile(p):
            found |= {m.encode() for m in re.findall(r'"((?:[^"\\\n]|\\.){6,})"', open(p, encoding='utf-8', errors='replace').read()) if '\\' not in m}
    # 只从受版本管理的源码扣除，不能让工作树里现生成的真实资源遮住泄漏；.inc 也是源码（技能索引的诊断名在这里）。
    # Subtract tracked sources only, never freshly generated game resources that could hide leaks; .inc is source too (skill-index diagnostic names).
    tracked = subprocess.check_output(['git', 'ls-files', '-z', '--', 'dm'], cwd=REPO).split(b'\0')
    source = []
    for name in filter(None, tracked):
        path = os.path.join(REPO, os.fsdecode(name)); f = os.path.basename(path)
        if f.endswith(('.c', '.h', '.inc', '.m', '.cpp', '.py', '.ets', '.ts', '.json', '.tsv', '.md')) and f not in TABLES:
            source.append(open(path, 'rb').read())
    # 替身保留这三份仓库元数据的摘要来标明适配版本；只扣除当前文件实算的摘要，不放行任意十六进制串。
    # Stubs retain the digests of these three repository metadata files to identify the adaptation; subtract their actual digests, not arbitrary hex strings.
    for name in ('dm/ios/Sources/TouchArt/native_art_manifest.json', 'dm/ios/docs/native_art_v1_geometry.json',
                 'dm/ios/docs/touch_layout_spec_native_binding_v5.json'):
        source.append(hashlib.sha256(open(os.path.join(REPO, name), 'rb').read()).hexdigest().encode())
    blob = b'\0'.join(source)
    return sorted(s for s in found if s not in blob)


def main():
    args = sys.argv[1:]
    gen = None
    if '--gen' in args:
        i = args.index('--gen'); gen = args[i + 1]; del args[i:i + 2]
    with zipfile.ZipFile(args[0]) as z:
        items = list(entries(z))
    fail = 0
    bad = [n for n, _ in items if runtime_pack(n)]
    if bad:
        print('NOT CLEAN (1): %d runtime pack files, e.g. %s' % (len(bad), bad[0])); fail = 1
    else:
        print('ok (1): no runtime pack')
    hits = [(n, m.group().decode('latin-1')) for n, d in items for rx in PATHS for m in [rx.search(d)] if m]
    if hits:
        print('NOT CLEAN (2): machine path in %d files, e.g. %s: %s' % (len(hits), hits[0][0], hits[0][1])); fail = 1
    else:
        print('ok (2): no machine paths')
    if gen:
        keys = table_strings(gen)
        if len(keys) < 100:
            print('NOT CLEAN (3): only %d table-only strings found in %s (need >= 100 to judge)' % (len(keys), gen)); fail = 1
        else:
            leaks = [(n, k) for n, d in items if n.endswith('.so') for k in keys if k in d]
            if leaks:
                print('NOT CLEAN (3): %d real-table strings in %s, e.g. %r' % (len(leaks), leaks[0][0], leaks[0][1])); fail = 1
            else:
                print('ok (3): none of %d real-table strings in any .so' % len(keys))
    else:
        print('skipped (3): pass --gen <generated dir> to check the real tables')
    sys.exit(fail)


main()
