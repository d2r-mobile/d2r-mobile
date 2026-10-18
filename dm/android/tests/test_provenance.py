#!/usr/bin/env python3
"""Offline provenance sidecar tests with owned temporary zip fixtures."""
from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
HELPER = HERE.parent / "app" / "provenance.py"


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def make_zip(path: Path, native=b"native-arm64", fixture=b"owned-fixture",
             vert=b"vertex-shader", frag=b"fragment-shader") -> None:
    with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as archive:
        archive.writestr("AndroidManifest.xml", b"manifest")
        archive.writestr("lib/arm64-v8a/libdm_android_app.so", native)
        archive.writestr("assets/synthetic.dma", fixture)
        archive.writestr("assets/indexed_textured.vert.spv", vert)
        archive.writestr("assets/indexed_textured.frag.spv", frag)


def run(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(HELPER), *args],
                          text=True, capture_output=True, check=False)


class ProvenanceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.apk = self.root / "candidate.apk"
        make_zip(self.apk)
        self.cache = self.root / "native-build"
        self.cache.mkdir()
        (self.cache / "CMakeCache.txt").write_text(
            "ANDROID_ABI:UNINITIALIZED=arm64-v8a\n"
            "ANDROID_PLATFORM:UNINITIALIZED=android-29\n"
            "CMAKE_BUILD_TYPE:STRING=Release\n", encoding="utf-8")
        self.sidecar = self.root / "candidate.apk.provenance.json"

    def tearDown(self) -> None:
        self.temp.cleanup()

    def write(self, *, dirty="false", cache=None, output=None, apk=None):
        args = ["write", "--apk", str(apk or self.apk),
                "--output", str(output or self.sidecar),
                "--source-commit", "0123456789abcdef0123456789abcdef01234567",
                "--source-dirty", dirty,
                "--native-build", str(cache or self.cache),
                "--build-tools", str(self.root / "sdk" / "build-tools" / "35.0.0"),
                "--min-sdk", "29", "--target-sdk", "35"]
        return run(*args)

    def test_sidecar_binds_actual_apk_and_entries(self) -> None:
        result = self.write()
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(self.sidecar.read_text())
        self.assertEqual(data["schema"], "dm-diagnostic-apk-provenance/v1")
        self.assertEqual(data["kind"], "diagnostic-apk")
        self.assertTrue(data["diagnostic_only"])
        self.assertEqual(data["source"]["commit"], "0123456789abcdef0123456789abcdef01234567")
        self.assertEqual(data["source"]["dirty"], False)
        self.assertEqual(data["source"]["dirty_state"], "clean")
        self.assertEqual(data["apk"]["file"], self.apk.name)
        self.assertEqual(data["apk"]["size"], self.apk.stat().st_size)
        self.assertEqual(data["apk"]["sha256"], sha(self.apk.read_bytes()))
        self.assertEqual(data["build"]["abi"], "arm64-v8a")
        self.assertEqual(data["build"]["platform_api"], 29)
        self.assertEqual(data["build"]["min_sdk"], 29)
        self.assertEqual(data["build"]["target_sdk"], 35)
        self.assertEqual(data["build"]["build_tools"], "35.0.0")
        self.assertEqual(data["build"]["cmake_build_type"], "Release")
        self.assertEqual(data["test_evidence"], {"status": "unknown", "reference": None})
        entries = {e["path"]: e for e in data["entries"]}
        self.assertEqual(entries["lib/arm64-v8a/libdm_android_app.so"]["sha256"], sha(b"native-arm64"))
        self.assertEqual(entries["assets/synthetic.dma"]["sha256"], sha(b"owned-fixture"))
        self.assertEqual(entries["assets/indexed_textured.vert.spv"]["sha256"], sha(b"vertex-shader"))
        self.assertEqual(entries["assets/indexed_textured.frag.spv"]["sha256"], sha(b"fragment-shader"))
        raw = self.sidecar.read_text()
        self.assertNotIn(str(self.root), raw)       # no absolute personal path
        self.assertNotIn("keystore", raw.lower())
        self.assertNotIn("signing", raw.lower())
        verified = run("verify", "--apk", str(self.apk), "--sidecar", str(self.sidecar))
        self.assertEqual(verified.returncode, 0, verified.stderr)

    def test_dirty_source_never_claims_clean(self) -> None:
        result = self.write(dirty="true")
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(self.sidecar.read_text())
        self.assertTrue(data["source"]["dirty"])
        self.assertEqual(data["source"]["dirty_state"], "dirty")
        self.assertEqual(data["source"]["commit"], "0123456789abcdef0123456789abcdef01234567")
        verified = run("verify", "--apk", str(self.apk), "--sidecar", str(self.sidecar))
        self.assertEqual(verified.returncode, 0, verified.stderr)

    def test_modified_apk_rejected(self) -> None:
        self.assertEqual(self.write().returncode, 0)
        changed = self.root / "changed.apk"
        make_zip(changed, fixture=b"owned-fixturX")
        changed.replace(self.apk)  # same sidecar, new final bytes
        verified = run("verify", "--apk", str(self.apk), "--sidecar", str(self.sidecar))
        self.assertNotEqual(verified.returncode, 0)
        self.assertIn("SHA-256", verified.stderr)

    def test_modified_entry_rejected(self) -> None:
        self.assertEqual(self.write().returncode, 0)
        data = json.loads(self.sidecar.read_text())
        data["entries"][0]["sha256"] = "0" * 64
        self.sidecar.write_text(json.dumps(data))
        verified = run("verify", "--apk", str(self.apk), "--sidecar", str(self.sidecar))
        self.assertNotEqual(verified.returncode, 0)
        self.assertIn("digest mismatch", verified.stderr)

    def test_sidecar_missing_required_entry_rejected(self) -> None:
        self.assertEqual(self.write().returncode, 0)
        data = json.loads(self.sidecar.read_text())
        data["entries"] = [e for e in data["entries"] if e["path"] != "assets/synthetic.dma"]
        self.sidecar.write_text(json.dumps(data))
        verified = run("verify", "--apk", str(self.apk), "--sidecar", str(self.sidecar))
        self.assertNotEqual(verified.returncode, 0)
        self.assertIn("missing required entry", verified.stderr)

    def test_actual_source_root_reflects_git_truth(self) -> None:
        repo = HERE.parents[2]
        commit = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"],
                                          text=True).strip()
        dirty = bool(subprocess.check_output(
            ["git", "-C", str(repo), "status", "--porcelain", "--untracked-files=normal"],
            text=True).strip())
        result = run("write", "--apk", str(self.apk), "--output", str(self.sidecar),
                     "--source-root", str(repo),
                     "--native-build", str(self.cache),
                     "--build-tools", str(self.root / "sdk" / "build-tools" / "35.0.0"),
                     "--min-sdk", "29", "--target-sdk", "35")
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(self.sidecar.read_text())
        self.assertEqual(data["source"]["commit"], commit)
        self.assertEqual(data["source"]["dirty"], dirty)
        self.assertEqual(data["source"]["dirty_state"], "dirty" if dirty else "clean")

    def test_missing_inputs_and_malformed_sidecar(self) -> None:
        missing_output = self.root / "missing.provenance.json"
        result = self.write(apk=self.root / "missing.apk", output=missing_output)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(missing_output.exists())
        self.assertNotEqual(run("verify", "--apk", str(self.apk),
                                "--sidecar", str(self.root / "absent.json")).returncode, 0)
        bad = self.root / "bad.json"
        bad.write_text("{not-json")
        self.assertNotEqual(run("verify", "--apk", str(self.apk),
                                "--sidecar", str(bad)).returncode, 0)

    def test_required_provenance_identity_fields_rejected(self) -> None:
        cases = []
        def missing_source(data):
            data.pop("source")
        def missing_evidence(data):
            data.pop("test_evidence")
        def not_diagnostic(data):
            data["diagnostic_only"] = False
        def bad_short(data):
            data["source"]["commit_short"] = "000000000000"
        def dirty_contradiction(data):
            data["source"]["dirty"] = True
            data["source"]["dirty_state"] = "clean"
        def explicit_without_reference(data):
            data["test_evidence"]["status"] = "passed"
            data["test_evidence"]["reference"] = None
        def bad_build_type(data):
            data["build"]["abi"] = []
        cases = [missing_source, missing_evidence, not_diagnostic, bad_short,
                 dirty_contradiction, explicit_without_reference, bad_build_type]
        for mutate in cases:
            self.assertEqual(self.write().returncode, 0)
            data = json.loads(self.sidecar.read_text())
            mutate(data)
            self.sidecar.write_text(json.dumps(data))
            verified = run("verify", "--apk", str(self.apk), "--sidecar", str(self.sidecar))
            self.assertNotEqual(verified.returncode, 0, mutate.__name__)

    def test_non_object_root_rejected(self) -> None:
        self.sidecar.write_text("[]")
        verified = run("verify", "--apk", str(self.apk), "--sidecar", str(self.sidecar))
        self.assertNotEqual(verified.returncode, 0)
        self.assertIn("root is not an object", verified.stderr)

    def test_atomic_replace_failure_leaves_no_complete_output(self) -> None:
        output_dir = self.root / "published"
        output_dir.mkdir()
        result = self.write(output=output_dir)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("atomically publish", result.stderr)
        self.assertTrue(output_dir.is_dir())
        self.assertEqual(list(self.root.glob(".published.*")), [])  # temp removed

    def test_failed_validation_preserves_known_old_output(self) -> None:
        self.assertEqual(self.write().returncode, 0)
        before = self.sidecar.read_bytes()
        result = self.write(apk=self.root / "missing.apk")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.sidecar.read_bytes(), before)

    def test_unknown_cache_fields_are_explicit(self) -> None:
        result = self.write(cache=self.root / "no-cache")
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(self.sidecar.read_text())
        self.assertEqual(data["build"]["abi"], "unknown")
        self.assertEqual(data["build"]["platform_api"], "unknown")
        self.assertEqual(data["build"]["cmake_build_type"], "unknown")

    def test_duplicate_entries_are_rejected(self) -> None:
        duplicate = self.root / "duplicate.apk"
        with zipfile.ZipFile(duplicate, "w") as archive:
            archive.writestr("lib/arm64-v8a/libdm_android_app.so", b"a")
            archive.writestr("lib/arm64-v8a/libdm_android_app.so", b"b")
            archive.writestr("assets/synthetic.dma", b"fixture")
            archive.writestr("assets/indexed_textured.vert.spv", b"v")
            archive.writestr("assets/indexed_textured.frag.spv", b"f")
        result = self.write(apk=duplicate, output=self.root / "duplicate.json")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / "duplicate.json").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
