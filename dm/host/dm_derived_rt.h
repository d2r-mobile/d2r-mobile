// 壳（不含任何游戏内容的 App）里，从游戏文件生成的几张表不编进代码，运行时从安装包里的 dm/derived.tsv 读。
// 这个文件只被壳构建时生成的替身文件包含（ios/tools/shell_stubs.py 写出的 dm_hud_exp.h、dm_vault_tab.h、dm_touch_native_art.c），
// 内测 / 自己用 Xcode 构建的包不走这里：它们的三份生成文件里是真实数据。
// derived.tsv 由使用者自己的游戏文件生成（dm/tools/d2rm_pack.py）：每行「标签<TAB>字段<TAB>…」，UTF-8，字段里没有制表符与换行。
// In the shell (the App containing no game content at all), the few tables generated from the game files are not compiled into the code; at run time they are read from dm/derived.tsv in the install package.
// This file is included only by the stub files generated when the shell is built (dm_hud_exp.h, dm_vault_tab.h, dm_touch_native_art.c written by ios/tools/shell_stubs.py);
// beta packages / packages you build yourself with Xcode do not go through here: their three generated files hold real data.
// derived.tsv is generated from the user's own game files (dm/tools/d2rm_pack.py): each line is "tag<TAB>field<TAB>...", UTF-8, with no tabs or newlines inside fields.
#pragma once
#include <mach-o/dyld.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// 对每一行标签为 tag 的记录调一次 cb（f[0..n) 是各字段，指向本函数持有的行缓冲；要留着的字符串自己 strdup）。返回这种记录的条数，读不到文件 = -1
// Calls cb once for every record line whose tag is tag (f[0..n) are the fields, pointing into the line buffer owned by this function; strdup any strings you want to keep). Returns the number of records of this kind; file cannot be read = -1
static inline int dm_derived_each(const char *tag, void (*cb)(char **f, int n, void *ctx), void *ctx) {
    char p[4096]; uint32_t sz = sizeof p; if (_NSGetExecutablePath(p, &sz)) return -1;
    char *sl = strrchr(p, '/'); if (!sl || (size_t)(sl - p) + 24 > sizeof p) return -1; strcpy(sl, "/dm/derived.tsv");
    FILE *fp = fopen(p, "r"); if (!fp) { fprintf(stderr, "[dm] 【红】安装包里没有 dm/derived.tsv（%s）：这个包没有用游戏文件做全\n", p); return -1; }
    static char ln[8192]; int cnt = 0; size_t tl = strlen(tag);
    while (fgets(ln, sizeof ln, fp)) { size_t l = strlen(ln); while (l && (ln[l - 1] == '\n' || ln[l - 1] == '\r')) ln[--l] = 0;
        if (strncmp(ln, tag, tl) || ln[tl] != '\t') continue;
        char *f[16]; int n = 0; for (char *q = ln + tl + 1; n < 16; ) { f[n++] = q; q = strchr(q, '\t'); if (!q) break; *q++ = 0; }
        cb(f, n, ctx); cnt++; }
    fclose(fp); return cnt;
}
