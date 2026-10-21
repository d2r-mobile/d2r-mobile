// #253 owned dirty snapshot transaction regression: actual production
// transaction over a private mmap range, with real protection transitions and
// real generation/incarnation state. The only test seam injects one mprotect
// failure; no fake snapshot implementation.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../../gfx/nvn/nvn_dirty.c"

void dm_fault_hook_register(int (*fn)(void *, int, void *)) { (void)fn; }
void dm_guest_write_prep_register(void (*fn)(void *, size_t)) { (void)fn; }
void nvn_dirty_test_fail_next_protect(void);

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

static void reset_range(uint8_t *base, size_t bytes) {
    nvn_dirty_remove_range(base);
    nvn_dirty_add_range(base, bytes);
    nvn_dirty_protect(base, bytes);
}

int main(void) {
    size_t page = (size_t)getpagesize(), bytes = page * 8;
    uint8_t *base = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    CHECK(base != MAP_FAILED);
    if (base == MAP_FAILED) return 1;
    nvn_dirty_add_range(base, bytes);
    nvn_dirty_protect(base, bytes);

    nvn_dirty_snapshot_plan plan;
    errno = E2BIG;
    CHECK(nvn_dirty_snapshot_begin(base, bytes, 0, 0, &plan) == 1);
    CHECK(plan.full == 1 && plan.run_count == 1 && plan.runs[0].offset == 0 && plan.runs[0].size == bytes);
    CHECK(errno == E2BIG);
    CHECK(nvn_dirty_snapshot_end(&plan) == 1);
    uint64_t stamp = plan.stamp, incarnation = plan.incarnation;

    // A real write mark on page 0 becomes the only delta run.
    prep_write(base, page);
    CHECK(nvn_dirty_snapshot_begin(base, bytes, stamp, incarnation, &plan) == 1);
    CHECK(plan.full == 0 && plan.run_count == 1 && plan.runs[0].offset == 0 && plan.runs[0].size == page);
    CHECK(nvn_dirty_snapshot_end(&plan) == 1);
    stamp = plan.stamp;

    // A write after the snapshot stamp invalidates end.
    nvn_dirty_protect(base, bytes);
    CHECK(nvn_dirty_snapshot_begin(base, bytes, stamp, incarnation, &plan) == 1);
    prep_write(base, page);
    CHECK(nvn_dirty_snapshot_end(&plan) == 0);
    CHECK(plan.reason == NVN_DIRTY_SNAPSHOT_EPOCH);

    // Re-registering the same address changes incarnation and requires full.
    reset_range(base, bytes);
    CHECK(nvn_dirty_snapshot_begin(base, bytes, stamp, incarnation, &plan) == 1);
    CHECK(plan.full == 1 && plan.incarnation != incarnation);
    CHECK(nvn_dirty_snapshot_end(&plan) == 1);
    incarnation = plan.incarnation; stamp = plan.stamp;

    // Unaligned start is clipped to the requested bytes, not inflated.
    nvn_dirty_protect(base, bytes);
    CHECK(nvn_dirty_snapshot_begin(base + 1, page, 0, 0, &plan) == 1);
    CHECK(plan.full == 1 && plan.run_count == 1 && plan.runs[0].offset == 0 && plan.runs[0].size == page);
    CHECK(nvn_dirty_snapshot_end(&plan) == 1);

    // More than 32 non-contiguous dirty pages must refuse rather than drop runs.
    {
        size_t wide_bytes = page * 70;
        uint8_t *wide = mmap(NULL, wide_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        CHECK(wide != MAP_FAILED);
        if (wide != MAP_FAILED) {
            nvn_dirty_add_range(wide, wide_bytes);
            nvn_dirty_protect(wide, wide_bytes);
            CHECK(nvn_dirty_snapshot_begin(wide, wide_bytes, 0, 0, &plan) == 1);
            CHECK(nvn_dirty_snapshot_end(&plan) == 1);
            uint64_t wide_stamp = plan.stamp, wide_inc = plan.incarnation;
            for (size_t i = 0; i < 70; i += 2) prep_write(wide + i * page, page);
            CHECK(nvn_dirty_snapshot_begin(wide, wide_bytes, wide_stamp, wide_inc, &plan) == 0);
            CHECK(plan.reason == NVN_DIRTY_SNAPSHOT_RUN_LIMIT);
            nvn_dirty_remove_range(wide);
            CHECK(munmap(wide, wide_bytes) == 0);
        }
    }

    // Abort releases the metadata lock and leaves the range reusable.
    nvn_dirty_remove_range(base);
    nvn_dirty_add_range(base, bytes);
    nvn_dirty_protect(base, bytes);
    CHECK(nvn_dirty_snapshot_begin(base, bytes, 0, 0, &plan) == 1);
    nvn_dirty_snapshot_abort(&plan);
    nvn_dirty_remove_range(base);   // would deadlock if abort did not unlock
    nvn_dirty_add_range(base, bytes);
    nvn_dirty_protect(base, bytes);

    // One injected PROT_READ failure rolls back RW and permanently disables
    // this range's snapshot caching; a later begin is refused as DISABLED.
    nvn_dirty_test_fail_next_protect();
    CHECK(nvn_dirty_snapshot_begin(base, bytes, 0, 0, &plan) == 0);
    CHECK(plan.reason == NVN_DIRTY_SNAPSHOT_PROTECTION);
    CHECK(nvn_dirty_snapshot_begin(base, bytes, 0, 0, &plan) == 0);
    CHECK(plan.reason == NVN_DIRTY_SNAPSHOT_DISABLED);

    nvn_dirty_remove_range(base);
    CHECK(munmap(base, bytes) == 0);
    if (failures) return 1;
    puts("PASS dirty snapshot transaction: full/delta/epoch/incarnation/abort/protection");
    return 0;
}
