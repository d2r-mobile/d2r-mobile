#include "texture_flat_probe.h"

#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static const char *k_shader = "ce3ad354c986bf85";
static const char *k_valid = "ce3ad354c986bf85:1:1";

int main(void) {
    texture_flat_selector selector = {0};
    CHECK(texture_flat_parse(k_valid, &selector));
    CHECK(!strcmp(selector.shader, k_shader) && selector.stage == 1 && selector.slot == 1);

    // Strict full-string inputs: short, prefixed, uppercase, trailing junk and oversized are all off.
    CHECK(!texture_flat_parse(NULL, &selector));
    CHECK(!texture_flat_parse("1", &selector));
    CHECK(!texture_flat_parse("1abcdefghijklmnop:1:1", &selector));
    CHECK(!texture_flat_parse("CE3AD354C986BF85:1:1", &selector));
    CHECK(!texture_flat_parse("ce3ad354c986bf85:1:1x", &selector));
    CHECK(!texture_flat_parse("ce3ad354c986bf85::1", &selector));
    char long_value[80];
    memset(long_value, '1', sizeof long_value - 1);
    long_value[sizeof long_value - 1] = 0;
    CHECK(!texture_flat_parse(long_value, &selector));

    // Selected binding only; stage/slot/shader are exact.
    texture_flat_state state = {0};
    texture_flat_state_apply(&state, k_valid);
    CHECK(state.enabled);
    CHECK(texture_flat_state_matches(&state, k_shader, 1, 1, 3, 0, 0x44));
    CHECK(!texture_flat_state_matches(&state, k_shader, 1, 2, 3, 0, 0x44));
    CHECK(!texture_flat_state_matches(&state, k_shader, 2, 1, 3, 0, 0x44));
    CHECK(!texture_flat_state_matches(&state, "0000000000000000", 1, 1, 3, 0, 0x44));

    // Color 2D only: arrays, depth/comparison, storage and non-color formats are excluded.
    CHECK(texture_flat_state_matches(&state, k_shader, 1, 1, 3, 0, 0x25));
    CHECK(!texture_flat_state_matches(&state, k_shader, 1, 1, 0x103, 0, 0x44));
    CHECK(!texture_flat_state_matches(&state, k_shader, 1, 1, 0x403, 0, 0x44));
    CHECK(!texture_flat_state_matches(&state, k_shader, 1, 1, 3, 1, 0x44));
    CHECK(!texture_flat_state_matches(&state, k_shader, 1, 1, 3, 0, 0x49));
    CHECK(!texture_flat_state_matches(&state, k_shader, 1, 1, 3, 0, 0x31));

    // Disable restores the real descriptor path; a later valid value can re-arm.
    texture_flat_state_apply(&state, "");
    CHECK(!state.enabled && !texture_flat_state_matches(&state, k_shader, 1, 1, 3, 0, 0x44));
    texture_flat_state_apply(&state, "invalid");
    CHECK(!state.enabled);
    texture_flat_state_apply(&state, k_valid);
    CHECK(state.enabled && texture_flat_state_matches(&state, k_shader, 1, 1, 3, 0, 0x44));

    puts("PASS: texture flat selector, strict property parse and restore");
    return 0;
}
