from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
APP = HERE.parent
RUNTIME = APP / "runtime"
IOS = APP.parent / "ios" / "Sources"
HOST = APP.parent / "host"


def build(cc: str, work: Path, label: str, source: str,
          test_source: Path | None = None) -> Path:
    c_file = work / f"{label}.c"
    exe = work / label
    c_file.write_text(source, encoding="utf-8")
    cmd = [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
           "-DDM_INPUT_TEST",
           "-fsanitize=address,undefined",
           f"-I{APP}", f"-I{APP / 'app'}", f"-I{RUNTIME}", f"-I{IOS}", f"-I{HOST}",
           str(test_source or (HERE / "input_source.c")),
           str(c_file),
           str(APP / "app" / "touch_bridge.c"),
           str(RUNTIME / "touch_game_bridge.c"),
           str(RUNTIME / "touch_adapter.c"),
           str(RUNTIME / "input.c"),
           str(IOS / "dm_touch_controls.c"),
           "-lm", "-lpthread", "-o", str(exe)]
    done = subprocess.run(cmd, text=True, capture_output=True)
    if done.returncode != 0:
        raise RuntimeError(f"{label}: compile failed\n{done.stderr}")
    return exe


def run(exe: Path, label: str) -> tuple[int, str]:
    env = os.environ.copy()
    env["ASAN_OPTIONS"] = "detect_leaks=0"
    env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    done = subprocess.run([str(exe)], text=True, capture_output=True, env=env)
    text = done.stdout + done.stderr
    print(f"{label}: exit={done.returncode}")
    if done.stdout:
        print(done.stdout, end="")
    if done.stderr:
        print(done.stderr, end="", file=sys.stderr)
    return done.returncode, text


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    source = (APP / "app" / "input_source.c").read_text(encoding="utf-8")
    mutants = [
        ("omit-cancel",
         "    clear_touch(arbiter);\n    clear_gamepad(arbiter);\n    arbiter->source = source;",
         "    /* mutant: omit cancel/clear before source switch */\n    arbiter->source = source;"),
        ("omit-neutral",
         "    arbiter->fresh_required = 0;\n    arbiter->pending_neutral = 1;\n}\n\nstatic int invalidate",
         "    arbiter->fresh_required = 0;\n    /* mutant: omit neutral boundary */\n}\n\nstatic int invalidate"),
        ("omit-generation",
         "    if (!arbiter || !sample || !sample->connected || !arbiter->focused ||\n        arbiter->paused || !arbiter->connected ||\n        arbiter->device_id != device_id || arbiter->generation != generation)\n        return 0;",
         "    (void)generation;\n    if (!arbiter || !sample || !sample->connected || !arbiter->focused ||\n        arbiter->paused || !arbiter->connected ||\n        arbiter->device_id != device_id)\n        return 0;"),
        ("omit-request-retirement",
         "    (void)dm_touch_bridge_retire_requests(arbiter->touch, time);",
         "    (void)dm_touch_bridge_cancel_all(arbiter->touch, time);"),
    ]
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        normal = build(args.cc, work, "input-source-normal", source)
        rc, _ = run(normal, "input-source-normal")
        if rc != 0:
            print("FAIL: normal input_source fixture failed", file=sys.stderr)
            return 1
        for name, old, new in mutants:
            if old not in source:
                print(f"fail closed: mutant anchor missing for {name}", file=sys.stderr)
                return 1
            mutant = build(args.cc, work, f"input-source-mutant-{name}",
                           source.replace(old, new, 1),
                           HERE / "input_source_requests.c"
                           if name == "omit-request-retirement" else None)
            rc, text = run(mutant, f"input-source-mutant-{name}")
            if rc == 0 or "FAIL" not in text:
                print(f"FAIL: mutant {name} did not fail at an assertion", file=sys.stderr)
                return 1
            print(f"private input-source mutant {name} failed as required")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
