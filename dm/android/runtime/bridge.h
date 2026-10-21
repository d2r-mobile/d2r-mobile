// 原生／客体栈桥 ABI；偏移同时供 C 与汇编使用，不读取平台 TLS 私有字段。
// Native/guest stack bridge ABI; offsets are shared by C/assembly, with no private platform TLS access.
#pragma once
#define DM_BRIDGE_SLOTS 128
#define DM_BRIDGE_RECORD_SIZE 128
#define DM_BRIDGE_NATIVE 0
#define DM_BRIDGE_GUEST 1
#define DM_BRIDGE_SERVICE 2
#define DM_BRIDGE_EXITING 3
#define DM_BRIDGE_FRAME_SIZE 160
#define DM_BRIDGE_GATE_KIND 0
#ifndef __ASSEMBLER__
#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define DM_BRIDGE_SANITIZED 1
#else
#define DM_BRIDGE_SANITIZED 0
#endif
#include "host.h"
#include <stdatomic.h>
typedef struct {
    void *native_fake,*guest_fake,*native_fiber,*guest_fiber;
} dm_bridge_sanitizers;
typedef struct dm_bridge_record {
    _Atomic uintptr_t key;
    dm_guest_context *context;
    uintptr_t *run_slot;
    uintptr_t native_low,native_high,guest_low,guest_high;
    uintptr_t reserved[3];
    struct dm_bridge_frame *active;
    uint32_t domain,flags;
    dm_bridge_sanitizers *san;
    uint64_t generation,calls;
    uintptr_t last_native_sp;
} dm_bridge_record;
typedef struct dm_bridge_frame {
    uintptr_t source_sp,return_pc;
    dm_bridge_record *owner;
    uint64_t source_domain;
    uint32_t action,kind;
    uint64_t result,fpcr,fpsr,x18,x29,args[9],padding;
} dm_bridge_frame;
int dm_bridge_attach(dm_guest_context *context,dm_bridge_sanitizers *san);
void dm_bridge_detach(dm_guest_context *context);
int dm_bridge_prepare(dm_bridge_record *record,uintptr_t *run_slot,uintptr_t guest_low,size_t size);
void dm_bridge_start_guest(dm_bridge_record *record);
void dm_bridge_finish_guest(dm_bridge_record *record);
void dm_bridge_finish_native(dm_bridge_record *record);
void dm_bridge_exit_direct(dm_bridge_record *record);
int dm_bridge_request_exit(dm_guest_context *context);
int dm_bridge_on_native_stack(void);
size_t dm_bridge_registry_count(void);
uint64_t dm_bridge_import_dispatch(unsigned id,const uint64_t args[9]);
#endif
