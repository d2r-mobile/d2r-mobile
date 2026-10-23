// 登记表段（HLE / 强制接管 / NVN 实现）：各源文件用 DM_SECT 把表项放进同名段，宿主用 DM_SECT_GET 取整段。
// Mach-O（苹果）：段名 "__DATA,名"，运行时 getsectiondata 取；ELF（安卓）：段名就是「名」（合法 C 标识符），链接器自动给出 __start_名 / __stop_名。
// 两边表项逐字相同；ELF 上 retain 防止 --gc-sections 把只被段边界符号引用的表项丢掉。
// Registration table sections (HLE / forced takeover / NVN implementations): each source file puts its entries into the same-named section with DM_SECT, and the host takes the whole section with DM_SECT_GET.
// Mach-O (Apple): section name "__DATA,name", fetched at run time with getsectiondata; ELF (Android): the section name is just "name" (a valid C identifier), and the linker provides __start_name / __stop_name automatically.
// The entries are word-for-word the same on both; on ELF, retain keeps --gc-sections from dropping entries that are referenced only through the section boundary symbols.
#pragma once
#ifdef __APPLE__
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#define DM_SECT(name) __attribute__((used, section("__DATA," #name)))
// 取段 name 的首址与字节数（找不到段时 p = NULL、bytes = 0）
// Fetch the start address and byte count of section name (p = NULL, bytes = 0 when the section is missing)
#define DM_SECT_GET(name, type, p, bytes) do { unsigned long dm_sz_ = 0; \
    (p) = (type *)getsectiondata(&_mh_execute_header, "__DATA", #name, &dm_sz_); (bytes) = (p) ? dm_sz_ : 0; } while (0)
#elif defined(__ANDROID__) && !defined(DM_ANDROID_NATIVE_APP)
#define DM_SECT_NAME_swhle dm_hle
#define DM_SECT_NAME_swforce dm_force
#define DM_SECT_NAME_swnvn dm_nvn
#define DM_SECT_STRING_INNER(name) #name
#define DM_SECT_STRING(name) DM_SECT_STRING_INNER(name)
#define DM_SECT_BOUND_INNER(prefix, name) prefix##name
#define DM_SECT_BOUND(prefix, name) DM_SECT_BOUND_INNER(prefix, name)
#define DM_SECT(name) __attribute__((used, retain, section(DM_SECT_STRING(DM_SECT_NAME_##name)), aligned(sizeof(void *))))
#define DM_SECT_GET(name, type, p, bytes) do { \
    extern const char DM_SECT_BOUND(__start_, DM_SECT_NAME_##name)[] __attribute__((weak, visibility("hidden"))); \
    extern const char DM_SECT_BOUND(__stop_, DM_SECT_NAME_##name)[] __attribute__((weak, visibility("hidden"))); \
    (p) = (type *)DM_SECT_BOUND(__start_, DM_SECT_NAME_##name); \
    (bytes) = (p) ? (unsigned long)(DM_SECT_BOUND(__stop_, DM_SECT_NAME_##name) - DM_SECT_BOUND(__start_, DM_SECT_NAME_##name)) : 0; \
} while (0)
#else
#define DM_SECT(name) __attribute__((used, retain, section(#name)))
#define DM_SECT_GET(name, type, p, bytes) do { \
    extern const char __start_##name[] __attribute__((weak, visibility("hidden"))); \
    extern const char __stop_##name[] __attribute__((weak, visibility("hidden"))); \
    (p) = (type *)__start_##name; (bytes) = __start_##name ? (unsigned long)(__stop_##name - __start_##name) : 0; } while (0)
#endif
