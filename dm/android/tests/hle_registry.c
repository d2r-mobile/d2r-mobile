// #222 owned ELF HLE registry fixture: exercises the real HLE/HLE_FORCE macros and host_sections.c.
// No game data or guest code; synthetic mangled names only.
#include "../host/hle.h"
#include "../android/native/host_sections.h"
#include <stdio.h>
#include <string.h>

static const char HLE_NAME[] = "_ZN4test7hle_fooEi";
static const char FORCE_NAME[] = "_ZN4test9force_barEi";

HLE(HLE_NAME, int, test_hle_foo, int value) { return value + 7; }
HLE_FORCE(FORCE_NAME, "owned registry fixture", int, test_force_bar, int value) { return value + 11; }

int main(void) {
    unsigned long hle_size = 0, force_size = 0;
    const hle_ent *hle = dm_android_host_section(0, &hle_size);
    const hle_force_ent *force = dm_android_host_section(1, &force_size);
    if (!hle || hle_size != sizeof(hle_ent)) { fprintf(stderr, "FAIL hle table size=%lu\n", hle_size); return 1; }
    if (!force || force_size != sizeof(hle_force_ent)) { fprintf(stderr, "FAIL force table size=%lu\n", force_size); return 1; }
    if (strcmp(hle[0].name, HLE_NAME) || hle[0].fn != (void *)test_hle_foo) { fprintf(stderr, "FAIL hle entry\n"); return 1; }
    if (strcmp(force[0].name, FORCE_NAME) || force[0].fn != (void *)test_force_bar ||
        strcmp(force[0].why, "owned registry fixture")) { fprintf(stderr, "FAIL force entry\n"); return 1; }
    if (test_hle_foo(5) != 12 || test_force_bar(5) != 16) { fprintf(stderr, "FAIL call\n"); return 1; }
    puts("PASS: Android ELF HLE and FORCE registries are non-empty and correct");
    return 0;
}
