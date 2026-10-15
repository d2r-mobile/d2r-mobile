// 宿主每帧模块的指令数读数（测试开关 DM_PUMP_PROF=1）：每个模块前后各读一次本线程累计执行的指令数（thread_selfcounts，libsystem_kernel 导出、
// Apple 性能工具在用；指令数不受线程被调到哪种核影响，同样的活就是同样的数），按模块累计，每 600 次 dm_ipf_frame 打一行排行。
// 不开开关时每处只多一次已缓存的判断。只在游戏主线程（取手柄处）和服务端拍里用，两处各自一张表、不加锁。
// Per-frame instruction counts of host modules (test switch DM_PUMP_PROF=1): reads this thread's accumulated executed-instruction count before and after each module (thread_selfcounts, exported by libsystem_kernel,
// used by Apple's performance tools; the instruction count does not depend on which kind of core the thread was scheduled on, so the same work gives the same number), accumulates per module, and prints one ranking line every 600 dm_ipf_frame calls.
// With the switch off, each site costs only one cached check. Used only on the game main thread (where the gamepad is read) and in the server tick, each with its own table and no lock.
#pragma once
#include <stdint.h>
int dm_ipf_on(void);
uint64_t dm_ipf_instr(void);
void dm_ipf_add(int table, const char *name, uint64_t instr);
void dm_ipf_frame(int table);   // table 0 = 主线程每帧（取手柄处），1 = 服务端每拍 | table 0 = main thread per frame (where the gamepad is read), 1 = server per tick
#define DM_PROF(table, name, stmt) do { if (dm_ipf_on()) { uint64_t _p0 = dm_ipf_instr(); stmt; dm_ipf_add(table, name, dm_ipf_instr() - _p0); } else { stmt; } } while (0)
