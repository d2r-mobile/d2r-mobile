#!/usr/bin/env python3
"""Extract the real padmode release functions and run them against fake state.

This is a source-path regression: it does not copy the body into the fixture.
If a production release stops clearing a published touch/pad field, the
extracted function fails here. Build/run locally with clang; no Android SDK.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "native" / "frontend_activity.c"
TEXT = SOURCE.read_text()


def extract(name: str) -> str:
    match = re.search(rf"(?ms)^static void {name}\(game_app \*s\) \{{.*?^\}}", TEXT)
    if not match:
        sys.exit(f"fail closed: production {name} not found")
    return match.group(0)


PRELUDE = r"""
#include <stdint.h>
#include <stdio.h>
typedef struct {
    uint64_t touch_buttons;
    int touch_lx, touch_ly, touch_rx, touch_ry, tap_a, touch_map;
    uint64_t pad_buttons, hat_buttons, trigger_buttons;
    int32_t lx, ly, rx, ry;
} game_app;
static int live_cancel_calls, pointer_clear_calls, touch_set_calls;
static void live_touch_cancel_sources_all(game_app *s) { (void)s; live_cancel_calls++; }
static void dm_touch_pointer_clear(int *p) { *p = 0; pointer_clear_calls++; }
static void dm_touch_set(void *p, int n) { (void)p; (void)n; touch_set_calls++; }
"""


def main() -> int:
    release_touch = extract("padmode_release_touch")
    release_pad = extract("padmode_release_pad")
    program = PRELUDE + release_touch + "\n" + release_pad + r"""
int main(void) {
    game_app s = {
        .touch_buttons = 0xffff, .touch_lx = 200, .touch_ly = -200,
        .touch_rx = 300, .touch_ry = 400, .tap_a = 1, .touch_map = 1,
        .pad_buttons = 7, .hat_buttons = 8, .trigger_buttons = 9,
        .lx = 10, .ly = 11, .rx = 12, .ry = 13,
    };
    padmode_release_touch(&s);
    if (s.touch_buttons || s.touch_lx || s.touch_ly || s.touch_rx || s.touch_ry ||
        s.tap_a || s.touch_map || !live_cancel_calls || !pointer_clear_calls || !touch_set_calls) {
        fputs("FAIL production release leaves legacy touch state or misses live/raw cancel\n", stderr);
        return 1;
    }
    padmode_release_pad(&s);
    if (s.pad_buttons || s.hat_buttons || s.trigger_buttons ||
        s.lx || s.ly || s.rx || s.ry) {
        fputs("FAIL production release leaves physical pad state\n", stderr);
        return 1;
    }
    puts("PASS padmode release: legacy touch + live/raw cancel; physical pad fields");
    return 0;
}
"""
    with tempfile.TemporaryDirectory(prefix="padmode-release-") as td:
        c = Path(td) / "release.c"
        exe = Path(td) / "release"
        c.write_text(program)
        subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        str(c), "-o", str(exe)], check=True)
        return subprocess.run([str(exe)]).returncode


if __name__ == "__main__":
    raise SystemExit(main())
