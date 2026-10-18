// 注入提交/等待错误，验证在确认完成前绝不销毁 GPU 在用资源。
// Inject submit/wait failures and verify GPU resources are never destroyed before completion is established.
#include "../vulkan/vk_host.h"
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #condition); exit(1); } } while (0)
static VkResult end_result, create_result, submit_result, wait_result, idle_result;
static volatile sig_atomic_t safe_abort;
static char trace[32];
static size_t used;
static void event(char code) { safe_abort = 0; CHECK(used + 1 < sizeof trace); trace[used++] = code; trace[used] = 0; }
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer command) {
    (void)command; event('E'); return end_result;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice device, const VkFenceCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkFence *out) {
    (void)device; (void)info; (void)allocator; event('C'); if (create_result == VK_SUCCESS) *out = (VkFence)(uintptr_t)5; return create_result;
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue, uint32_t count, const VkSubmitInfo *submit, VkFence fence) {
    (void)queue; (void)count; (void)submit; (void)fence; event('S'); return submit_result;
}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice device, uint32_t count, const VkFence *fences,
    VkBool32 all, uint64_t timeout) {
    (void)device; (void)count; (void)fences; (void)all; (void)timeout; event('W'); return wait_result;
}
VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device) { (void)device; event('I'); safe_abort = !strcmp(trace, "ECSWI"); return idle_result; }
VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *allocator) {
    (void)device; (void)fence; (void)allocator; event('D');
}
VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice device, VkCommandPool pool, uint32_t count,
    const VkCommandBuffer *commands) { (void)device; (void)pool; (void)count; (void)commands; event('F'); }

static void aborted(int signal_number) {
    _exit(signal_number == SIGABRT && safe_abort ? 73 : 74);
}
static void run(const char *name, VkResult submit, VkResult wait, VkResult idle, VkResult expected, const char *events) {
    submit_result = submit; wait_result = wait; idle_result = idle; used = 0; trace[0] = 0;
    dm_vk vk = {.device = (VkDevice)(uintptr_t)1, .queue = (VkQueue)(uintptr_t)2, .pool = (VkCommandPool)(uintptr_t)3};
    CHECK(dm_vk_submit(&vk, (VkCommandBuffer)(uintptr_t)4) == expected);
    CHECK(!strcmp(trace, events));
    printf("%s: %s PASS\n", name, trace);
}
int main(void) {
    end_result = VK_ERROR_OUT_OF_HOST_MEMORY;
    run("end failure", VK_SUCCESS, VK_SUCCESS, VK_SUCCESS, VK_ERROR_OUT_OF_HOST_MEMORY, "EF");
    end_result = VK_SUCCESS; create_result = VK_ERROR_OUT_OF_HOST_MEMORY;
    run("fence creation failure", VK_SUCCESS, VK_SUCCESS, VK_SUCCESS, VK_ERROR_OUT_OF_HOST_MEMORY, "ECF");
    create_result = VK_SUCCESS;
    run("success", VK_SUCCESS, VK_SUCCESS, VK_SUCCESS, VK_SUCCESS, "ECSWDF");
    run("submit OOM", VK_ERROR_OUT_OF_HOST_MEMORY, VK_SUCCESS, VK_SUCCESS, VK_ERROR_OUT_OF_HOST_MEMORY, "ECSDF");
    run("submit unknown / idle success", VK_ERROR_UNKNOWN, VK_SUCCESS, VK_SUCCESS, VK_ERROR_UNKNOWN, "ECSIDF");
    run("wait OOM / idle success", VK_SUCCESS, VK_ERROR_OUT_OF_HOST_MEMORY, VK_SUCCESS, VK_ERROR_OUT_OF_HOST_MEMORY, "ECSWIDF");
    run("wait OOM / idle lost", VK_SUCCESS, VK_ERROR_OUT_OF_DEVICE_MEMORY, VK_ERROR_DEVICE_LOST, VK_ERROR_OUT_OF_DEVICE_MEMORY, "ECSWIDF");
    run("wait lost", VK_SUCCESS, VK_ERROR_DEVICE_LOST, VK_SUCCESS, VK_ERROR_DEVICE_LOST, "ECSWDF");
    run("submit lost / idle success", VK_ERROR_DEVICE_LOST, VK_SUCCESS, VK_SUCCESS, VK_ERROR_DEVICE_LOST, "ECSIDF");
    run("submit lost / idle lost", VK_ERROR_DEVICE_LOST, VK_SUCCESS, VK_ERROR_DEVICE_LOST, VK_ERROR_DEVICE_LOST, "ECSIDF");
    fflush(stdout);
    pid_t child = fork(); CHECK(child >= 0);
    if (!child) {
        signal(SIGABRT, aborted);
        run("uncertain completion", VK_SUCCESS, VK_ERROR_OUT_OF_HOST_MEMORY,
            VK_ERROR_OUT_OF_HOST_MEMORY, VK_SUCCESS, "unreachable");
        _exit(75);
    }
    int status; CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 73);
    puts("uncertain completion: ECSWI / fail-stop before cleanup PASS");
    return 0;
}
