// iOS 启动 I/O 采样（DM_BOOTPROF=1 时生效；分段表 ⓪–⑥ 由宿主 diag.c 出，零点同为进程创建）：
//   每 100 ms 一行 Documents/bootprof.tsv：t（秒，零点 = 进程创建，内核 p_starttime）、
//   ri_pageins（缺页读入次数：内嵌代码段等文件页首次触达）、ri_diskio_bytesread（真读盘字节：RomFS 首读 + 代码页）、phys_footprint
// iOS startup I/O sampling (active when DM_BOOTPROF=1; the phase table ⓪–⑥ is produced by the host's diag.c, with the same zero point = process creation):
//   one line every 100 ms to Documents/bootprof.tsv: t (seconds, zero point = process creation, kernel p_starttime),
//   ri_pageins (page-in count: first touch of file pages such as the embedded code segment), ri_diskio_bytesread (bytes actually read from disk: first RomFS reads + code pages), phys_footprint
#import <Foundation/Foundation.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <mach/mach.h>
#include <pthread.h>
#include <unistd.h>

int proc_pid_rusage(int pid, int flavor, rusage_info_t *buffer);   // libproc（iOS SDK 无头文件，符号在 libsystem） | libproc (no header in the iOS SDK; the symbol is in libsystem)

uint64_t now_ns(void);                                               // 宿主时钟（hle_os.c） | Host clock (hle_os.c)
static uint64_t g_t0;                                                // 本构造函数时刻 | Time of this constructor
static double g_pre_s;                                               // 进程创建 → 本构造函数（秒） | Process creation → this constructor (seconds)

static void *sampler(void *p) {
    FILE *f = fopen((const char *)p, "w"); if (!f) return NULL;
    fprintf(f, "t_since_spawn\tpageins\tdisk_read_MiB\tfootprint_MiB\n");
    for (int i = 0; i < 300; i++) {                                  // 30 秒 | 30 seconds
        struct rusage_info_v4 ri; task_vm_info_data_t vi; mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
        proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri);
        task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vi, &n);
        fprintf(f, "%.2f\t%llu\t%.1f\t%llu\n", g_pre_s + (now_ns() - g_t0) / 1e9, ri.ri_pageins, ri.ri_diskio_bytesread / 1048576.0, vi.phys_footprint >> 20);
        fflush(f);
        usleep(100000);
    }
    fclose(f); return NULL;
}

__attribute__((constructor)) static void bootprof_ios(void) {
    g_t0 = now_ns();
    if (!getenv("DM_BOOTPROF")) return;
    struct kinfo_proc kp; size_t len = sizeof kp; int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    struct timeval now; gettimeofday(&now, NULL);
    if (!sysctl(mib, 4, &kp, &len, NULL, 0))
        g_pre_s = (now.tv_sec - kp.kp_proc.p_starttime.tv_sec) + (now.tv_usec - kp.kp_proc.p_starttime.tv_usec) / 1e6;
    NSString *doc = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES)[0];
    char *path = strdup([doc stringByAppendingPathComponent:@"bootprof.tsv"].fileSystemRepresentation);
    pthread_t th; pthread_create(&th, NULL, sampler, path); pthread_detach(th);
}
