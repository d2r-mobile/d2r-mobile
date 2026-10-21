// #222 owned ESR record fixture: write/read/unknown/truncated/invalid-size must stay distinguishable.
// Synthetic bytes only; no game data or signal execution.
#include "../host/dm_ucontext.h"
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static size_t put_esr(uint8_t *buf, size_t cap, uint64_t esr) {
    if (cap < 16) return 0;
    uint32_t magic = 0x45535201u, length = 16;
    memcpy(buf, &magic, 4);
    memcpy(buf + 4, &length, 4);
    memcpy(buf + 8, &esr, 8);
    return 16;
}

int main(void) {
    uint8_t record[64];
    memset(record, 0, sizeof record);
    CHECK(dm_uc_esr_write_record(record, sizeof record) == DM_UC_WRITE_UNKNOWN);
    put_esr(record, sizeof record, 0x00000040u);   // WnR = 1
    CHECK(dm_uc_esr_write_record(record, sizeof record) == DM_UC_WRITE_WRITE);
    put_esr(record, sizeof record, 0x00000000u);   // WnR = 0
    CHECK(dm_uc_esr_write_record(record, sizeof record) == DM_UC_WRITE_READ);
    // Unknown magic followed by a valid record is skipped, not guessed.
    memset(record, 0, sizeof record);
    record[0] = 0x01; record[4] = 8;
    CHECK(dm_uc_esr_write_record(record, sizeof record) == DM_UC_WRITE_UNKNOWN);
    // Truncated ESR record: claimed length 16 but buffer ends at 8.
    memset(record, 0, sizeof record);
    put_esr(record, sizeof record, 0x40u);
    CHECK(dm_uc_esr_write_record(record, 8) == DM_UC_WRITE_UNKNOWN);
    CHECK(dm_uc_esr_write_record(record, 12) == DM_UC_WRITE_UNKNOWN);
    // Invalid size arithmetic: length larger than remaining bytes.
    memset(record, 0, sizeof record);
    record[0] = 0x01; record[4] = 0x20;
    CHECK(dm_uc_esr_write_record(record, 16) == DM_UC_WRITE_UNKNOWN);
    CHECK(dm_uc_esr_write_record(NULL, 16) == DM_UC_WRITE_UNKNOWN);
    puts("PASS: ESR record write/read/unknown/truncated/malformed");
    return 0;
}
