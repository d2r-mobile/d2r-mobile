#!/usr/bin/env python3
"""#153 mutation harness: the real audio-focus tests must fail when generation or user intent is ignored.

Two private mutations of the actual controller are compiled together with the real test file:

* ignore-generation: the late-event generation check is removed
* ignore-intent:     desired_play is dropped from the play decision

Both must compile and then fail a test assertion. A mutant that builds cleanly and passes means the regression has
no teeth; an anchor that is missing fails closed instead of silently mutating nothing.
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

MUTATIONS = [
    ("ignore-generation", "if (generation != policy->generation) {", "if (0) {"),
    ("ignore-intent", "if (!policy->desired_play || policy->requires_user_rearm) return 0;",
     "if (policy->requires_user_rearm) return 0;"),
]


def compile_and_run(cc: str, app: Path, tests: Path, source: Path, work: Path, label: str):
    exe = work / f"audio-focus-{label}"
    build = subprocess.run(
        [cc, "-std=gnu11", "-g", "-fsanitize=address,undefined", f"-I{app}",
         str(tests), str(source), "-lm", "-o", str(exe)],
        capture_output=True, text=True)
    if build.returncode != 0:
        return "build_failed", build.returncode, build.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    return "ran", run.returncode, run.stdout + run.stderr


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", required=True)
    parser.add_argument("--tests", default=None)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()

    app = Path(args.app)
    source_path = app / "audio_focus.c"
    tests = Path(args.tests) if args.tests else app.parent / "tests" / "audio_focus.c"
    original = source_path.read_text(encoding="utf-8")

    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        status, code, output = compile_and_run(args.cc, app, tests, source_path, work, "control")
        if status != "ran" or code != 0:
            print(f"FAIL: unmutated controller did not pass its own tests (status={status}, exit={code})",
                  file=sys.stderr)
            print(output, file=sys.stderr)
            return 1
        for label, old, new in MUTATIONS:
            if original.count(old) != 1:
                print(f"fail closed: mutation anchor {label!r} found {original.count(old)} times", file=sys.stderr)
                return 1
            mutated = work / f"audio_focus_{label}.c"
            mutated.write_text(original.replace(old, new), encoding="utf-8")
            status, code, output = compile_and_run(args.cc, app, tests, mutated, work, label)
            if status != "ran":
                print(f"FAIL: mutant {label} did not compile", file=sys.stderr)
                print(output, file=sys.stderr)
                return 1
            if code == 0:
                print(f"FAIL: mutant {label} passed the real tests, regression has no teeth", file=sys.stderr)
                return 1
            if "FAIL " not in output:
                print(f"FAIL: mutant {label} failed outside a test assertion", file=sys.stderr)
                print(output, file=sys.stderr)
                return 1
            print(f"mutant {label}: rejected as intended")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
