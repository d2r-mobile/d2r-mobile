#!/usr/bin/env python3
"""#173 private mutation control for the actual InputConnection boundary.

Reintroduces the reviewed surrogate endpoint bug (extending deletion when the
exclusive end starts a surrogate pair) and requires the focused real-class test
to fail. The production source is never overwritten.
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

APP = Path("dm/android/app/java/org/d2rmobile/android/diagnostic/DmInputConnection.java")
TEST = Path("dm/android/tests/ime-stubs/DmInputConnectionTest.java")


def run(javac: str, java: str, jar: Path, source: Path, test: Path, work: Path, label: str):
    classes = work / label
    classes.mkdir()
    build = subprocess.run(
        [javac, "-encoding", "UTF-8", "-cp", str(jar), "-d", str(classes),
         str(source), str(test)], capture_output=True, text=True)
    if build.returncode != 0:
        return "build_failed", build.returncode, build.stdout + build.stderr
    run_test = subprocess.run(
        [java, "-cp", f"{jar}{os.pathsep}{classes}",
         "org.d2rmobile.android.diagnostic.DmInputConnectionTest"],
        capture_output=True, text=True)
    return "ran", run_test.returncode, run_test.stdout + run_test.stderr


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--jar", required=True)
    parser.add_argument("--javac", default="javac")
    parser.add_argument("--java", default="java")
    args = parser.parse_args()
    root = Path(args.root)
    jar = Path(args.jar)
    app = root / APP
    test = root / TEST
    source = app.read_text(encoding="utf-8")
    good_anchor = """            if (end < committed.length() && end > cursor
                    && Character.isLowSurrogate(committed.charAt(end))
                    && Character.isHighSurrogate(committed.charAt(end - 1))) {
                end++; // deletion ends inside a pair; include its low surrogate
            }"""
    bad = """            if (end < committed.length() && end > cursor
                    && Character.isHighSurrogate(committed.charAt(end))
                    && Character.isLowSurrogate(committed.charAt(end + 1))) {
                end++;
            }"""
    if source.count(good_anchor) != 1:
        print("fail closed: endpoint mutation anchor is missing or ambiguous", file=sys.stderr)
        return 1
    composing_anchor = """        int before = beforeLength;
        if (before > 0) {"""
    composing_bad = """        int before = beforeLength;
        if (before > 0) {
            composing.setLength(0);"""
    if source.count(composing_anchor) != 1:
        print("fail closed: composing mutation anchor is missing or ambiguous", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory(prefix="dm-ime-mutation.") as td:
        work = Path(td)
        status, code, output = run(args.javac, args.java, jar, app, test, work, "control")
        if status != "ran" or code != 0:
            print("FAIL: unmutated actual test did not pass", file=sys.stderr)
            print(output, file=sys.stderr)
            return 1
        mutant = work / "DmInputConnection.java"
        mutant.write_text(source.replace(good_anchor, bad, 1), encoding="utf-8")
        status, code, output = run(args.javac, args.java, jar, mutant, test, work, "endpoint-mutant")
        if status != "ran":
            print("FAIL: endpoint mutation did not compile", file=sys.stderr)
            print(output, file=sys.stderr)
            return 1
        if code == 0 or "FAIL" not in output:
            print("FAIL: endpoint mutation passed or failed outside assertions", file=sys.stderr)
            print(output, file=sys.stderr)
            return 1
        print("mutant endpoint-surrogate-extension: rejected as intended")

        composing_dir = work / "composing-src"
        composing_dir.mkdir()
        composing_mutant = composing_dir / "DmInputConnection.java"
        composing_mutant.write_text(source.replace(composing_anchor, composing_bad, 1), encoding="utf-8")
        status, code, output = run(args.javac, args.java, jar, composing_mutant, test, work,
                                   "composing-mutant")
        if status != "ran":
            print("FAIL: composing mutation did not compile", file=sys.stderr)
            print(output, file=sys.stderr)
            return 1
        if code == 0 or "FAIL" not in output:
            print("FAIL: composing mutation passed or failed outside assertions", file=sys.stderr)
            print(output, file=sys.stderr)
            return 1
        print("mutant composing-deletion: rejected as intended")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
