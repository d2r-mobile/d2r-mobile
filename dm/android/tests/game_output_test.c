// #254 owned output-geometry fixture: matching/wider aspect, missing/invalid size, rotation, bounds.
#include "../native/game_output.h"
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static int expect(unsigned sw, unsigned sh, unsigned ew, unsigned eh) {
    unsigned w = 0, h = 0;
    int ok = dm_game_output_phone(sw, sh, &w, &h);
    if (!ok) { fprintf(stderr, "FAIL %ux%u: no output\n", sw, sh); return 0; }
    if (w != ew || h != eh) { fprintf(stderr, "FAIL %ux%u -> %ux%u, want %ux%u\n", sw, sh, w, h, ew, eh); return 0; }
    return 1;
}

static int fallback(unsigned sw, unsigned sh) {
    unsigned w = 99, h = 99;
    if (dm_game_output_phone(sw, sh, &w, &h)) { fprintf(stderr, "FAIL %ux%u should keep 16:9\n", sw, sh); return 0; }
    return 1;
}

int main(void) {
    CHECK(expect(2400, 1080, 1600, 720));          // 20:9 phone
    CHECK(expect(2340, 1080, 1560, 720));          // 19.5:9 phone
    CHECK(expect(3200, 1440, 1600, 720));          // native panel size must not become render size
    CHECK(expect(3440, 1440, 1720, 720));          // 21.5:9, still bounded
    CHECK(expect(5120, 1440, 1920, 720));          // ultra-wide cap
    CHECK(fallback(1920, 1080));                    // exact 16:9 compatibility
    CHECK(fallback(1600, 1000));                    // narrower than 16:9
    CHECK(fallback(1080, 2400));                    // portrait/rotation fallback
    CHECK(fallback(0, 0));                          // no window
    CHECK(fallback(319, 240));                      // invalid tiny surface
    CHECK(fallback(320, 239));
    CHECK(fallback(0xffffffffu, 240));              // UINT_MAX: no float-cast overflow
    CHECK(fallback(0xffffffffu, 1080));
    CHECK(fallback(16385, 720));                    // absurd surface bound
    CHECK(fallback(240, 0xffffffffu));
    puts("PASS: phone-aspect output bounded at 720 height; 16:9/invalid/portrait fall back");
    return 0;
}
