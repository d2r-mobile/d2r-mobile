// #223 owned counterexample: two queued submits keep their own host drawing
// requests; the one-shot force-full request is consumed exactly once.
#include "../native/nvn_vulkan_host.inc"

#include <stdint.h>
#include <stdio.h>

static int check(uint64_t v, uint32_t frz, uint32_t force) {
    return (uint32_t)v == frz && (uint32_t)(v >> 32) == force;
}

int main(void) {
    dm_nvn_host_state s;
    dm_nvn_host_state_init(&s);

    dm_nvn_host_state_frozen(&s, 1);
    dm_nvn_host_state_force_full(&s, 3);
    uint64_t first = dm_nvn_host_state_latch(&s);
    if (!check(first, 2, 3)) { fprintf(stderr, "first latch mismatch\n"); return 1; }

    // force-full is one-shot; freeze remains the requested state.
    uint64_t second = dm_nvn_host_state_latch(&s);
    if (!check(second, 2, 0)) { fprintf(stderr, "force request was lost or duplicated\n"); return 2; }

    // A later request must not leak into the first job.
    dm_nvn_host_state_frozen(&s, 0);
    dm_nvn_host_state_force_full(&s, 2);
    uint64_t third = dm_nvn_host_state_latch(&s);
    if (!check(third, 1, 2)) { fprintf(stderr, "third latch mismatch\n"); return 3; }

    int frz = -1, force = -1;
    dm_nvn_host_state_apply(&s, first, &frz, &force);
    if (frz != 1 || force != 3) { fprintf(stderr, "first apply crossed state\n"); return 4; }
    dm_nvn_host_state_apply(&s, third, &frz, &force);
    if (frz != 0 || force != 2) { fprintf(stderr, "third apply crossed state\n"); return 5; }

    printf("PASS nvn host latch: first/force-once/later isolation\n");
    return 0;
}
