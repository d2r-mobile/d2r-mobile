#!/usr/bin/env python3
"""#179 private mutation controls for the actual View/session helper.

Neither mutation touches production files. One loses the matching BACK UP
consume; the other recreates the session connection on a repeated query.
Both must make the focused actual test fail.
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path("dm/android/app/java/org/d2rmobile/android/diagnostic")
STUBS = Path("dm/android/tests/ime-view-stubs/android")
TEST = Path("dm/android/tests/ime-view-stubs/DmTextEditorViewTest.java")


def run_case(javac, java, jar, stubs, app, source, work, label):
    classes = work / label
    classes.mkdir()
    build = subprocess.run(
        [javac, "--release", "11", "-cp", f"{stubs}{os.pathsep}{jar}",
         "-d", str(classes), str(source), str(app / "DmInputConnection.java"),
         str(TEST)], capture_output=True, text=True)
    if build.returncode != 0:
        return "build_failed", build.returncode, build.stdout + build.stderr
    proc = subprocess.run(
        [java, "-cp", f"{stubs}{os.pathsep}{classes}{os.pathsep}{jar}",
         "org.d2rmobile.android.diagnostic.DmTextEditorViewTest"],
        capture_output=True, text=True)
    return "ran", proc.returncode, proc.stdout + proc.stderr


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--jar", required=True)
    parser.add_argument("--javac", default="javac")
    parser.add_argument("--java", default="java")
    args = parser.parse_args()
    root = Path(args.root)
    jar = Path(args.jar)
    app = root / ROOT
    test = root / TEST
    source_path = app / "DmTextEditorView.java"
    source = source_path.read_text(encoding="utf-8")
    back_anchor = """            backGestureInProgress = true;
            retire(active, true);"""
    back_bad = """            backGestureInProgress = false;
            retire(active, true);"""
    document_anchor = """        if (active.connection == null) {
            active.connection = new DmInputConnection(new SessionSink(active));
        }"""
    document_bad = """        if (active.connection != null) active.connection.closeConnection();
        active.connection = new DmInputConnection(new SessionSink(active));"""
    if source.count(back_anchor) != 1 or source.count(document_anchor) != 1:
        print("fail closed: mutation anchor missing or ambiguous", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory(prefix="dm-ime-view-mutation.") as td:
        work = Path(td)
        stubs = work / "stubs"
        stubs.mkdir()
        stub_sources = sorted(str(p) for p in (root / STUBS).rglob("*.java"))
        build = subprocess.run(
            [args.javac, "--release", "11", "-cp", str(jar), "-d", str(stubs)] + stub_sources,
            capture_output=True, text=True)
        if build.returncode != 0:
            print("FAIL: stub compile failed", file=sys.stderr)
            print(build.stdout + build.stderr, file=sys.stderr)
            return 1
        status, code, output = run_case(args.javac, args.java, jar, stubs, app,
                                        source_path, work, "control")
        if status != "ran" or code != 0:
            print("FAIL: unmutated actual test did not pass", file=sys.stderr)
            print(output, file=sys.stderr)
            return 1
        for label, anchor, replacement in (
                ("back-up-leak", back_anchor, back_bad),
                ("recreate-document", document_anchor, document_bad)):
            mutant = work / label
            mutant.mkdir()
            mutated = mutant / "DmTextEditorView.java"
            mutated.write_text(source.replace(anchor, replacement, 1), encoding="utf-8")
            status, code, output = run_case(args.javac, args.java, jar, stubs, app,
                                            mutated, work, label + "-classes")
            if status != "ran":
                print(f"FAIL: {label} mutation did not compile", file=sys.stderr)
                print(output, file=sys.stderr)
                return 1
            if code == 0 or "FAIL" not in output:
                print(f"FAIL: {label} mutation passed or failed outside assertions", file=sys.stderr)
                print(output, file=sys.stderr)
                return 1
            print(f"mutant {label}: rejected as intended")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
