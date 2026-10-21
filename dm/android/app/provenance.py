#!/usr/bin/env python3
"""Offline provenance sidecar for the owned diagnostic APK.

The sidecar is written next to the final signed APK only after the caller has
verified the signature. It binds the APK's actual bytes, its selected owned
entries and the source/build cache facts. It never reads a keystore, game asset
or user file path, and it never emits absolute paths.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

SCHEMA = "dm-diagnostic-apk-provenance/v1"
NATIVE = "libdm_android_app.so"
FIXTURE = "assets/synthetic.dma"
SHADER_VERT = "assets/indexed_textured.vert.spv"
SHADER_FRAG = "assets/indexed_textured.frag.spv"
COMMIT_RE = re.compile(r"^[0-9a-f]{7,64}$")


class ProvenanceError(Exception):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def cache_values(path: Path | str | None) -> dict[str, str]:
    values: dict[str, str] = {}
    if path is None:
        return values
    path = Path(path)
    cache = path / "CMakeCache.txt"
    if not cache.is_file():
        return values
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if "=" not in line or line.startswith("#"):
            continue
        key, value = line.split("=", 1)
        key = key.split(":", 1)[0]
        values[key] = value
    return values


def source_facts(args: argparse.Namespace) -> tuple[str, bool]:
    commit = args.source_commit
    if commit is None:
        if not args.source_root:
            raise ProvenanceError("source commit is unknown; pass --source-root or --source-commit")
        try:
            commit = subprocess.check_output(
                ["git", "-C", str(args.source_root), "rev-parse", "HEAD"],
                text=True, stderr=subprocess.DEVNULL).strip()
        except (OSError, subprocess.CalledProcessError):
            raise ProvenanceError("cannot resolve source commit") from None
    if not COMMIT_RE.fullmatch(commit):
        raise ProvenanceError("source commit must be a lowercase hexadecimal git object id")
    if args.source_dirty is not None:
        dirty = args.source_dirty == "true"
    else:
        if not args.source_root:
            raise ProvenanceError("source dirty state is unknown; pass --source-root or --source-dirty")
        try:
            status = subprocess.check_output(
                ["git", "-C", str(args.source_root), "status", "--porcelain",
                 "--untracked-files=normal"],
                text=True, stderr=subprocess.DEVNULL)
        except (OSError, subprocess.CalledProcessError):
            raise ProvenanceError("cannot determine source dirty state") from None
        dirty = bool(status.strip())
    return commit, dirty


def zip_entries(apk: Path) -> dict[str, zipfile.ZipInfo]:
    try:
        archive = zipfile.ZipFile(apk)
    except (OSError, zipfile.BadZipFile) as exc:
        raise ProvenanceError("APK is not a readable zip archive") from exc
    with archive:
        infos = archive.infolist()
        names = [info.filename for info in infos]
        if len(names) != len(set(names)):
            raise ProvenanceError("APK contains duplicate entry names")
        return {info.filename: info for info in infos}


def select_entries(apk: Path, abi: str) -> list[tuple[str, bytes]]:
    infos = zip_entries(apk)
    native_candidates = [name for name in infos if name.startswith("lib/") and
                         name.endswith("/" + NATIVE)]
    if abi != "unknown":
        native = f"lib/{abi}/{NATIVE}"
        if native not in infos:
            raise ProvenanceError("APK has no native entry for the cached ABI")
    else:
        if len(native_candidates) != 1:
            raise ProvenanceError("APK native ABI is unknown or ambiguous")
        native = native_candidates[0]
    required = [native, FIXTURE, SHADER_VERT, SHADER_FRAG]
    for name in required:
        if name not in infos:
            raise ProvenanceError(f"APK is missing required owned entry: {name}")
        info = infos[name]
        if info.is_dir() or info.file_size < 0:
            raise ProvenanceError(f"APK has an invalid owned entry: {name}")
    selected: list[tuple[str, bytes]] = []
    try:
        with zipfile.ZipFile(apk) as archive:
            for name in required:
                selected.append((name, archive.read(name)))
    except (OSError, KeyError, zipfile.BadZipFile) as exc:
        raise ProvenanceError("cannot read required APK entries") from exc
    return selected


def abi_from_cache(cache: dict[str, str]) -> str:
    return cache.get("ANDROID_ABI", "unknown") or "unknown"


def platform_from_cache(cache: dict[str, str]) -> object:
    raw = cache.get("ANDROID_PLATFORM", "")
    match = re.fullmatch(r"android-(\d+)", raw)
    return int(match.group(1)) if match else "unknown"


def build_facts(args: argparse.Namespace, abi: str) -> dict[str, object]:
    cache = cache_values(args.native_build)
    return {
        "abi": abi,
        "platform_api": platform_from_cache(cache),
        "min_sdk": args.min_sdk if args.min_sdk is not None else "unknown",
        "target_sdk": args.target_sdk if args.target_sdk is not None else "unknown",
        "build_tools": Path(args.build_tools).name if args.build_tools else "unknown",
        "cmake_build_type": cache.get("CMAKE_BUILD_TYPE", "unknown"),
    }


def write_sidecar(args: argparse.Namespace) -> None:
    if args.test_status != "unknown" and not args.test_reference:
        raise ProvenanceError("test evidence requires an explicit reference")
    apk = Path(args.apk)
    if not apk.is_file() or apk.stat().st_size <= 0:
        raise ProvenanceError("APK is missing or empty")
    cache = cache_values(args.native_build)
    abi = abi_from_cache(cache)
    selected = select_entries(apk, abi)
    commit, dirty = source_facts(args)
    entries = []
    for name, data in selected:
        entries.append({"path": name, "size": len(data),
                        "sha256": hashlib.sha256(data).hexdigest()})
    sidecar = {
        "schema": SCHEMA,
        "kind": "diagnostic-apk",
        "diagnostic_only": True,
        "source": {
            "commit": commit,
            "commit_short": commit[:12],
            "dirty": dirty,
            "dirty_state": "dirty" if dirty else "clean",
        },
        "apk": {
            "file": apk.name,
            "size": apk.stat().st_size,
            "sha256": sha256_file(apk),
            "entry_count": len(zip_entries(apk)),
        },
        "build": build_facts(args, abi),
        "entries": entries,
        "test_evidence": {
            "status": args.test_status,
            "reference": args.test_reference,
        },
    }
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        fd, temporary = tempfile.mkstemp(prefix=f".{output.name}.", dir=output.parent)
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(sidecar, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, output)
        temporary = None
    except OSError as exc:
        raise ProvenanceError("cannot atomically publish sidecar") from exc
    finally:
        if temporary is not None:
            try:
                os.unlink(temporary)
            except FileNotFoundError:
                pass


def is_int(value: object) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def validate_sidecar_facts(data: object) -> dict[str, object]:
    if not isinstance(data, dict):
        raise ProvenanceError("sidecar root is not an object")
    if data.get("schema") != SCHEMA or data.get("kind") != "diagnostic-apk":
        raise ProvenanceError("sidecar schema/kind is not diagnostic provenance")
    if data.get("diagnostic_only") is not True:
        raise ProvenanceError("sidecar diagnostic_only must be exactly true")
    source = data.get("source")
    if not isinstance(source, dict):
        raise ProvenanceError("sidecar source facts are missing")
    commit = source.get("commit")
    if not isinstance(commit, str) or not COMMIT_RE.fullmatch(commit):
        raise ProvenanceError("sidecar source commit is missing or malformed")
    if source.get("commit_short") != commit[:12]:
        raise ProvenanceError("sidecar source commit_short does not match commit")
    if not isinstance(source.get("dirty"), bool):
        raise ProvenanceError("sidecar source dirty must be boolean")
    expected_state = "dirty" if source["dirty"] else "clean"
    if source.get("dirty_state") != expected_state:
        raise ProvenanceError("sidecar source dirty_state contradicts dirty")
    apk = data.get("apk")
    if not isinstance(apk, dict):
        raise ProvenanceError("sidecar APK facts are missing")
    if not isinstance(apk.get("file"), str) or not apk["file"] or \
            Path(apk["file"]).name != apk["file"]:
        raise ProvenanceError("sidecar APK file must be a bare filename")
    if not is_int(apk.get("size")) or apk["size"] <= 0:
        raise ProvenanceError("sidecar APK size is missing or malformed")
    if not isinstance(apk.get("sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", apk["sha256"]):
        raise ProvenanceError("sidecar APK sha256 is missing or malformed")
    if not is_int(apk.get("entry_count")) or apk["entry_count"] <= 0:
        raise ProvenanceError("sidecar APK entry_count is missing or malformed")
    entries = data.get("entries")
    if not isinstance(entries, list) or not entries:
        raise ProvenanceError("sidecar entries are missing or malformed")
    build = data.get("build")
    if not isinstance(build, dict):
        raise ProvenanceError("sidecar build facts are missing")
    abi = build.get("abi")
    if not isinstance(abi, str) or not (abi == "unknown" or re.fullmatch(r"[A-Za-z0-9_-]+", abi)):
        raise ProvenanceError("sidecar ABI is missing or malformed")
    for key in ("platform_api", "min_sdk", "target_sdk"):
        value = build.get(key)
        if value != "unknown" and (not is_int(value) or value <= 0):
            raise ProvenanceError(f"sidecar build {key} is missing or malformed")
    for key in ("build_tools", "cmake_build_type"):
        if not isinstance(build.get(key), str) or not build[key]:
            raise ProvenanceError(f"sidecar build {key} is missing or malformed")
    evidence = data.get("test_evidence")
    if not isinstance(evidence, dict):
        raise ProvenanceError("sidecar test_evidence is missing")
    status = evidence.get("status")
    reference = evidence.get("reference")
    if status not in ("unknown", "passed", "failed"):
        raise ProvenanceError("sidecar test_evidence status is malformed")
    if status == "unknown" and reference is not None:
        raise ProvenanceError("unknown test evidence must not carry a reference")
    if status != "unknown" and (not isinstance(reference, str) or not reference.strip()):
        raise ProvenanceError("explicit test evidence requires a reference")
    return data


def verify_sidecar(args: argparse.Namespace) -> None:
    apk = Path(args.apk)
    sidecar_path = Path(args.sidecar)
    if not apk.is_file() or not sidecar_path.is_file():
        raise ProvenanceError("APK or sidecar is missing")
    try:
        data = json.loads(sidecar_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ProvenanceError("sidecar is not valid JSON") from exc
    data = validate_sidecar_facts(data)
    apk_fact = data["apk"]
    entries = data["entries"]
    build = data["build"]
    assert isinstance(apk_fact, dict) and isinstance(entries, list) and isinstance(build, dict)
    if apk_fact["file"] != apk.name:
        raise ProvenanceError("sidecar APK filename does not match")
    if apk_fact["size"] != apk.stat().st_size:
        raise ProvenanceError("sidecar APK size does not match")
    if apk_fact["sha256"] != sha256_file(apk):
        raise ProvenanceError("sidecar APK SHA-256 does not match")
    abi = build["abi"]
    if not isinstance(abi, str):
        raise ProvenanceError("sidecar ABI is malformed")
    selected = select_entries(apk, abi)
    required_names = {name for name, _ in selected}
    by_name: dict[str, dict[str, object]] = {}
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
            raise ProvenanceError("sidecar has a malformed entry")
        name = entry["path"]
        if name.startswith("/") or ".." in name.split("/"):
            raise ProvenanceError("sidecar entry path is unsafe")
        if not is_int(entry.get("size")) or entry["size"] < 0:
            raise ProvenanceError(f"sidecar entry size is malformed: {name}")
        if not isinstance(entry.get("sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]):
            raise ProvenanceError(f"sidecar entry sha256 is malformed: {name}")
        if name in by_name:
            raise ProvenanceError("sidecar has duplicate entry paths")
        by_name[name] = entry
    for name in required_names:
        if name not in by_name:
            raise ProvenanceError(f"sidecar is missing required entry: {name}")
    with zipfile.ZipFile(apk) as archive:
        names = set(archive.namelist())
        if apk_fact["entry_count"] != len(names):
            raise ProvenanceError("sidecar APK entry_count does not match")
        for name, entry in by_name.items():
            if name not in names:
                raise ProvenanceError(f"APK is missing sidecar entry: {name}")
            payload = archive.read(name)
            if entry.get("size") != len(payload) or entry.get("sha256") != hashlib.sha256(payload).hexdigest():
                raise ProvenanceError(f"APK entry digest mismatch: {name}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    write = sub.add_parser("write", help="write a sidecar for a final signed APK")
    write.add_argument("--apk", required=True)
    write.add_argument("--output", required=True)
    write.add_argument("--source-root")
    write.add_argument("--source-commit")
    write.add_argument("--source-dirty", choices=("true", "false"))
    write.add_argument("--native-build")
    write.add_argument("--build-tools")
    write.add_argument("--min-sdk", type=int)
    write.add_argument("--target-sdk", type=int)
    write.add_argument("--test-status", default="unknown",
                       choices=("unknown", "passed", "failed"))
    write.add_argument("--test-reference")
    verify = sub.add_parser("verify", help="verify an APK against a sidecar")
    verify.add_argument("--apk", required=True)
    verify.add_argument("--sidecar", required=True)
    args = parser.parse_args()
    try:
        if args.command == "write":
            write_sidecar(args)
            print(f"provenance sidecar written: {Path(args.output).name}")
        else:
            verify_sidecar(args)
            print("provenance sidecar verified")
    except ProvenanceError as exc:
        print(f"provenance error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
