#include "texture_diag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL at line %d\\n", __LINE__); return 1; } } while (0)

int main(void) {
    CHECK(texture_diag_classify(1, 0, 0x25, 1, 1, 0, 0x25, 0) == TEXTURE_DIAG_UNBOUND);
    CHECK(texture_diag_classify(0, 7, 0x25, 1, 1, 0, 0x25, 0) == TEXTURE_DIAG_MISSING);
    CHECK(texture_diag_classify(1, 7, 0x9a, 0, 0, 0, 0x9a, 0) == TEXTURE_DIAG_UNSUPPORTED_FORMAT);
    CHECK(texture_diag_classify(1, 7, 0x25, 1, 0, 0, 0x25, 0) == TEXTURE_DIAG_UNSAMPLED_FORMAT);
    CHECK(texture_diag_classify(1, 7, 0x25, 1, 1, 1, 0x38, 0) == TEXTURE_DIAG_VIEW_FORMAT);
    CHECK(texture_diag_classify(1, 7, 0x32, 1, 1, 0, 0x32, 0) == TEXTURE_DIAG_DEPTH);
    CHECK(texture_diag_classify(1, 7, 0x25, 1, 1, 0, 0x25, 0x10) == TEXTURE_DIAG_LINEAR);
    CHECK(texture_diag_classify(1, 7, 0x25, 1, 1, 0, 0x25, 0) == TEXTURE_DIAG_OK);
    CHECK(!strcmp(texture_diag_name(TEXTURE_DIAG_MISSING), "missing"));

    // 组合 handle 的 sampler bits 不能冒充 texture id；只有 sampler bits 仍是 unbound。
    CHECK(texture_diag_texture_id(0xabc00007u) == 7);
    CHECK(texture_diag_classify(1, texture_diag_texture_id(0xabc00000u), 0x25, 1, 1, 0, 0x25, 0) == TEXTURE_DIAG_UNBOUND);

    // 默认关闭，即使进程启动阶段也没有预算。
    unsetenv("DM_VK_TEX_DIAG");
    texture_diag_state state = {0};
    texture_diag_state_init(&state);
    CHECK(!state.active && state.used == 0);
    CHECK(!texture_diag_state_reserve(&state));

    // 到达角色场景后由新 token 开窗；之前没有预算也无所谓。
    texture_diag_state_tick(&state, "scene-character-1");
    CHECK(state.active && state.limit == TEXTURE_DIAG_WINDOW_MAX && !strcmp(state.token, "scene-character-1"));
    for (unsigned i = 0; i < TEXTURE_DIAG_WINDOW_MAX; i++) CHECK(texture_diag_state_reserve(&state));

    // 预算耗尽后同一 token 不再重开；创建拒绝和 bind 共用同一个 used。
    CHECK(state.used == TEXTURE_DIAG_WINDOW_MAX);
    CHECK(!texture_diag_state_reserve(&state));
    texture_diag_state_tick(&state, "scene-character-1");
    CHECK(state.used == TEXTURE_DIAG_WINDOW_MAX && !texture_diag_state_reserve(&state));

    // 新 token 重置预算，允许下一次有界窗口。
    texture_diag_state_tick(&state, "scene-character-2");
    CHECK(state.active && state.used == 0 && !strcmp(state.token, "scene-character-2"));
    CHECK(texture_diag_state_reserve(&state) && state.used == 1);

    // 长 token 完整身份：同一长 token 不重开；共享前 31 字节但后缀不同是不同窗口。
    texture_diag_state long_state = {0};
    const char *long_token = "scene-character-0123456789-ABCDX";
    const char *long_token_other = "scene-character-0123456789-ABCDY";
    CHECK(strlen(long_token) > 31 && !strncmp(long_token, long_token_other, 31));
    texture_diag_state_tick(&long_state, long_token);
    CHECK(long_state.active && !strcmp(long_state.token, long_token));
    for (unsigned i = 0; i < TEXTURE_DIAG_WINDOW_MAX; i++) CHECK(texture_diag_state_reserve(&long_state));
    texture_diag_state_tick(&long_state, long_token);
    CHECK(long_state.used == TEXTURE_DIAG_WINDOW_MAX && !texture_diag_state_reserve(&long_state));
    texture_diag_state_tick(&long_state, long_token_other);
    CHECK(long_state.active && long_state.used == 0 && !strcmp(long_state.token, long_token_other));

    // 超过完整存储容量的 token 被拒绝，不截断，也不开窗。
    char overlong[TEXTURE_DIAG_TOKEN_MAX + 8];
    memset(overlong, 'x', sizeof overlong - 1);
    overlong[sizeof overlong - 1] = 0;
    texture_diag_state rejected = {0};
    texture_diag_state_tick(&rejected, overlong);
    CHECK(!rejected.active && !rejected.used);
    overlong[sizeof overlong - 2] = 'y';
    texture_diag_state_tick(&rejected, overlong);
    CHECK(!rejected.active && !rejected.used);

    // 清空 property token 撤销当前窗口；env 仍可单独使用。
    texture_diag_state_tick(&state, "");
    CHECK(!state.active && !texture_diag_state_reserve(&state));
    setenv("DM_VK_TEX_DIAG", "2", 1);
    texture_diag_state env_state = {0};
    texture_diag_state_init(&env_state);
    CHECK(env_state.active && env_state.limit == 2);
    CHECK(texture_diag_state_reserve(&env_state) && texture_diag_state_reserve(&env_state));
    CHECK(!texture_diag_state_reserve(&env_state));
    unsetenv("DM_VK_TEX_DIAG");

    puts("PASS: texture diagnostic classification and trigger window");
    return 0;
}
