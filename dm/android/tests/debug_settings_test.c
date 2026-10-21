#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "launch_settings.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char directory[] = "/tmp/d2r-debug-settings-XXXXXX", path[256], error[512];
    assert(mkdtemp(directory));
    snprintf(path, sizeof path, "%s/launch-settings.conf", directory);
    dm_android_launch_settings settings;
    assert(dm_android_launch_settings_apply(directory, &settings, error, sizeof error));
    assert(settings.debug_collection == 0);
    const char *cases[] = {"debug_overlay=1\n", "debug_overlay=0\ndebug_collection=1\n",
                          "debug_overlay=1\ndebug_collection=0\n", "debug_collection=yes\n",
                          "debug_collection=0\ndebug_collection=1\n"};
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        FILE *file = fopen(path, "w"); assert(file);
        assert(fputs(cases[i], file) >= 0); assert(!fclose(file));
        settings.debug_collection = 99;
        int ok = dm_android_launch_settings_apply(directory, &settings, error, sizeof error);
        assert(ok == (i < 3));
        if (ok) {
            assert(settings.debug_collection == (i == 1));
            assert(settings.debug_overlay == (i == 1 ? 0 : 1));
            assert(!strcmp(getenv("DM_DEBUG_OVERLAY"), i == 1 ? "0" : "1"));
        } else assert(settings.debug_collection == 99);
    }
    assert(!unlink(path)); assert(!rmdir(directory));
    puts("debug_settings_test PASS");
}
