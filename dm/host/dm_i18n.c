// 我们自己加的界面文字取串（英文版：语言跟原版设置走，简中以外一律英文；物品注释 / 旧译名英文下不显示）。
// 唯一来源 dm/i18n/ui_text.tsv（键 / 中文 / English / 用在哪），dm/ios/tools/dm_i18n.py 用它做两件事：
//   · 追加进原版字符串表 ui.json（RomFS 覆盖层）：zhCN 列 = 中文、其余 12 列 = 英文。布局写「@键」、宿主 dm_str 走原版取串 main+0x7f5630，
//     切语言时原版自己清空 + 重装字符串表（SetLanguage 0x317298，r34 起不剪），不用我们跟。
//   · 生成 dm_i18n_tab.h（本文件编进去）：游戏画不到的地方（UIKit 弹框、资源下载页、分享文件名、底栏字形）按 dm_lang_zh() 取。
// 语言：游戏设过（SetLanguage 观测门）就以它为准；之前（资源下载页、进前端前）读存档 Settings/Settings.json 的 "Language"；
//   没有设置文件 = 首次安装，按 iOS 系统语言（dm_sys_lang）。我们的字：简中、繁体用中文，其余英文。
// String lookup for the UI text we added ourselves (English version: the language follows the original game's setting, and everything other than
// Simplified Chinese is English; item notes / legacy translated names are not shown in English).
// Single source: dm/i18n/ui_text.tsv (key / Chinese / English / where used); dm/ios/tools/dm_i18n.py does two things with it:
//   - Appends it to the original string table ui.json (RomFS overlay): zhCN column = Chinese, the other 12 columns = English. Layouts write "@key", and
//   the host's dm_str goes through the original string lookup at main+0x7f5630;
//     on a language switch the original game clears and reloads the string tables itself (SetLanguage 0x317298, no longer patched out as of r34), so we
//     do not have to follow it.
//   - Generates dm_i18n_tab.h (compiled into this file): places the game cannot draw (UIKit alerts, the asset download page, shared file names,
//   bottom-bar glyphs) pick by dm_lang_zh().
// Language: once the game has set it (SetLanguage observation gate), that value wins; before that (asset download page, before entering the front end)
// read "Language" from Settings/Settings.json in the save data;
//   no settings file = first install, use the iOS system language (dm_sys_lang). Our text: Chinese for Simplified and Traditional Chinese, English for
//   everything else.
#include "dm_i18n.h"
#include "dm_i18n_tab.h"
#include <TargetConditionals.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Atomic int g_dm_lang = -1;

#if TARGET_OS_IPHONE
#include <CoreFoundation/CoreFoundation.h>
#endif
// 语言标记（BCP 47，如 en-US、zh-Hans-CN、zh-Hant-TW、yue-Hant-HK）→ 原版语言号。只落两种：语言码是中文（zh）或汉语方言
// （yue 粤、wuu 吴、hak 客、nan 闽南、cmn 官话、gan 赣、hsn 湘、lzh 文言）→ 8 简中，不看字形和地区——我们没有繁体的自有文字，
// 原版繁中 + 我们的简体字一屏两种字；其余一律 0 英文——我们自有的字只有中英两套，德 / 日等原版语言配英文自有字没人验过。空标记 = 简中
// Language tag (BCP 47, e.g. en-US, zh-Hans-CN, zh-Hant-TW, yue-Hant-HK) → the original game's language index. Only two outcomes: a language code that
// is Chinese (zh) or a Chinese variety
// (yue Cantonese, wuu Wu, hak Hakka, nan Min Nan, cmn Mandarin, gan Gan, hsn Xiang, lzh Literary Chinese) → 8 Simplified Chinese, regardless of script
// and region, because we have no Traditional Chinese text of our own
// and the original game's Traditional Chinese plus our Simplified text would put two scripts on one screen; everything else → 0 English, because our own
// text exists only in Chinese and English, and nobody has verified the original game's other languages such as German / Japanese paired with our English
// text. Empty tag = Simplified Chinese
int dm_lang_from_tag(const char *t) {
    if (!t || !*t) return 8;
    char l[4] = { 0 }; int i = 0; while (i < 3 && t[i] && t[i] != '-' && t[i] != '_') { l[i] = (char)(t[i] | 0x20); i++; }
    static const char *const zh[] = { "zh", "yue", "wuu", "hak", "nan", "cmn", "gan", "hsn", "lzh" };
    for (unsigned k = 0; k < sizeof zh / sizeof *zh; k++) if (!strcmp(l, zh[k])) return 8;
    return 0;
}
// 首次安装（还没有设置文件）时的语言：按 iOS 首选语言第一项（CFLocaleCopyPreferredLanguages，过 dm_lang_from_tag：中文及汉语方言 → 简中，其余 → 英文）。
// 曾一律简中（那时触屏版没有能改语言的入口，系统是英文的中文玩家新装进了英文版改不回来）；自定义页标题行有语言开关（dm_custom.c lang_en），放开。
// DM_LANG（宿主 / 测试模拟，同 hle_oe.c）优先；非 iOS 的宿主没有系统语言可问，照旧简中
// Language on first install (no settings file yet): by the first entry of the iOS preferred languages (CFLocaleCopyPreferredLanguages, through dm_lang_from_tag: Chinese and Sinitic varieties → Simplified Chinese, everything else → English).
// It used to be always Simplified Chinese (back then the touch version had no way to change the language, and a Chinese player whose system was in English got the English version on a fresh install and could not change it back); the Custom page title row now has a language switch (dm_custom.c lang_en), so this is opened up.
// DM_LANG (host / test simulation, same as hle_oe.c) takes priority; a non-iOS host has no system language to ask and stays Simplified Chinese as before
int dm_sys_lang(void) {
    const char *e = getenv("DM_LANG"); if (e && *e) return dm_lang_from_tag(e);
#if TARGET_OS_IPHONE
    int r = 8; CFArrayRef a = CFLocaleCopyPreferredLanguages();
    if (a) { if (CFArrayGetCount(a) > 0) { char t[64]; if (CFStringGetCString((CFStringRef)CFArrayGetValueAtIndex(a, 0), t, sizeof t, kCFStringEncodingUTF8)) r = dm_lang_from_tag(t); } CFRelease(a); }
    return r;
#else
    return 8;
#endif
}

static int settings_lang(void) {   // 设置文件里的 "Language"；文件不在 = 首次安装，按系统语言（hle_fs.c 首次建设置文件时也写这个值）；文件在但没有这个键 = -1 | "Language" in the settings file; file missing = first install, use the system language (hle_fs.c writes the same value when it first creates the settings file); file present but key missing = -1
    const char *sv = getenv("DM_SAVE"); if (!sv) return dm_sys_lang();
    char path[1200]; snprintf(path, sizeof path, "%s/Settings/Settings.json", sv);
    FILE *f = fopen(path, "rb"); if (!f) return dm_sys_lang();
    char buf[8192]; size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0;
    const char *k = strstr(buf, "\"Language\""); if (!k) return -1;
    k = strchr(k + 10, ':'); return k ? atoi(k + 1) : -1;
}

int dm_lang_index(void) {
    int l = atomic_load(&g_dm_lang); if (l >= 0) return l;
    static _Atomic int file = -2; int f = atomic_load(&file); if (f == -2) { f = settings_lang(); atomic_store(&file, f); }
    return f < 0 ? 8 : f;   // 有设置文件但没写语言 = 游戏缺省（宿主报 zh-Hans） | settings file exists but has no language entry = the game default (the host reports zh-Hans)
}
int dm_lang_zh(void) { int l = dm_lang_index(); return l == 8 || l == 9; }   // 繁体（zhTW）我们的字也用简中（繁体玩家读简体比读英文顺） | for Traditional Chinese (zhTW) our text also uses Simplified Chinese (Traditional Chinese players read Simplified more easily than English)

const char *dm_ui(const char *key) {
    for (unsigned i = 0; i < sizeof k_dm_i18n / sizeof *k_dm_i18n; i++) if (!strcmp(k_dm_i18n[i].k, key)) return dm_lang_zh() ? k_dm_i18n[i].zh : k_dm_i18n[i].en;
    return key;
}

const char *dm_str(uint8_t *b, const char *key) {
    if (!b) return dm_ui(key);
    struct { const char *k; uint64_t n; } s = { key, strlen(key) };
    const char *r = ((const char *(*)(void *))(b + 0x7f5630))(&s);
    return r && *r && strcmp(r, key) ? r : dm_ui(key);   // 覆盖层里没有这个键（不该发生）就退回编译期表 | if the overlay lacks this key (should not happen), fall back to the compile-time table
}
