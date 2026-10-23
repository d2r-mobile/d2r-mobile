/* 用自造字符串验证Debug性能参数，不读资源、不启动游戏。
 * Verify Debug performance parameters with synthetic strings, without resources or launching the game. */
#define DM_ANDROID_DEBUG_ENV 1
#include "../debug_env.h"
#include <assert.h>
#include <stdio.h>

static void rejected(const char *value) {
    dm_android_debug_values out={.present=7};
    assert(!dm_android_debug_parse(value,strlen(value),&out));assert(out.present==7);
}
int main(void) {
    dm_android_debug_values out;
    const char *valid="# bounded A/B\r\nDM_3D_SCALE=0.75\r\nDM_3D_SCALE_DYN=0\r\nDM_3D_SCALE_FLOOR=0.60\r\n";
    assert(dm_android_debug_parse(valid,strlen(valid),&out));assert(out.present==7);assert(!strcmp(out.value[0],"0.75"));
    rejected("DM_NVN_SYNC=0\n");rejected("PATH=/tmp\n");rejected("DM_3D_SCALE_DYN=2\n");
    rejected("DM_3D_SCALE=nan\n");rejected("DM_3D_SCALE=1e0\n");rejected("DM_3D_SCALE=1.01\n");
    rejected("DM_3D_SCALE=Inf\n");rejected("DM_3D_SCALE=0.75garbage\n");
    rejected("DM_3D_SCALE=0.5\n");rejected("DM_3D_SCALE=0.75\nDM_3D_SCALE=0.75\n");
    rejected("DM_3D_SCALE_DYN=0 =1\n");
    char long_line[140];memset(long_line,'#',sizeof long_line);assert(!dm_android_debug_parse(long_line,sizeof long_line,&out));
    const char nul[]="DM_3D_SCALE_DYN=0\0DM_3D_SCALE_DYN=1";assert(!dm_android_debug_parse(nul,sizeof nul-1,&out));
    char too_big[4097];memset(too_big,'\n',sizeof too_big);assert(!dm_android_debug_parse(too_big,sizeof too_big,&out));
    out.present=7;assert(dm_android_debug_load("/this-path-does-not-exist-dm-debug-env",&out)==0);assert(out.present==7);
    rejected("DM_3D_SCALE=0.75\nDM_NVN_SYNC=0\n");
    puts("PASS debug_env allowlist, atomic rejection, ranges, duplicate, line limits and NUL");return 0;
}
