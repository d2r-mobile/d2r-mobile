#!/usr/bin/env python3
# 壳用：把三份「从游戏文件生成、编进代码」的文件拆成两半——
#   数据   → 一份 derived.tsv（随使用者自己的游戏文件生成，放进安装包的 dm/）
#   替身   → 同名的三份源文件，里面没有数据，只有大小固定的空表和从 derived.tsv 填表的几行（host/dm_derived_rt.h）：
#            构造函数登记读表函数，苹果 / 安卓登记时当场读，鸿蒙等平台层设好路径后调 dm_derived_reload() 再读
# 这样壳的可执行文件里不含这三张表的内容，而用它们的代码（diag.c、dm_vault_ui.c、DmTouchOverlay.m …）一行不用改。
# 表的条数会写进替身（数组大小）：条数随游戏版本固定，不是游戏内容。
# 用法：shell_stubs.py data  <dm 目录> <输出 derived.tsv>     读真实的三份生成文件，写数据（d2rm_pack.py 与 shell_build.sh 都用）
#       shell_stubs.py stubs <dm 目录>                       把三份生成文件原地换成替身（只在 shell_build.sh 的临时副本里做）
#       shell_stubs.py data-gen / stubs-gen <生成目录> …      同上，三份文件都在一个目录里（安卓构建的 DM_GEN_DIR：dm/android/tools/shell_apk.sh、dm/tools/d2rm_android.py）
# For the shell: split the three files that are "generated from game files and compiled in" into two halves --
#   data       → one derived.tsv (generated from the user's own game files, put into dm/ of the install package)
#   stand-ins  → three source files of the same names with no data, only fixed-size empty tables and a few lines that fill them from derived.tsv (host/dm_derived_rt.h):
#                the constructors register the loaders; Apple / Android read when registering, HarmonyOS reads when the platform layer calls dm_derived_reload() once the path is set
# So the shell's executable contains none of these tables, and the code that uses them (diag.c, dm_vault_ui.c, DmTouchOverlay.m ...) is unchanged.
# The table sizes are written into the stand-ins (array sizes): they are fixed per game version and are not game content.
# Usage: shell_stubs.py data  <dm directory> <output derived.tsv>     read the three real generated files and write the data (used by d2rm_pack.py and shell_build.sh)
#        shell_stubs.py stubs <dm directory>                          replace the three generated files with stand-ins in place (only in shell_build.sh's temporary copy)
#        shell_stubs.py data-gen / stubs-gen <generated directory> ...  the same, with all three files in one directory (the DM_GEN_DIR of the Android build: dm/android/tools/shell_apk.sh, dm/tools/d2rm_android.py)
import os, re, sys

CSTR = r'"((?:[^"\\]|\\.)*)"'


def unq(s): return bytes(s, 'utf-8').decode('unicode_escape').encode('latin-1').decode('utf-8') if '\\' in s else s


def paths(dm, gen=None):
    if gen: return tuple(os.path.join(gen, f) for f in ('dm_hud_exp.h', 'dm_vault_tab.h', 'dm_touch_native_art.c'))
    return (os.path.join(dm, 'host', 'dm_hud_exp.h'), os.path.join(dm, 'host', 'dm_vault_tab.h'), os.path.join(dm, 'ios', 'Sources', 'dm_touch_native_art.c'))


def parse(dm, gen=None):
    hud, vault, art = (open(p, encoding='utf-8').read() for p in paths(dm, gen))
    exp = [int(x) for x in re.search(r'k_exp\[100\] = \{(.*?)\};', hud, re.S).group(1).replace('\n', ' ').split(',') if x.strip()]
    assert len(exp) == 100, len(exp)
    parts = [unq(x) for x in re.findall(CSTR, re.search(r'vt_part_name\[VT_NPART\] = \{(.*?)\};', vault).group(1))]
    rows = [(int(a), int(b), int(c), int(d), int(e), int(f), unq(k), unq(i)) for a, b, c, d, e, f, k, i in
            re.findall(r'^\s*\{ (\d+), (\d+), (\d+), (\d+), (\d+), (-?\d+), %s, %s \},$' % (CSTR, CSTR), vault, re.M)]
    sets = [unq(x) for x in re.findall(CSTR, re.search(r'vt_set_key\[\] = \{(.*?)\};', vault).group(1))]
    assert rows and sets and len(parts) == int(re.search(r'#define VT_NPART (\d+)', vault).group(1))
    assert vault.count('\n    { ') == len(rows), (vault.count('\n    { '), len(rows))
    m = re.search(r'static const struct \{ uint32_t id; int atlas,frame; \} rows\[\]=\{\n(.*?)\n\};', art, re.S)
    trows = [tuple(int(x) for x in t) for t in re.findall(r'^ \{(\d+),(-?\d+),(-?\d+)\},$', m.group(1), re.M)]
    assert len(trows) == m.group(1).count('\n') + 1, (len(trows), m.group(1).count('\n') + 1)
    return exp, parts, rows, sets, trows, (hud, vault, art, m)


def data(dm, dst, gen=None):
    exp, parts, rows, sets, trows, _ = parse(dm, gen)
    L = ['EXP\t' + '\t'.join(map(str, exp[i:i + 10])) for i in range(0, 100, 10)]
    L += ['VTPART\t' + p for p in parts] + ['VTROW\t' + '\t'.join(map(str, r)) for r in rows] + ['VTSET\t' + s for s in sets]
    L += ['TAROW\t%d\t%d\t%d' % t for t in trows]
    for l in L: assert '\n' not in l and l.count('\t') < 16
    for r in rows: assert '\t' not in r[6] and '\t' not in r[7]
    with open(dst, 'w', encoding='utf-8', newline='\n') as f: f.write('\n'.join(L) + '\n')
    return len(exp), len(parts), len(rows), len(sets), len(trows)


def stubs(dm, gen=None):
    exp, parts, rows, sets, trows, (hud, vault, art, m) = parse(dm, gen)
    p_hud, p_vault, p_art = paths(dm, gen)
    open(p_hud, 'w', encoding='utf-8').write('''// 壳的替身（ios/tools/shell_stubs.py 生成）：经验表不编进代码，启动时从安装包的 dm/derived.tsv 读（标签 EXP，每行 10 个）
#pragma once
#include <stdint.h>
#include "dm_derived_rt.h"
static uint32_t k_exp[100];
static void k_exp_row(char **f, int n, void *ctx) { int *i = ctx; for (int k = 0; k < n && *i < 100; k++) k_exp[(*i)++] = (uint32_t)strtoul(f[k], NULL, 10); }
static int k_exp_load(void) { int i = 0; dm_derived_each("EXP", k_exp_row, &i); return i == 100 ? 0 : -1; }
__attribute__((constructor)) static void k_exp_reg(void) { dm_derived_register(k_exp_load); }
''')
    open(p_vault, 'w', encoding='utf-8').write('''// 壳的替身（ios/tools/shell_stubs.py 生成）：编年史仓库固定表不编进代码，启动时从安装包的 dm/derived.tsv 读
// （标签 VTPART / VTROW / VTSET）。条数随游戏版本固定，写成数组大小；内容全在数据文件里。
#include "dm_derived_rt.h"
#define VT_NPART %d
static const char *vt_part_name[VT_NPART];
typedef struct { uint8_t kind, part, tier; uint16_t row, lvl; int16_t set; const char *key, *icon; } vt_row;
static vt_row vt_rows[%d];
#define VT_NROW (int)(sizeof vt_rows / sizeof vt_rows[0])
static const char *vt_set_key[%d];
#define VT_NSET (int)(sizeof vt_set_key / sizeof vt_set_key[0])
static void vt_part_cb(char **f, int n, void *ctx) { int *i = ctx; if (n >= 1 && *i < VT_NPART) vt_part_name[(*i)++] = strdup(f[0]); }
static void vt_set_cb(char **f, int n, void *ctx) { int *i = ctx; if (n >= 1 && *i < VT_NSET) vt_set_key[(*i)++] = strdup(f[0]); }
static void vt_row_cb(char **f, int n, void *ctx) { int *i = ctx; if (n < 8 || *i >= VT_NROW) return;
    vt_rows[(*i)++] = (vt_row){ (uint8_t)atoi(f[0]), (uint8_t)atoi(f[1]), (uint8_t)atoi(f[2]), (uint16_t)atoi(f[3]), (uint16_t)atoi(f[4]), (int16_t)atoi(f[5]), strdup(f[6]), strdup(f[7]) }; }
static int vt_load(void) { int a = 0, b = 0, c = 0;
    dm_derived_each("VTPART", vt_part_cb, &a); dm_derived_each("VTROW", vt_row_cb, &b); dm_derived_each("VTSET", vt_set_cb, &c);
    for (int i = a; i < VT_NPART; i++) vt_part_name[i] = ""; for (int i = c; i < VT_NSET; i++) vt_set_key[i] = "";
    for (int i = b; i < VT_NROW; i++) vt_rows[i] = (vt_row){ .set = -1, .key = "", .icon = "" };
    if (a == VT_NPART && b == VT_NROW && c == VT_NSET) return 0;
    fprintf(stderr, "[dm] 【红】编年史仓库表没读全：部位 %%d / %%d、条目 %%d / %%d、套装 %%d / %%d\\n", a, VT_NPART, b, VT_NROW, c, VT_NSET); return -1; }
__attribute__((constructor)) static void vt_reg(void) { dm_derived_register(vt_load); }
''' % (len(parts), len(rows), len(sets)))
    stub = art[:m.start()] + '''static struct { uint32_t id; int atlas,frame; } rows[%d];   /* 壳的替身：技能索引不编进代码，启动时从安装包的 dm/derived.tsv 读（标签 TAROW） */
static void ta_row_cb(char **f, int n, void *ctx) { int *i = ctx; if (n < 3 || *i >= (int)(sizeof rows / sizeof rows[0])) return;
    rows[*i].id = (uint32_t)strtoul(f[0], NULL, 10); rows[*i].atlas = atoi(f[1]); rows[*i].frame = atoi(f[2]); (*i)++; }
static int ta_load(void) { int i = 0; dm_derived_each("TAROW", ta_row_cb, &i);
    for (int k = i; k < (int)(sizeof rows / sizeof rows[0]); k++) { rows[k].id = UINT32_MAX - 1; rows[k].atlas = rows[k].frame = -1; }
    if (i == (int)(sizeof rows / sizeof rows[0])) return 0;
    fprintf(stderr, "[dm] 【红】技能图标索引没读全：%%d / %%d\\n", i, (int)(sizeof rows / sizeof rows[0])); return -1; }
__attribute__((constructor)) static void ta_reg(void) { dm_derived_register(ta_load); }''' % len(trows) + art[m.end():]
    stub = stub.replace('#include <stddef.h>', '#include <stddef.h>\n#include "%s"' % ('dm_derived_rt.h' if gen else '../../host/dm_derived_rt.h'), 1); assert 'dm_derived_rt.h' in stub   # 生成目录里的那份靠头文件搜索路径找 dm/host | the copy in the generated directory finds dm/host through the header search path
    open(p_art, 'w', encoding='utf-8').write(stub)
    return len(parts), len(rows), len(sets), len(trows)


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == 'data': print('derived.tsv：经验 %d、部位 %d、仓库条目 %d、套装 %d、技能索引 %d' % data(sys.argv[2], sys.argv[3]))
    elif len(sys.argv) == 3 and sys.argv[1] == 'stubs': print('替身已写：部位 %d、仓库条目 %d、套装 %d、技能索引 %d' % stubs(sys.argv[2]))
    elif len(sys.argv) == 4 and sys.argv[1] == 'data-gen': print('derived.tsv：经验 %d、部位 %d、仓库条目 %d、套装 %d、技能索引 %d' % data(None, sys.argv[3], sys.argv[2]))
    elif len(sys.argv) == 3 and sys.argv[1] == 'stubs-gen': print('替身已写：部位 %d、仓库条目 %d、套装 %d、技能索引 %d' % stubs(None, sys.argv[2]))
    else: sys.exit('用法：shell_stubs.py data <dm 目录> <输出 derived.tsv> | stubs <dm 目录> | data-gen <生成目录> <输出 derived.tsv> | stubs-gen <生成目录>')
