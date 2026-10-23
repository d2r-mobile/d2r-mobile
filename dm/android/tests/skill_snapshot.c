// #35 shared skill snapshot dependency fixture: real parse/group/identity,
// Android-safe self reader semantics, and bridge attach gate/scene behaviour.
#include "dm_touch_game_state.h"
#include "dm_touch_game_identity.h"
#include "dm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)

module_t g_mod[MAX_MOD];
int g_nmod;
static int gate6_present;
static void (*gate6_callback)(const uint64_t *regs);
static uint64_t fake_now;
void gate_obs_set(int id, void (*fn)(const uint64_t *)) { if (id == 6) gate6_callback = fn; }
int gate_obs_present(int id) { return id == 6 && gate6_present; }
uint64_t now_ns(void) { return ++fake_now; }
int dm_swapskill_equipped(int32_t item) { (void)item; return 1; }

static void fill_mapping(unsigned char *mapping) {
    static const uint32_t logical[6] = {0x1000, 0x2000, 0x4000, 0x8000, 0x200, 0x800};
    for (int i = 0; i < 6; ++i) {
        uint32_t values[3] = {logical[i], (uint32_t)i, (uint32_t)(6 + i)};
        memcpy(mapping + i * 12, values, sizeof values);
    }
}

static int test_safe_reader(void) {
    long page = sysconf(_SC_PAGESIZE);
    CHECK(page > 0);
    unsigned char *mapping = mmap(NULL, (size_t)page * 2, PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(mapping != MAP_FAILED);
    for (long i = 0; i < page * 2; ++i) mapping[i] = (unsigned char)(i * 13 + 7);
    unsigned char out[32] = {0};
    CHECK(dm_tg_vm_read((uint64_t)(uintptr_t)(mapping + page - 8), out, sizeof out));
    CHECK(memcmp(out, mapping + page - 8, sizeof out) == 0);
    CHECK(!dm_tg_vm_read(0, out, sizeof out));
    CHECK(!dm_tg_vm_read((uint64_t)(uintptr_t)mapping, out, 0));
    CHECK(!dm_tg_vm_read(UINTPTR_MAX - 2, out, 8));
    CHECK(mprotect(mapping, (size_t)page, PROT_NONE) == 0);
    CHECK(!dm_tg_vm_read((uint64_t)(uintptr_t)mapping, out, sizeof out));
    CHECK(mprotect(mapping, (size_t)page, PROT_READ | PROT_WRITE) == 0);
    CHECK(munmap(mapping, (size_t)page * 2) == 0);
    CHECK(!dm_tg_vm_read((uint64_t)(uintptr_t)mapping, out, sizeof out));
    return 1;
}

static int test_parse_and_group(void) {
    unsigned char mapping[72], bank[DM_TG_SLOTS * DM_TG_STRIDE];
    fill_mapping(mapping);
    memset(bank, 0xff, sizeof bank); // all slot values UINT32_MAX = empty

    dm_tg_snapshot snapshot;
    CHECK(dm_tg_parse(&snapshot, mapping, sizeof mapping, bank, sizeof bank,
                      0, 0, 0, UINT32_MAX, 100));
    CHECK(snapshot.valid && snapshot.supported && snapshot.portal.slot == -1);

    dm_tg_group group = {0};
    for (int slot = 0; slot < 6; ++slot) {
        CHECK(dm_tg_parse(&snapshot, mapping, sizeof mapping, bank, sizeof bank,
                          0, slot, slot, UINT32_MAX, 100 + (uint64_t)slot));
        CHECK(dm_tg_group_observe(&group, &snapshot, 1, 1, 11, 22,
                                  DM_TG_IDENTITY_NATIVE) == (slot == 5));
    }
    CHECK(group.completed.ready && group.completed.group_seen == 63);

    dm_tg_group unknown = {0};
    CHECK(!dm_tg_group_observe(&unknown, &snapshot, 1, 1, 11, 22,
                               DM_TG_IDENTITY_UNCONFIRMED));
    CHECK(!unknown.completed.ready);

    // A portal slot is valid, but freshness is bounded by the original TTL.
    uint32_t portal_id = 359;
    memcpy(bank, &portal_id, 4);
    CHECK(dm_tg_parse(&snapshot, mapping, sizeof mapping, bank, sizeof bank,
                      0, 0, 0, portal_id, 1000));
    CHECK(snapshot.portal.valid);
    snapshot.ready = 1;
    CHECK(dm_tg_portal_mask(&snapshot, 1000 + DM_TG_MAX_AGE_NS - 1) != 0);
    CHECK(dm_tg_portal_mask(&snapshot, 1000 + DM_TG_MAX_AGE_NS + 1) == 0);
    CHECK(dm_tg_portal_mask(&snapshot, 999) == 0);
    return 1;
}

static int test_identity(void) {
    enum { BASE = 0x100000, SLOT = 3 };
    dm_tg_hud_cohort cohort = {0};
    unsigned char *widget = calloc(1, 0xc91);
    CHECK(widget != NULL);
    uint32_t slot = SLOT;
    memcpy(widget + 0xb18, &slot, sizeof slot);
    widget[0xc90] = 0;

    uint64_t frame[3] = {(uint64_t)(uintptr_t)widget, 0, (uint64_t)(BASE + 0x3e2f74)};
    uint64_t regs[32] = {0};
    regs[19] = SLOT;
    regs[29] = (uint64_t)(uintptr_t)&frame[1]; // fp -> frame[1], fp-8 = frame[0]
    uint64_t caller = 0, hud = 0;
    CHECK(dm_tg_hud_identity(&cohort, regs, BASE, 1, 1, dm_tg_vm_read,
                             &caller, &hud) == DM_TG_IDENTITY_UNCONFIRMED);
    CHECK(cohort.seen == (1u << SLOT));
    free(widget);
    return 1;
}

static int test_attach(void) {
    unsigned char *main_image = calloc(1, 0x2d5f0c0 + 32 * DM_TG_STRIDE + 8);
    CHECK(main_image != NULL);
    memset(g_mod, 0, sizeof g_mod);
    g_nmod = 1;
    snprintf(g_mod[0].name, sizeof g_mod[0].name, "main");
    g_mod[0].base = main_image;
    g_mod[0].size = 0x2d5f0c0 + 32 * DM_TG_STRIDE + 8;

    gate6_present = 0;
    gate6_callback = NULL;
    CHECK(!dm_touch_game_attach());
    dm_tg_snapshot snapshot;
    CHECK(!dm_touch_game_snapshot(&snapshot) && !snapshot.supported);

    gate6_present = 1;
    CHECK(dm_touch_game_attach());
    CHECK(gate6_callback != NULL);
    dm_touch_game_set_scene(1, 1, 1);
    CHECK(!dm_touch_game_snapshot(&snapshot) && snapshot.supported);
    dm_touch_game_set_scene(0, 2, 2);

    free(main_image);
    g_nmod = 0;
    return 1;
}

static void emit_hud(unsigned char widget[6][0xc98], uint64_t regs[32], uint64_t frame[3]) {
    for(uint32_t slot=0;slot<6;slot++) {
        memcpy(widget[slot]+0xb18,&slot,4);
        frame[0]=(uint64_t)(uintptr_t)widget[slot];
        regs[19]=regs[23]=slot;regs[8]=(uint64_t)slot*DM_TG_STRIDE;
        gate6_callback(regs);
    }
}

static int test_layout_rebuild(void) {
    const size_t size=0x2d5f0c0+32*DM_TG_STRIDE;
    unsigned char *image=calloc(1,size);
    CHECK(image!=NULL);
    g_nmod=1;strcpy(g_mod[0].name,"main");g_mod[0].base=image;g_mod[0].size=size;
    fill_mapping(image+0x2d13cbc);
    for(uint32_t i=0;i<32;i++)memcpy(image+0x2d5f0c0+i*DM_TG_STRIDE,&i,4);
    gate6_present=1;CHECK(dm_touch_game_attach());
    dm_touch_game_set_scene(1,10,10);dm_touch_game_set_layout(100);
    uint64_t frame[3]={0,0,(uint64_t)(uintptr_t)image+0x3e2f74},regs[32]={0};
    regs[10]=(uint64_t)(uintptr_t)image+0x2d5f0c0;regs[29]=(uint64_t)(uintptr_t)&frame[1];
    unsigned char before[6][0xc98]={{0}},after[6][0xc98]={{0}};
    dm_tg_snapshot s;
    emit_hud(before,regs,frame);CHECK(!dm_touch_game_snapshot(&s));
    emit_hud(before,regs,frame);CHECK(dm_touch_game_snapshot(&s)&&s.ready);
    uint64_t old_identity=s.hud_identity,old_capture=s.captured_ns;
    dm_touch_game_set_layout(100);
    CHECK(dm_touch_game_snapshot(&s)&&s.captured_ns==old_capture);
    emit_hud(after,regs,frame);
    CHECK(dm_touch_game_snapshot(&s)&&s.hud_identity==old_identity&&s.captured_ns==old_capture);
    dm_touch_game_set_layout(101);dm_touch_game_set_scene(1,11,10);
    CHECK(!dm_touch_game_snapshot(&s)&&!s.ready&&s.world_epoch==10);
    emit_hud(after,regs,frame);CHECK(!dm_touch_game_snapshot(&s));
    emit_hud(after,regs,frame);
    CHECK(dm_touch_game_snapshot(&s)&&s.ready&&s.group_seen==63&&s.hud_identity!=old_identity);
    CHECK(s.scene_epoch==11&&s.world_epoch==10);
    old_capture=s.captured_ns;emit_hud(before,regs,frame);
    CHECK(dm_touch_game_snapshot(&s)&&s.captured_ns==old_capture);
    dm_touch_game_set_layout(102);dm_touch_game_set_scene(1,12,10);
    emit_hud(before,regs,frame);CHECK(!dm_touch_game_snapshot(&s));
    emit_hud(before,regs,frame);CHECK(dm_touch_game_snapshot(&s)&&s.ready&&s.hud_identity==old_identity);
    free(image);g_nmod=0;
    return 1;
}

int main(void) {
    CHECK(test_safe_reader());
    CHECK(test_parse_and_group());
    CHECK(test_identity());
    CHECK(test_attach());
    CHECK(test_layout_rebuild());
    puts("PASS: shared skill snapshot parses/binds identity, safe-reader rejects invalid/protected spans, and bridge attach/scene fail closed");
    return 0;
}
