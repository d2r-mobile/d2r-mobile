// 仅子进程诊断：不提供可序列化改写产物，不代表 SDK 入口验收。
// Child-only diagnostics: no serializable rewrite output and no SDK-entry acceptance.
#pragma once
#include "nso.h"
#include "runtime/host.h"
#include <stddef.h>
#include <stdint.h>
typedef struct dm_startup_diagnostic dm_startup_diagnostic;
typedef struct {uint32_t begin,end;} dm_diag_extent;
// 原始映像不变；仅完整隔离含阻塞独占加载的函数，其他扫描阻塞仍拒绝。
// Preserve originals; quarantine entire functions containing blocked exclusive loads, reject every other scan blocker.
int dm_diag_prepare(const dm_nso_image *images,size_t count,uintptr_t gate,dm_startup_diagnostic **out);
int dm_diag_map(dm_startup_diagnostic *session,dm_host *host);
int dm_diag_address(const dm_startup_diagnostic *session,size_t module,uint32_t offset,size_t size,uintptr_t *out);
const dm_gate_table *dm_diag_gates(const dm_startup_diagnostic *session);
size_t dm_diag_quarantines(const dm_startup_diagnostic *session,size_t module,const dm_diag_extent **out);
// 先解除所有线程附着，再释放会话，最后销毁宿主；会话固定映射，防止提前销毁。
// Detach all threads, free the session, then destroy host; session pins prevent early host destruction.
int dm_diag_free(dm_startup_diagnostic *session);

#define DM_DIAG_TRACE_LIMIT 64u
typedef enum {DM_DIAG_SERVICE=1,DM_DIAG_RETURNED,DM_DIAG_QUARANTINE,DM_DIAG_SIGNAL,DM_DIAG_UNSUPPORTED,DM_DIAG_OVERFLOW} dm_diag_event;
typedef struct {uint32_t magic,sequence,event,code;uint64_t pc,value;} dm_diag_record;
int dm_diag_trace_start(int fd,const dm_startup_diagnostic *session,uint32_t managed_handle);
_Noreturn void dm_diag_trace_finish(void);
int dm_diag_trace_validate(const void *bytes,size_t size,dm_diag_event *terminal);
