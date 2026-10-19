// GNU 链接器的节起止符号；条目由上游 HLE/HLE_FORCE 宏登记。
// GNU linker section boundary symbols; entries are registered by upstream HLE/HLE_FORCE macros.
#include "host_sections.h"
#include <stddef.h>
extern const unsigned char __start_dm_hle[] __attribute__((weak));
extern const unsigned char __stop_dm_hle[] __attribute__((weak));
extern const unsigned char __start_dm_force[] __attribute__((weak));
extern const unsigned char __stop_dm_force[] __attribute__((weak));
const void *dm_android_host_section(int forced, unsigned long *size) {
    const unsigned char *start = forced ? __start_dm_force : __start_dm_hle;
    const unsigned char *end = forced ? __stop_dm_force : __stop_dm_hle;
    *size = start && end ? end - start : 0;
    return start;
}
