#include "dm_hud.h"
#include <pthread.h>
#include <stdatomic.h>
// 存储与跨线程交接（读客体在 diag.c 的拍入口里做）：前端 / Mac 触点线程请求档位，拍线程取走并发布 HUD 数字
// Storage and cross-thread handoff (reading the guest is done in the tick entry in diag.c): the front end / Mac touch point thread requests a level, and the tick thread takes it and publishes the HUD numbers
static _Atomic int g_req;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static dm_hud_info g_info;
void dm_hud_request_players(int n) { atomic_store(&g_req, n < 1 ? 1 : n > 8 ? 8 : n); }
int dm_hud_requested_players(void) { return atomic_load(&g_req); }
void dm_hud_get(dm_hud_info *out) { pthread_mutex_lock(&g_mu); *out = g_info; pthread_mutex_unlock(&g_mu); }
void dm_hud_publish(const dm_hud_info *in) { pthread_mutex_lock(&g_mu); g_info = *in; pthread_mutex_unlock(&g_mu); }
