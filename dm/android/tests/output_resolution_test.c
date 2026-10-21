#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "launch_settings.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char directory[]="/tmp/d2r-output-XXXXXX", path[256], error[512];
    assert(mkdtemp(directory));
    snprintf(path,sizeof path,"%s/launch-settings.conf",directory);
    const char *cases[]={"output_resolution=1600x720\nrender_scale=89\n", "resolution=1600x900\nrender_scale=100\n",
                         "output_resolution=1600x720\nrender_scale=67\n", "output_resolution=0x720\n", "resolution=1600x900\noutput_resolution=1280x720\n", "render_scale=1\n"};
    for (unsigned i=0;i<6;i++) {
        FILE *file=fopen(path,"w"); assert(file); assert(fputs(cases[i],file)>=0); assert(!fclose(file));
        dm_android_launch_settings settings;
        int ok=dm_android_launch_settings_apply(directory,&settings,error,sizeof error);
        assert(ok==(i<3));
        if(i==0) { assert(settings.resolution_exact==1); assert(!strcmp(getenv("DM_GAME_RES"),"1600x720")); assert(!strcmp(getenv("DM_GAME_RES_EXACT"),"1")); assert(!strcmp(getenv("DM_3D_SCALE"),"0.888888889")); }
        if(i==1) { assert(settings.resolution_exact==0); assert(!getenv("DM_GAME_RES_EXACT")); assert(!strcmp(getenv("DM_3D_SCALE"),"1.0")); }
        if(i==2) { assert(!strcmp(getenv("DM_GAME_RES"),"1600x720")); assert(!strcmp(getenv("DM_3D_SCALE"),"0.666666667")); }
    }
    assert(!unlink(path)); assert(!rmdir(directory));
    puts("output_resolution_test PASS");
}
