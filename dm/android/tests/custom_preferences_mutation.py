#!/usr/bin/env python3
"""#157 mutation harness: the real preference tests must fail on two private mutations of the real controller.

* commit-ignores-store: the store result is ignored, so a rejected save would commit anyway
* ignore-lang-explicit:  lang_en is always written, changing the upstream "leave the language alone" rule

An anchor that is missing fails closed; a mutant that passes means the regression has no teeth.
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

MUTATIONS = [
    ("commit-ignores-store", "    if (store(ctx, document, length) != 0) {", "    if (0) {"),
    ("ignore-lang-explicit",
     "        if (lang && !instance->lang_explicit && !(include_pending && instance->has_pending[i])) continue;",
     "        if (0) continue;"),
]


def compile_and_run(cc: str, app: Path, tests: Path, source: Path, work: Path, label: str):
    exe = work / f"custom-preferences-{label}"
    build = subprocess.run(
        [cc, "-std=gnu11", "-g", "-fsanitize=address,undefined", f"-I{app}",
         str(tests), str(source), "-o", str(exe)],
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
    source_path = app / "custom_preferences.c"
    tests = Path(args.tests) if args.tests else app.parent / "tests" / "custom_preferences.c"
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
            mutated = work / f"custom_preferences_{label}.c"
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
