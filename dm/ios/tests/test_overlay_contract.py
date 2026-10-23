#!/usr/bin/env python3
"""Synthetic contract regressions: no proprietary assets or device required."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import overlay_contract as contract


class OverlayContractTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source_sha = "a" * 64

    def overlay(self, name="overlay", reversed_order=False):
        root = self.root / name
        root.mkdir()
        files = [("ctrl_touch", b"touch layouts\n"), ("touch_strings.tsv", b"key\t12345678\tTouch\n"),
                 ("data/.build.config", b"synthetic config\n"), ("data/00-00000042.data", b"\0synthetic archive\xff"),
                 ("features.txt", b"item_notes\n"), ("touchart/joy.png", b"synthetic image bytes")]
        for relative, content in reversed(files) if reversed_order else files:
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
        return root

    def source(self, name, newline):
        root = self.root / name
        root.mkdir()
        for relative in contract.SOURCE_INPUTS:
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(("# public source: " + relative + "\ntext = '中文'\n").replace("\n", newline).encode("utf-8"))
        return root

    def test_manifest_deterministic_inventory_and_content(self):
        first, second = self.overlay("first"), self.overlay("second", True)
        left = contract.write_manifest(first, self.source_sha).read_bytes()
        right = contract.write_manifest(second, self.source_sha).read_bytes()
        self.assertEqual(left, right)
        self.assertNotIn(b"\r", left)
        self.assertTrue(left.startswith(b"DM_OVERLAY_V1 " + b"a" * 64 + b"\n"))
        entries = contract.verify_manifest(first, self.source_sha)
        self.assertEqual(list(entries), sorted(entries))
        self.assertEqual(len(entries), 6)
        self.assertNotIn(contract.MANIFEST, entries)
        self.assertEqual(entries["data/00-00000042.data"], hashlib.sha256(b"\0synthetic archive\xff").hexdigest())

    def test_source_crlf_lf_same_and_changed_source_different(self):
        unix, windows = self.source("unix", "\n"), self.source("windows", "\r\n")
        expected = contract.source_fingerprint(unix)
        self.assertEqual(expected, contract.source_fingerprint(windows))
        dependency = windows / "dm/ios/tools/touchrectv1_patch.json"
        dependency.write_bytes(dependency.read_bytes() + b"changed\r\n")
        self.assertNotEqual(expected, contract.source_fingerprint(windows))

    def test_private_and_generated_files_not_fingerprinted(self):
        root = self.source("repo", "\n")
        expected = contract.source_fingerprint(root)
        (root / "private-romfs.data").write_bytes(b"private input not opened")
        generated = root / "dm/ios/Gen/assets"
        generated.mkdir(parents=True)
        (generated / "cache.bin").write_bytes(b"generated result")
        self.assertEqual(expected, contract.source_fingerprint(root))

    def test_missing_source_dependency_fails(self):
        root = self.source("repo", "\n")
        (root / "dm/ios/tools/gen_assets.py").unlink()
        with self.assertRaises(FileNotFoundError):
            contract.source_fingerprint(root)

    def test_header_matches_fingerprint_and_is_stable(self):
        root = self.source("repo", "\n")
        expected = contract.source_fingerprint(root)
        header = self.root / "generated/header.h"
        contract.write_header(header, expected)
        before = header.stat().st_mtime_ns
        contract.write_header(header, expected)
        self.assertEqual(before, header.stat().st_mtime_ns)
        self.assertIn(f'#define DM_OVERLAY_SOURCE_SHA256 "{expected}"', header.read_text())

    def test_legacy_overlay_is_not_repaired(self):
        root = self.overlay()
        with self.assertRaises(FileNotFoundError):
            contract.verify_manifest(root, self.source_sha)
        self.assertFalse((root / contract.MANIFEST).exists())

    def test_missing_required_files_each_fail(self):
        for index, relative in enumerate([*sorted(contract.REQUIRED), "data/00-00000042.data"]):
            with self.subTest(relative=relative):
                root = self.overlay("missing" + str(index))
                (root / relative).unlink()
                with self.assertRaises(contract.ContractError):
                    contract.write_manifest(root, self.source_sha)
                self.assertFalse((root / contract.MANIFEST).exists())

    def test_empty_required_file_rejected(self):
        root = self.overlay()
        (root / "ctrl_touch").write_bytes(b"")
        with self.assertRaises(contract.ContractError):
            contract.write_manifest(root, self.source_sha)

    def test_content_fingerprint_and_extra_file_fail(self):
        root = self.overlay()
        contract.write_manifest(root, self.source_sha)
        with self.assertRaises(contract.ContractError):
            contract.verify_manifest(root, "b" * 64)
        (root / "ctrl_touch").write_bytes(b"changed")
        with self.assertRaises(contract.ContractError):
            contract.verify_manifest(root, self.source_sha)
        contract.write_manifest(root, self.source_sha)
        (root / "unlisted.bin").write_bytes(b"extra")
        with self.assertRaises(contract.ContractError):
            contract.verify_manifest(root, self.source_sha)

    def test_malformed_duplicate_unsafe_and_unordered_rows_fail(self):
        good = contract.render_manifest(self.overlay(), self.source_sha)
        header, *rows = good.splitlines(keepends=True)
        corruptions = [good[:-1], good.replace(b"\n", b"\r\n"), header + b"\n" + b"".join(rows),
                       header + rows[0] + b"".join(rows), header + b"".join(reversed(rows)),
                       header + b"g" * 64 + b" unsafe\n" + b"".join(rows)]
        for bad in ("../outside", "/absolute", "C:/drive", "data//empty", "data/./alias", "data/../alias",
                    "data\\windows", "data/bad name", "data/NUL", "data/trailing.", contract.MANIFEST):
            corruptions.append(header + b"0" * 64 + b" " + bad.encode() + b"\n" + b"".join(rows))
        for index, data in enumerate(corruptions):
            with self.subTest(index=index), self.assertRaises(contract.ContractError):
                contract.parse_manifest(data, self.source_sha)

    def test_case_ambiguous_manifest_rejected(self):
        good = contract.render_manifest(self.overlay(), self.source_sha)
        header, *rows = good.splitlines(keepends=True)
        rows.append(b"a" * 64 + b" CTRL_TOUCH\n")
        rows.sort(key=lambda value: value[65:])
        with self.assertRaises(contract.ContractError):
            contract.parse_manifest(header + b"".join(rows), self.source_sha)

    def test_symlink_file_and_directory_rejected(self):
        root = self.overlay()
        outside = self.root / "outside"
        outside.mkdir()
        (outside / "secret.bin").write_bytes(b"must not follow")
        try:
            (root / "link").symlink_to(outside, target_is_directory=True)
        except OSError as error:
            self.skipTest(f"symlink creation unavailable on this host: {error}")
        with self.assertRaises(contract.ContractError):
            contract.render_manifest(root, self.source_sha)
        (root / "link").unlink()
        (root / "link.bin").symlink_to(outside / "secret.bin")
        with self.assertRaises(contract.ContractError):
            contract.render_manifest(root, self.source_sha)

    def test_manifest_symlink_cannot_overwrite_outside(self):
        root = self.overlay()
        outside = self.root / "unchanged.txt"
        outside.write_bytes(b"do not overwrite")
        try:
            (root / contract.MANIFEST).symlink_to(outside)
        except OSError as error:
            self.skipTest(f"symlink creation unavailable on this host: {error}")
        with self.assertRaises(contract.ContractError):
            contract.write_manifest(root, self.source_sha)
        self.assertEqual(outside.read_bytes(), b"do not overwrite")

    def test_streamed_payload_larger_than_chunk(self):
        root = self.overlay()
        data = (bytes(range(256)) * 8193)
        (root / "data/00-00000042.data").write_bytes(data)
        contract.write_manifest(root, self.source_sha)
        entries = contract.verify_manifest(root, self.source_sha)
        self.assertEqual(entries["data/00-00000042.data"], hashlib.sha256(data).hexdigest())

    def test_limits_fail_without_reading_large_payload(self):
        with self.assertRaises(contract.ContractError):
            contract.valid_relative_path("a" * (contract.MAX_PATH + 1))
        with self.assertRaises(contract.ContractError):
            contract.valid_relative_path("/".join(["a"] * (contract.MAX_DEPTH + 1)))
        with self.assertRaises(contract.ContractError):
            contract.parse_manifest(b"x" * (contract.MAX_MANIFEST + 1), self.source_sha)


if __name__ == "__main__":
    unittest.main(verbosity=2)
