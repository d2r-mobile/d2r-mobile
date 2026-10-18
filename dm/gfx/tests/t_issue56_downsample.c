// #56 阶段 A：DownsampleTextureView 红桩边界（离线、无 GPU 工作）。
// 从 nvnBootstrapLoader 按游戏同一路径取址，经生产跳板进入未实现桩；用自有的 command buffer / texture / view
// 断言诊断名字精确、重复调用不改变任何字节，并验证 uint8 计数器的 1/257 打印节律。
// #56 phase A: the DownsampleTextureView red-stub boundary (offline, no GPU work). The address is taken from
// nvnBootstrapLoader exactly as the game does and reached through the production thunk into the unimplemented stub;
// owned command buffer / textures / views must stay byte-identical and the diagnostic must name the entry exactly. The
// uint8 counter rhythm (call 1 and call 257) is asserted as documented.
#include "../nvn/hle_nvn.c"
uint64_t g_t0_ns;
#include "../tex/nvn_deswizzle.h"
#include <time.h>
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }

#include <fcntl.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stdout, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

static const char *kName = "nvnCommandBufferDownsampleTextureView";

// 统计捕获文件里包含入口名的诊断行数 | count captured diagnostic lines naming the entry
static int count_named(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    static char buf[65536];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    int lines = 0;
    for (char *p = buf; (p = strstr(p, kName)); p++) lines++;
    return lines;
}

// stderr 重定向：任一步失败都返回 0，调用方必须在发起被测调用之前终止，避免在未捕获状态下误报诊断证据。
// stderr redirection: any failing step returns 0 and the caller must stop before invoking the entry, so a diagnostic
// proof is never reported from an uncaptured run.
static int g_saved = -1;

static int capture_begin(const char *path) {
    int fd = open(path, O_WRONLY | O_TRUNC);
    if (fd < 0) return 0;
    fflush(stderr);
    g_saved = dup(2);
    if (g_saved < 0) { close(fd); return 0; }
    if (dup2(fd, 2) < 0) { close(g_saved); g_saved = -1; close(fd); return 0; }
    close(fd);
    return 1;
}

static int capture_end(void) {
    fflush(stderr);
    int ok = g_saved >= 0 && dup2(g_saved, 2) >= 0;
    if (g_saved >= 0) close(g_saved);
    g_saved = -1;
    return ok;
}

int main(void) {
    // 1. 生产取址路径：bootstrap → GetProcAddress → 本入口跳板。
    // 1. Production lookup path: bootstrap -> GetProcAddress -> this entry's thunk.
    PFNNVNBOOTSTRAPLOADERPROC boot = (PFNNVNBOOTSTRAPLOADERPROC)h_nvnBootstrapLoader;
    PFNNVNDEVICEGETPROCADDRESSPROC gpa = (PFNNVNDEVICEGETPROCADDRESSPROC)boot("nvnDeviceGetProcAddress");
    CHECK(gpa != NULL);
    if (!gpa) return 1;
    unsigned idx = 0;
    while (idx < NVN_NFUNC && strcmp(nvn_names[idx], kName)) idx++;
    CHECK(idx < NVN_NFUNC);
    NVNT_nvnCommandBufferDownsampleTextureView fn =
        (NVNT_nvnCommandBufferDownsampleTextureView)gpa(NULL, kName);
    CHECK(fn != NULL);
    CHECK((void *)fn == nvn_thunk[idx]);
    // 该槽仍指向未实现桩，而不是某个“看似成功”的实现。
    // The slot still points at the unimplemented stub, not at any implementation that could look like success.
    CHECK(nvn_slot[idx].fn == (void *)nvn_unimpl_entry);

    // 2. 自有对象：命令缓冲、两张 texture、两个 view，全部填入哨兵字节。
    // 2. Owned objects: a command buffer, two textures and two views, all filled with sentinel bytes.
    NVNcommandBuffer cmd;
    NVNtexture src, dst;
    NVNtextureView src_view, dst_view;
    memset(&cmd, 0xa5, sizeof cmd);
    memset(&src, 0x11, sizeof src);
    memset(&dst, 0x22, sizeof dst);
    memset(&src_view, 0x33, sizeof src_view);
    memset(&dst_view, 0x44, sizeof dst_view);
    unsigned char cmd_before[sizeof cmd], src_before[sizeof src], dst_before[sizeof dst];
    unsigned char sv_before[sizeof src_view], dv_before[sizeof dst_view];
    memcpy(cmd_before, &cmd, sizeof cmd);
    memcpy(src_before, &src, sizeof src);
    memcpy(dst_before, &dst, sizeof dst);
    memcpy(sv_before, &src_view, sizeof src_view);
    memcpy(dv_before, &dst_view, sizeof dst_view);

    // 3. 捕获 stderr，重复调用：前 3 次只允许 1 条具名诊断。
    // 3. Capture stderr and repeat the call: the first 3 calls may emit exactly one named diagnostic.
    const char *tmp = getenv("TMPDIR");
    char path[512];
    snprintf(path, sizeof path, "%s/issue56_downsample_XXXXXX", tmp && *tmp ? tmp : "/tmp");
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd < 0) return 1;
    close(fd);
    int ok = capture_begin(path);
    CHECK(ok);
    if (!ok) { unlink(path); return 1; }
    for (int i = 0; i < 3; i++) fn(&cmd, &src, &src_view, &dst, &dst_view);
    ok = capture_end();
    CHECK(ok);
    if (!ok) { unlink(path); return 1; }
    CHECK(count_named(path) == 1);

    // 4. 计数节律：uint8 计数器在调用 257 时回绕并再次打印（文档已写明 1/257）。
    // 4. Counter rhythm: the uint8 counter wraps and logs again on call 257 (documented as 1/257).
    ok = capture_begin(path);
    CHECK(ok);
    if (!ok) { unlink(path); return 1; }
    for (int i = 0; i < 254; i++) fn(&cmd, &src, &src_view, &dst, &dst_view);
    ok = capture_end();
    CHECK(ok);
    if (!ok) { unlink(path); return 1; }
    CHECK(count_named(path) == 1);

    // 5. 不产生任何记录/资源变化：五个自有对象（cmd/src/dst/src_view/dst_view）字节完全不变。
    // 5. No recording or resource change: all five owned objects stay byte-identical.
    CHECK(!memcmp(cmd_before, &cmd, sizeof cmd));
    CHECK(!memcmp(src_before, &src, sizeof src));
    CHECK(!memcmp(dst_before, &dst, sizeof dst));
    CHECK(!memcmp(sv_before, &src_view, sizeof src_view));
    CHECK(!memcmp(dv_before, &dst_view, sizeof dst_view));

    unlink(path);
    printf("t_issue56_downsample: %d checks, %d failures (calls=257, diagnostics=2)\n", checks, failures);
    return failures ? 1 : 0;
}
