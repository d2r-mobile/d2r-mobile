#pragma once

#include <stdlib.h>
#include <string.h>

// 采样绑定的有界分类，不含资源内容、地址、设备标识。
// Bounded sampled-binding classification without resource contents, addresses or device identifiers.
typedef enum {
    TEXTURE_DIAG_OK = 0,
    TEXTURE_DIAG_UNBOUND,
    TEXTURE_DIAG_MISSING,
    TEXTURE_DIAG_UNSUPPORTED_FORMAT,
    TEXTURE_DIAG_UNSAMPLED_FORMAT,
    TEXTURE_DIAG_VIEW_FORMAT,
    TEXTURE_DIAG_DEPTH,
    TEXTURE_DIAG_LINEAR,
} texture_diag_kind;

#define TEXTURE_DIAG_WINDOW_MAX 48u
#define TEXTURE_DIAG_TEXTURE_MASK 0xfffffu
// Android property values are bounded at 92 bytes; 96 stores a complete supported token.
// Android property values are capped at 92 bytes; 96 stores a complete supported token.
#define TEXTURE_DIAG_TOKEN_MAX 96u

static inline const char *texture_diag_name(unsigned kind) {
    switch (kind) {
        case TEXTURE_DIAG_UNBOUND: return "unbound";
        case TEXTURE_DIAG_MISSING: return "missing";
        case TEXTURE_DIAG_UNSUPPORTED_FORMAT: return "unsupported_format";
        case TEXTURE_DIAG_UNSAMPLED_FORMAT: return "unsampled_format";
        case TEXTURE_DIAG_VIEW_FORMAT: return "view_format";
        case TEXTURE_DIAG_DEPTH: return "depth";
        case TEXTURE_DIAG_LINEAR: return "linear";
        default: return "ok";
    }
}

// 顺序即诊断优先级：未绑定 > 未登记 > 格式未知/不可采样 > 格式视图 > 深度 > 线性。
// Priority: unbound > unregistered > unknown/unsampled format > view format > depth > linear.
static inline unsigned texture_diag_classify(int valid, unsigned handle, unsigned format,
                                             int format_known, int sampled,
                                             int view_set, unsigned view_format, unsigned flags) {
    if (!handle) return TEXTURE_DIAG_UNBOUND;
    if (!valid) return TEXTURE_DIAG_MISSING;
    if (!format_known) return TEXTURE_DIAG_UNSUPPORTED_FORMAT;
    if (!sampled) return TEXTURE_DIAG_UNSAMPLED_FORMAT;
    if (view_set && view_format != format) return TEXTURE_DIAG_VIEW_FORMAT;
    if (format >= 0x31 && format <= 0x36) return TEXTURE_DIAG_DEPTH;
    if (flags & 0x10) return TEXTURE_DIAG_LINEAR;
    return TEXTURE_DIAG_OK;
}

static inline unsigned texture_diag_texture_id(unsigned handle) {
    return handle & TEXTURE_DIAG_TEXTURE_MASK;
}

// 一次请求 token 最多产生 limit 行；off by default，token 变化才重新开窗。
// One request token produces at most limit lines; off by default and only a token change re-arms.
typedef struct {
    unsigned limit, used, seen;
    unsigned ok, unbound, missing, unsupported, unsampled, view_format, depth, linear;
    unsigned active, summarized, initialized;
    char token[TEXTURE_DIAG_TOKEN_MAX];
} texture_diag_state;

static inline void texture_diag_state_init(texture_diag_state *state) {
    if (state->initialized) return;
    memset(state, 0, sizeof *state);
    state->initialized = 1;
    const char *setting = getenv("DM_VK_TEX_DIAG");
    int n = setting ? atoi(setting) : 0;
    if (n < 0) n = 0;
    if (n > (int)TEXTURE_DIAG_WINDOW_MAX) n = (int)TEXTURE_DIAG_WINDOW_MAX;
    if (n > 0) {
        state->limit = (unsigned)n;
        state->active = 1;
        strcpy(state->token, "env");
    }
}

// token 为空表示撤销当前 property 窗口；相同 token 不重开已耗尽的预算。
// An empty token revokes the current property window; the same token never reopens an exhausted budget.
static inline void texture_diag_state_tick(texture_diag_state *state, const char *token) {
    texture_diag_state_init(state);
    if (!token) token = "";
    if (!strcmp(token, state->token)) return;
    if (!*token) {
        if (strcmp(state->token, "env")) {
            state->token[0] = 0;
            state->active = 0;
        }
        return;
    }
    // 过长 token 明确拒绝且不开窗，绝不把完整输入与截断副本比较。
    // Reject overlong tokens without arming; never compare a full input against a truncated copy.
    size_t length = strlen(token);
    if (length >= sizeof state->token) return;
    memcpy(state->token, token, length + 1);
    state->limit = TEXTURE_DIAG_WINDOW_MAX;
    state->used = state->seen = 0;
    state->ok = state->unbound = state->missing = 0;
    state->unsupported = state->unsampled = state->view_format = state->depth = state->linear = 0;
    state->summarized = 0;
    state->active = 1;
}

// creation refusal 和 sampled bind 共用同一预算；返回 1 才允许输出一行。
// Creation refusals and sampled binds share one budget; a line may be emitted only on 1.
static inline int texture_diag_state_reserve(texture_diag_state *state) {
    texture_diag_state_init(state);
    if (!state->active || state->used >= state->limit) return 0;
    state->used++;
    state->seen++;
    return 1;
}
