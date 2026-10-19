// #249 owned layout/key-page fixture: calls the actual header used by loader.c.
#include "../../host/layout_keys.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static int parse(const char *line, unsigned *slot, unsigned *index) {
    return dm_layout_parse_key_line(line, slot, index);
}

int main(void) {
    unsigned slot, index, slots[8], page_slots = 2048;
    char error[128];
    // Current producer contract: patch_nso.py writes KEY_INDEX, an enumerate of these eight names, so
    // K18..KASPECTUI are emitted as 0..7 in this order (the old KEYS 504..511 table is not the output).
    static const char *const lines[8] = {
        "# KEY_K18_IDX\t0\n", "# KEY_KTLS_IDX\t1\n", "# KEY_KTLR_IDX\t2\n", "# KEY_KGATE_IDX\t3\n",
        "# KEY_KRES0_IDX\t4\n", "# KEY_KRES1_IDX\t5\n", "# KEY_KASPECT_IDX\t6\n", "# KEY_KASPECTUI_IDX\t7\n"
    };
    for (int i = 0; i < 8; ++i) {
        CHECK(parse(lines[i], &slot, &index) == DM_LAYOUT_KEY_OK);
        CHECK(slot == (unsigned)i);          // each parsed line maps to its producer key slot
        CHECK(index == (unsigned)i);         // and carries the producer's emitted index
        slots[slot] = index;
    }
    CHECK(dm_layout_slots_valid(slots, 8, page_slots, 8, error, sizeof error) == 0);
    CHECK(dm_layout_slots_valid(slots, 8, page_slots, -1, error, sizeof error) == 0);

    CHECK(parse("# KEY_K18_IDX 0\n", &slot, &index) == DM_LAYOUT_KEY_MALFORMED);   // no tab
    CHECK(parse("# KEY_K18_IDX\t-1\n", &slot, &index) == DM_LAYOUT_KEY_MALFORMED);
    CHECK(parse("# KEY_K18_IDX\t0x10\n", &slot, &index) == DM_LAYOUT_KEY_MALFORMED);
    CHECK(parse("# KEY_K18_IDX\t999999999999999999999\n", &slot, &index) == DM_LAYOUT_KEY_MALFORMED);
    CHECK(parse("# KEY_K18_IDX\t5 junk\n", &slot, &index) == DM_LAYOUT_KEY_MALFORMED);
    CHECK(parse("# KEY_K18_IDX\t0\njunk", &slot, &index) == DM_LAYOUT_KEY_MALFORMED);
    CHECK(parse("# PATCH_OFF\t0x1000\n", &slot, &index) == DM_LAYOUT_NOT_KEY);

    unsigned bad[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    bad[7] = 8; CHECK(dm_layout_slots_valid(bad, 8, page_slots, 8, error, sizeof error) != 0);
    CHECK(dm_layout_slots_valid(bad, 8, page_slots, -1, error, sizeof error) == 0);   // Apple allows 8
    bad[7] = 2048; CHECK(dm_layout_slots_valid(bad, 8, page_slots, -1, error, sizeof error) != 0);
    bad[7] = 0xffffffffu; CHECK(dm_layout_slots_valid(bad, 8, page_slots, -1, error, sizeof error) != 0);
    bad[7] = 0; CHECK(dm_layout_slots_valid(bad, 8, page_slots, -1, error, sizeof error) != 0);   // duplicate
    puts("PASS: producer layout accepted; malformed, overflow, duplicate, reserved and out-of-page rejected");
    return 0;
}
