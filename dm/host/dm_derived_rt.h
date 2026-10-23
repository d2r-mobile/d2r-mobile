// 壳（不含任何游戏内容的 App）里，从游戏文件生成的几张表不编进代码，运行时从安装包里的 dm/derived.tsv 读。
// 这个文件只被壳构建时生成的替身文件包含（ios/tools/shell_stubs.py 写出的 dm_hud_exp.h、dm_vault_tab.h、dm_touch_native_art.c），
// 内测 / 自己用 Xcode 构建的包不走这里：它们的三份生成文件里是真实数据。
// 安卓的发布包同理（dm/android/tools/shell_apk.sh）：表在导入的 files/dm/derived.tsv，路径由 DmActivity 在加载原生库之前放进环境变量 DM_DERIVED。
// 鸿蒙也读 DM_DERIVED，但原生库随 ArkTS 静态导入一加载就跑构造函数，那时路径还没设、导入包可能还没导入：替身在构造函数里只登记读表函数，
// 由平台层设好路径后调 dm_derived_reload() 显式读；读不全返回 -1、不记成已读，可以重试（导入完再调一次）。苹果 / 安卓仍在登记时当场读，行为不变。
// derived.tsv 由使用者自己的游戏文件生成（dm/tools/d2rm_pack.py）：每行「标签<TAB>字段<TAB>…」，UTF-8，字段里没有制表符与换行。
// In the shell (an App with no game content), the few tables generated from the game files are not compiled in; they are read at run time from dm/derived.tsv in the install package.
// Only the stand-in files generated when building the shell include this file (dm_hud_exp.h, dm_vault_tab.h, dm_touch_native_art.c written by ios/tools/shell_stubs.py);
// test builds and packages you build yourself with Xcode do not come here: their three generated files hold the real data.
// The Android release APK works the same way (dm/android/tools/shell_apk.sh): the tables are in the imported files/dm/derived.tsv, whose path DmActivity puts in the environment variable DM_DERIVED before loading the native library.
// HarmonyOS reads DM_DERIVED too, but the native library is loaded by the static ArkTS import and runs its constructors right away, before the path is set and possibly before the import pack is imported: there the stand-ins' constructors only register their loaders,
// and the platform layer calls dm_derived_reload() once the path is set; an incomplete read returns -1 and is not remembered as done, so it can be retried (call it again after importing). Apple / Android still read when registering, unchanged.
// derived.tsv is generated from the user's own game files (dm/tools/d2rm_pack.py): each line is "tag<TAB>field<TAB>...", UTF-8, with no tabs or newlines inside fields.
#pragma once
#if !defined(__ANDROID__) && !defined(__OHOS__)
#include <mach-o/dyld.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// 对每一行标签为 tag 的记录调一次 cb（f[0..n) 是各字段，指向本函数持有的行缓冲；要留着的字符串自己 strdup）。返回这种记录的条数，读不到文件 = -1
// Calls cb once for every record line whose tag is tag (f[0..n) are the fields, pointing into the line buffer owned by this function; strdup any strings you want to keep). Returns the number of records of this kind; file cannot be read = -1
static inline int dm_derived_each(const char *tag, void (*cb)(char **f, int n, void *ctx), void *ctx) {
#if defined(__ANDROID__) || defined(__OHOS__)
    const char *p = getenv("DM_DERIVED"); if (!p) { fprintf(stderr, "[dm] 【红】没有设 DM_DERIVED\n"); return -1; }
    FILE *fp = fopen(p, "r"); if (!fp) { fprintf(stderr, "[dm] 【红】没有 %s：还没导入用游戏文件做的导入包\n", p); return -1; }
#else
    char p[4096]; uint32_t sz = sizeof p; if (_NSGetExecutablePath(p, &sz)) return -1;
    char *sl = strrchr(p, '/'); if (!sl || (size_t)(sl - p) + 24 > sizeof p) return -1; strcpy(sl, "/dm/derived.tsv");
    FILE *fp = fopen(p, "r"); if (!fp) { fprintf(stderr, "[dm] 【红】安装包里没有 dm/derived.tsv（%s）：这个包没有用游戏文件做全\n", p); return -1; }
#endif
    static char ln[8192]; int cnt = 0; size_t tl = strlen(tag);
    while (fgets(ln, sizeof ln, fp)) { size_t l = strlen(ln); while (l && (ln[l - 1] == '\n' || ln[l - 1] == '\r')) ln[--l] = 0;
        if (strncmp(ln, tag, tl) || ln[tl] != '\t') continue;
        char *f[16]; int n = 0; for (char *q = ln + tl + 1; n < 16; ) { f[n++] = q; q = strchr(q, '\t'); if (!q) break; *q++ = 0; }
        cb(f, n, ctx); cnt++; }
    fclose(fp); return cnt;
}
// 读表函数登记表：三份替身各登记一个（读全返回 0，否则 -1）。弱定义，三个编译单元里的同名定义链接时合成一份；没有替身的构建里没人登记
// Loader registry: each of the three stand-ins registers one (0 when complete, otherwise -1). Weak definitions, so the same-named ones in three translation units merge into one at link time; builds without stand-ins register nothing
typedef int (*dm_derived_loader)(void);
__attribute__((weak)) dm_derived_loader dm_derived_loaders[8];
__attribute__((weak)) int dm_derived_nloaders;
// 重读全部表：返回 0 = 都读全，-1 = 有没读到的（已读到的照样填进去）；鸿蒙平台层在设好 DM_DERIVED 之后调，可重复调
// Re-read every table: 0 = all complete, -1 = something was not read (what was read is still filled in); the HarmonyOS platform layer calls it once DM_DERIVED is set, and it can be called again
__attribute__((weak)) int dm_derived_reload(void) { int bad = 0; for (int i = 0; i < dm_derived_nloaders; i++) bad |= dm_derived_loaders[i]() != 0; return bad ? -1 : 0; }
static inline void dm_derived_register(dm_derived_loader f) {
    if (dm_derived_nloaders < (int)(sizeof dm_derived_loaders / sizeof dm_derived_loaders[0])) dm_derived_loaders[dm_derived_nloaders++] = f;
#ifndef __OHOS__
    f();
#endif
}
