#!/usr/bin/env python3
"""Write a bounded, sanitized CI artifact manifest."""
import argparse
import hashlib
import json
import os
import platform
import re
import subprocess
from pathlib import Path


def run(*args):
    try:
        return subprocess.run(args, capture_output=True, text=True, check=False).stdout.strip()
    except OSError:
        return ""


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", required=True, choices=["host", "android"])
    parser.add_argument("--repo", required=True)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--artifact-dir", required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--test-status", required=True)
    parser.add_argument("--pages", default="")
    parser.add_argument("--param", action="append", default=[])
    args = parser.parse_args()
    repo = Path(args.repo).resolve()
    outdir = Path(args.artifact_dir).resolve()
    outdir.mkdir(parents=True, exist_ok=True)
    head = run("git", "-C", str(repo), "rev-parse", "HEAD")
    dirty = bool(run("git", "-C", str(repo), "status", "--porcelain"))
    depfile = repo / "dm/android/dependencies.cmake"
    deps = []
    if depfile.exists():
        text = depfile.read_text()
        for m in re.finditer(r"URL\s+(\S+).*?URL_HASH\s+SHA256=([0-9a-f]{64})",
                             text, re.S):
            deps.append({"url": m.group(1), "sha256": m.group(2)})
    pages = None
    if args.pages and Path(args.pages).exists():
        pages = json.loads(Path(args.pages).read_text())
    ndk = os.environ.get("ANDROID_NDK_HOME") or os.environ.get("ANDROID_NDK_ROOT")
    ndk_rev = ""
    if ndk:
        prop = Path(ndk) / "source.properties"
        if prop.exists():
            ndk_rev = next((line.split("=", 1)[1].strip()
                            for line in prop.read_text().splitlines()
                            if line.startswith("Pkg.Revision")), "")
    cc = run(os.environ.get("CC", "cc"), "--version").splitlines()
    manifest = {
        "schema": "dm-ci-manifest/v1",
        "mode": args.mode,
        "commit": head,
        "dirty": dirty,
        "source_label": "checkout",
        "build_label": args.mode,
        "workspace_label": "external",
        "tool_versions": {
            "os": platform.platform(),
            "machine": platform.machine(),
            "python": platform.python_version(),
            "cmake": run("cmake", "--version").splitlines()[0] if run("cmake", "--version") else "",
            "cc": cc[0] if cc else "",
            "readelf": run("readelf", "--version").splitlines()[0] if run("readelf", "--version") else "",
            "ndk": ndk_rev,
        },
        "dependencies": deps,
        "parameters": dict(item.split("=", 1) for item in args.param
                           if "=" in item and not item.startswith("CI_WORK=")),
        "tests": {"status": args.test_status, "ctest_log": Path(args.log).name},
        "skips": (["android_arm64_execution", "device_execution"]
                  if args.mode == "android" else []),
        "page_alignment": pages,
        "artifact_allowlist": [],
        "limitations": (
            ["cross_compile_only", "no_arm64_execution", "no_device_execution",
             "no_game_or_sdk_input"]
            if args.mode == "android" else
            ["host_native_tests", "no_device_execution", "no_game_or_sdk_input"]
        ),
    }
    manifest_path = outdir / "manifest.json"
    allow = [manifest_path.name, Path(args.log).name]
    if args.pages and Path(args.pages).exists():
        allow.append(Path(args.pages).name)
    manifest["artifact_allowlist"] = sorted(set(allow))
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print(manifest_path)


if __name__ == "__main__":
    main()
