#!/usr/bin/env python3
"""#163 private mutation: the real PCM gain path must be what the tests observe.

Mutant: `apply_gain` returns the sample unchanged (gain and mute silently ignored in the real data callback).
The real composition tests compare actual PCM, so they must fail. A missing anchor fails closed.
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

MUTATIONS = [
    ("ignore-gain", "    if (gain_q15 >= 32768u) return sample;", "    return sample;"),
]


def build_and_run(cc: str, app: Path, audio: Path, tests: Path, source: Path, work: Path, label: str):
    exe = work / f"audio-output-{label}"
    build = subprocess.run(
        [cc, "-std=gnu11", "-g", "-fsanitize=address,undefined", f"-I{app}", f"-I{audio}",
         str(tests), str(app / "audio_output.c"), str(app / "audio_focus.c"), str(source), "-lm", "-o", str(exe)],
        capture_output=True, text=True)
    if build.returncode != 0:
        return "build_failed", build.returncode, build.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    return "ran", run.returncode, run.stdout + run.stderr


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", required=True)
    parser.add_argument("--audio", required=True)
    parser.add_argument("--tests", required=True)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()

    app, audio, tests = Path(args.app), Path(args.audio), Path(args.tests)
    source_path = audio / "dm_aaudio.c"
    original = source_path.read_text(encoding="utf-8")

    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        status, code, output = build_and_run(args.cc, app, audio, tests, source_path, work, "control")
        if status != "ran" or code != 0:
            print(f"FAIL: unmutated composition did not pass its own tests (status={status}, exit={code})",
                  file=sys.stderr)
            print(output, file=sys.stderr)
            return 1
        for label, old, new in MUTATIONS:
            if original.count(old) != 1:
                print(f"fail closed: mutation anchor {label!r} found {original.count(old)} times", file=sys.stderr)
                return 1
            mutated = work / f"dm_aaudio_{label}.c"
            mutated.write_text(original.replace(old, new), encoding="utf-8")
            status, code, output = build_and_run(args.cc, app, audio, tests, mutated, work, label)
            if status != "ran":
                print(f"FAIL: mutant {label} did not compile", file=sys.stderr)
                print(output, file=sys.stderr)
                return 1
            if code == 0:
                print(f"FAIL: mutant {label} passed the PCM tests, the gain regression has no teeth", file=sys.stderr)
                return 1
            if "FAIL " not in output:
                print(f"FAIL: mutant {label} failed outside a PCM assertion", file=sys.stderr)
                print(output, file=sys.stderr)
                return 1
            print(f"mutant {label}: rejected as intended")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
