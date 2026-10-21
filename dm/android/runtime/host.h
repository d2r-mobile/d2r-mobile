// Android 原生宿主基础接口；依据 libnx SVC ABI、上游线程槽与 POSIX pthread，不改写 Bionic 的线程指针。
// Android native host foundations; based on libnx SVC ABI, upstream thread slots and POSIX pthreads, never replace Bionic's thread pointer.
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct dm_host dm_host;
typedef struct dm_event dm_event;
enum { DM_SLOT_X18, DM_SLOT_TLS, DM_SLOT_TLR, DM_SLOT_GATE, DM_SLOT_RES0, DM_SLOT_RES1, DM_SLOT_ASPECT, DM_SLOT_ASPECT_UI, DM_SLOT_COUNT };
typedef enum { DM_GATE_SVC, DM_GATE_TICKS, DM_GATE_TLS_READ, DM_GATE_TLS_WRITE, DM_GATE_TLR_READ, DM_GATE_X18_ADDRESS } dm_gate_kind;
typedef struct { uintptr_t return_address; dm_gate_kind kind; unsigned argument; } dm_gate_site;
typedef struct { const dm_gate_site *sites; size_t count; } dm_gate_table;
typedef struct { uint64_t slots[DM_SLOT_COUNT]; dm_host *host; const dm_gate_table *gates;
    uint64_t thread_id;uint32_t thread_handle;void *thread_state;struct dm_bridge_record *bridge;
} dm_guest_context;

// 门表只登记 patch.h / x18_patch.h 生成的跳板，表及条目在全部客体线程退出前必须有效且不可修改。
// Register only patch.h / x18_patch.h stubs; tables / entries must remain valid and immutable until all guest threads exit.

enum {
    DM_SVC_OK = 0, DM_SVC_NOT_IMPLEMENTED = 0x4201,
    DM_SVC_INVALID_SIZE = 0xca01, DM_SVC_INVALID_ADDRESS = 0xcc01,
    DM_SVC_OUT_OF_MEMORY = 0xd001, DM_SVC_BAD_STATE = 0xfa01,
    DM_SVC_INVALID_MEMORY_STATE = 0xd401, DM_SVC_INVALID_MEMORY_REGION = 0xdc01,
    DM_SVC_INVALID_HANDLE = 0xe401, DM_SVC_INVALID_COMBINATION = 0xe801,
    // ams::svc ResultInvalidCoreId: module 1, description 113 (113 << 9 | 1).
    DM_SVC_INVALID_CORE = 0xe201,
    DM_SVC_CANCELLED = 0xec01
};

// 宿主销毁前必须结束使用它的所有线程；仍有已附着线程时返回 EBUSY。
// Join all threads using a host before destruction; returns EBUSY while threads remain attached.
dm_host *dm_host_create(size_t heap_capacity);
// 可选连续堆／栈预留；仅自有堆页可做别名，映射参数须同时按 Horizon 4 KiB 和宿主页对齐。
// Optional contiguous heap/stack reservation; only owned heap pages can alias, aligned to both Horizon 4 KiB and host pages.
// 堆容量按 2 MiB，栈容量按宿主页；不提供完整 SDK 初始化／物理内存服务。
// Heap capacity uses 2 MiB units, stack capacity host pages; this does not provide full SDK initialization/physical memory services.
dm_host *dm_host_create_layout(size_t heap_capacity,size_t stack_capacity);
// 可选进程布局连续预留堆／栈／别名区，三者非零；栈和别名容量同时按宿主页与 4 KiB 对齐。
// Optional process layout reserves contiguous heap/stack/alias regions, all nonzero; stack/alias capacities align to host pages and 4 KiB.
// 别名区保持不可访问，仅报告 GetInfo 2/3；MapMemory 仍只能映射到栈区。GetInfo 13 包含全部预留区。
// The alias region remains inaccessible and is reported by GetInfo 2/3; MapMemory still targets only stacks. GetInfo 13 covers the whole reservation.
// 没有客体进程系统资源池，GetInfo 16 返回零；21/22 尚无模块／TLS／借用内存计费模型，明确不支持。
// No guest process system-resource pool exists, so GetInfo 16 returns zero; 21/22 remain unsupported without module/TLS/borrowed-memory accounting.
// 旧构造器不启用这些额外信息项；该 API 不提供共享／物理内存映射或完整 SDK 初始化。
// Legacy constructors do not enable these extra info IDs; this API provides neither shared/physical memory mapping nor full SDK initialization.
dm_host *dm_host_create_process_layout(size_t heap_capacity,size_t stack_capacity,size_t alias_capacity);
// 严格计费模式：预算按宿主页对齐；只允许自有模块、提交堆及专属 TLS/TLR，拒绝外部借用的客体区域。
// Strict accounting mode: host-page-aligned budget covers owned modules, committed heap and dedicated TLS/TLR; external borrowed guest regions are rejected.
// GetInfo 21 为强制预算，22 为当前后备用量；别名不重复计费，宿主元数据／服务栈不计入客体预算。
// GetInfo 21 is the enforced budget and 22 current backing usage; aliases are not charged twice, host metadata/service stacks are outside the guest budget.
// 模块和 TLS 共用预算大小的自有连续 arena，位于别名区后；GetInfo 12/13 包含全部客体映射。
// Modules and TLS share a budget-sized owned contiguous arena after alias VA; GetInfo 12/13 contains every guest mapping.
// TLS 支持 QueryMemory 与显式读写 pin；退出后拒绝新 pin，旧 pin 释放前保留计费映射。TLS 不作栈或互斥／仲裁后备。
// TLS supports QueryMemory and explicit read/write pins; retirement rejects new pins and keeps charged mappings through old pins. TLS cannot back stacks or mutex/arbiter words.
dm_host *dm_host_create_accounted_process(size_t heap_capacity,size_t stack_capacity,size_t alias_capacity,size_t budget);
int dm_host_destroy(dm_host *host);
// Explicit opt-in virtual-core policy: a 64-bit allowed mask plus one preferred core bit.
// Configure before guest threads or attachment; EBUSY while the host is attached or any thread is active.
// Unconfigured hosts keep GetInfo 0 unsupported.
// No physical affinity, core-count inference or legacy mask is implied.
int dm_host_configure_virtual_cores(dm_host *host, uint64_t mask, uint32_t preferred_core);
// 在附着线程／开始等待前登记模块的可写映射（最多 16 段）；调用方须保持内存有效直到销毁宿主，禁止与预留堆重叠。
// Register writable module mappings before attaching / waiting (up to 16 regions); caller keeps memory valid until host destruction, never overlap the reserved heap.
int dm_host_register_writable(dm_host *host, void *base, size_t size);
// 可执行段须在附着／建线程前登记；可信代码由装载器封为 RX，不在此探测系统权限。
// Register executable regions before attachment / thread creation; the loader seals trusted code RX, this API does not discover OS permissions.
int dm_host_register_executable(dm_host *host, void *base, size_t size);
// 只读构建策略：生产至少 4 KiB，ASAN/TSAN 至少 64 KiB；初始 SP 下方须满足入口余量。
// Read-only build policy: production minimum 4 KiB, ASAN/TSAN minimum 64 KiB; usable space below initial SP must meet entry headroom.
size_t dm_host_thread_stack_minimum(void);
size_t dm_host_thread_stack_entry_headroom(void);
// 入口余量为固定改写峰值 1056 字节（SAN 另加 32 KiB）；每处改写还须容纳客体自身存活帧和指令栈效果。
// Entry headroom covers the fixed rewrite peak of 1056 bytes (plus 32 KiB for SAN); each rewrite must also allow guest live frames and instruction stack effects.
// 16 字节对齐，位于已提交堆／已登记可写段；完整分配租约持续到未启动句柄关闭或原生线程完成。
// Stacks are 16-byte aligned in committed heap/registered writable memory; the full lease lasts until unstarted close or native thread completion.
int dm_host_register_thread_stack(dm_host *host, void *base, size_t size);
int dm_host_unregister_thread_stack(dm_host *host, void *top);
int dm_guest_attach(dm_host *host, const dm_gate_table *gates);
// 显式登记当前宿主线程；主线程引导以优先级 44 附着／登记，不调用 SDK 初始化器。输出只在成功时更新。
// Explicitly register the current native thread; main bootstrap attaches/registers at priority 44, never calls SDK initializers. Outputs change only on success.
// 登记线程通过 detach 或 pthread TLS 析构完成；外部线程不能使用 SVC ExitThread 切栈。
// Registered threads complete via detach or pthread TLS destruction; external threads cannot use SVC ExitThread stack switching.
int dm_guest_register_current_thread(int priority, uint32_t *handle);
int dm_guest_bootstrap_main(dm_host *host, const dm_gate_table *gates, uint32_t *handle);
// 由可信装载器按已验证 SDK 配置字段布局；不在运行时猜测版本／偏移，必须在附着前设置。
// A trusted loader configures a verified SDK field layout; never infer versions/offsets at runtime, and configure before attachment.
// 字段为 64 位基址／大小；按构建策略验证完整分配和精确初始 SP 余量，顶部保留区起点存放已登记可执行回调。
// Fields hold a 64-bit base/size; validate the full allocation and exact initial-SP headroom against build policy, with a registered executable callback at the top reserve.
typedef struct {
    uintptr_t entry;
    size_t thread_type_size, stack_base_offset, stack_size_offset, top_reserve;
} dm_sdk_thread_abi;
int dm_host_set_sdk_thread_abi(dm_host *host, const dm_sdk_thread_abi *abi);
int dm_gate_table_validate(const dm_gate_table *gates);
dm_guest_context *dm_guest_current(void);
int dm_guest_detach(void);
uint64_t dm_host_ticks(void);
uint64_t dm_host_svc(dm_host *host, unsigned number, uint64_t registers[31]);
// 同步等待支持事件／线程；0x19 取消已登记线程的当前同步等待或保留一次待取消标志，不终止线程。
// Synchronization waits cover events/threads; 0x19 cancels a registered thread's active synchronization wait or latches one pending cancellation, without terminating it.
// 已就绪对象及零超时优先于待取消标志；其他等待种类不被中断。禁止 pthread_cancel 客体或等待线程。
// Ready objects and zero timeout precede a pending cancellation; other wait kinds are not interrupted. Never pthread_cancel guest or waiting threads.
// 地址 SVC 0x34/0x35 仅支持 32 位类型 0/1/2，使用已提交堆或显式登记的可写段；同址 FIFO，尚无优先级／取消。
// Address SVCs 0x34/0x35 cover 32-bit types 0/1/2 in committed heap or registered writable regions; same-address FIFO, without priorities/cancellation yet.
// 互斥／条件 SVC 0x1a–0x1d 要求已登记线程及可信对齐锁字；超时不隐式重取锁。
// Mutex/condition SVCs 0x1a–0x1d require registered threads and trusted aligned words; timeout does not implicitly reacquire the mutex.

// 事件的生命周期由调用方管理；销毁前先 join 等待线程。timeout_ns=-1 表示无限等待。
// Caller owns event lifetime; join waiting threads before destruction. timeout_ns=-1 means infinite wait.
dm_event *dm_event_create(int auto_clear);
int dm_event_signal(dm_event *event);
int dm_event_clear(dm_event *event);
int dm_event_wait(dm_event *event, int64_t timeout_ns);
int dm_event_destroy(dm_event *event);
