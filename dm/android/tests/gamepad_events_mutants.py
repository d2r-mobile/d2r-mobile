from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
APP = HERE.parent
HOST = APP.parent / "host"


def build(cc: str, work: Path, label: str, source: str) -> Path:
    c_file = work / f"{label}.c"
    exe = work / label
    c_file.write_text(source, encoding="utf-8")
    cmd = [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
           "-fsanitize=address,undefined",
           f"-I{APP}", f"-I{APP / 'app'}", f"-I{APP / 'runtime'}", f"-I{HOST}",
           str(HERE / "gamepad_events.c"), str(c_file), "-lm", "-o", str(exe)]
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
    source = (APP / "app" / "gamepad_events.c").read_text(encoding="utf-8")
    mutants = [
        ("face-a-as-b",
         "    case AKEYCODE_BUTTON_A: return DM_BTN_A;",
         "    case AKEYCODE_BUTTON_A: return DM_BTN_B;"),
        ("omit-trigger-release",
         "        next_context.analog_buttons = 0;\n        next.buttons = (state->buttons & ~old) |\n                       next_context.digital_buttons | next_context.analog_buttons;\n        next.lx = next.ly = next.rx = next.ry = 0;",
         "        next.buttons = (state->buttons & ~old) |\n                       next_context.digital_buttons | next_context.analog_buttons;\n        next.lx = next.ly = next.rx = next.ry = 0;"),
        ("source-class-exact",
         "return (source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD ||\n           (source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK;",
         "return source == AINPUT_SOURCE_GAMEPAD || source == AINPUT_SOURCE_JOYSTICK;"),
    ]
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        normal = build(args.cc, work, "gamepad-normal", source)
        rc, _ = run(normal, "gamepad-normal")
        if rc != 0:
            print("FAIL: normal gamepad fixture failed", file=sys.stderr)
            return 1
        for name, old, new in mutants:
            if old not in source:
                print(f"fail closed: mutant anchor missing for {name}", file=sys.stderr)
                return 1
            mutant = build(args.cc, work, f"gamepad-mutant-{name}",
                           source.replace(old, new, 1))
            rc, text = run(mutant, f"gamepad-mutant-{name}")
            if rc == 0 or "FAIL line" not in text:
                print(f"FAIL: mutant {name} did not fail at an assertion", file=sys.stderr)
                return 1
            print(f"private gamepad mutant {name} failed as required")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
