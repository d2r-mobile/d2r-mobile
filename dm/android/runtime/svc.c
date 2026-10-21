// 基础内存／计时／调试及事件 SVC；寄存器约定依据 libnx kernel/svc.s 与上游 hle_thread.c。
// Basic memory / timing / debug and event SVCs; register contracts follow libnx kernel/svc.s and upstream hle_thread.c.
#include "internal.h"
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <time.h>

uint64_t dm_host_ticks(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return 0;
    return (uint64_t)now.tv_sec * 19200000u + (uint64_t)now.tv_nsec * 12u / 625u;
}

uint64_t dm_host_svc(dm_host *host, unsigned number, uint64_t registers[31]) {
    uint64_t result = DM_SVC_NOT_IMPLEMENTED;
    switch (number) {
    case 0x01: result = dm_host_set_heap(host, registers); break;
    case 0x04:case 0x05:case 0x06:case 0x29:result=dm_host_memory_svc(host,number,registers);break;
    case 0x0b: {
        int64_t ns = (int64_t)registers[0];
        if (ns < -2) { result = DM_SVC_INVALID_SIZE; break; }
        if (ns <= 0) result = sched_yield() ? DM_SVC_BAD_STATE : DM_SVC_OK;
        else {
            struct timespec wait = {.tv_sec = ns / 1000000000, .tv_nsec = ns % 1000000000};
            int rc;
            do { rc = nanosleep(&wait, &wait); } while (rc && errno == EINTR);
            result = rc ? DM_SVC_BAD_STATE : DM_SVC_OK;
        }
        break;
    }
    case 0x1e:
        // GetSystemTick 直接返回 x0，不是 Result+x1。 | GetSystemTick returns directly in x0, not Result+x1.
        registers[0] = dm_host_ticks(); return registers[0];
    case 0x27: {
        uintptr_t address = (uintptr_t)registers[0]; uint64_t length = registers[1];
        pthread_mutex_lock(&host->mutex);
        if (length > 1024u * 1024u || !dm_host_writable(host,address,(size_t)length)) result = DM_SVC_INVALID_ADDRESS;
        else result = fwrite((const void *)address, 1, (size_t)length, stderr) == length ? DM_SVC_OK : DM_SVC_BAD_STATE;
        pthread_mutex_unlock(&host->mutex);
        break;
    }
    case 0x08:case 0x09:case 0x0a:case 0x0c:case 0x25:
    case 0x11:case 0x12:case 0x16:case 0x17:case 0x18:case 0x19:case 0x45:
        result=dm_host_sync_svc(host,number,registers);break;
    case 0x1f:case 0x21:result=dm_host_ipc_svc(host,number,registers);break;
    case 0x34:case 0x35:result=dm_host_arbiter_svc(host,number,registers);break;
    case 0x1a:case 0x1b:case 0x1c:case 0x1d:result=dm_host_mutex_svc(host,number,registers);break;
    default: break;
    }
    registers[0] = result;
    return result;
}
