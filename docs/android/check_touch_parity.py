#!/usr/bin/env python3
"""Check the issue-31 touch parity matrix for bounded structural errors.

This is deliberately a consistency checker, not a game/runtime test. It checks:
- all required issues #32..#40 are referenced;
- all required feature families are present;
- every markdown table row has the same width as its header;
- every explicit `dm/...:line[-line][,line[-line]...]` anchor is well formed,
  its file exists, and each line range is within the file;
- the freeze commit and fork head are recorded;
- the source hash table matches the frozen git blobs when `--verify-frozen` is on.

It does not validate symbols, gesture behavior, Android execution or game
acceptance. A passing run is only a documentation/structural check.

Usage:
    python3 docs/android/check_touch_parity.py
    python3 docs/android/check_touch_parity.py --matrix some/other.md
    python3 docs/android/check_touch_parity.py --no-verify-frozen
"""
import argparse
import hashlib
import pathlib
import re
import subprocess
import sys

REQUIRED_ISSUES = tuple(range(32, 41))
REQUIRED_TERMS = (
    "world", "joystick", "aim", "skill set", "zl", "pulse", "cancel",
    "belt", "mercenary", "inventory", "double", "long", "drag", "shop",
    "stash", "cube", "custom", "locked", "difficulty", "scroll", "slider",
    "ime", "chinese", "safe area", "hud", "handoff", "prefs",
)
FROZEN_COMMIT = "0d397fddedbc199e05d3e9d8c63de40984109a80"
FORK_HEAD = "c91c83c060aac2ddf2989128497451c45feb575e"
ANCHOR_TOKEN_RE = re.compile(r"`(dm/[^`\s:]+\.(?:c|h|m|py|json|md)):([^`]*)`")
RANGE_LIST_RE = re.compile(r"^\d+(?:-\d+)?(?:,\d+(?:-\d+)?)*$")
HASH_ROW_RE = re.compile(r"^\| `(dm/[^`]+)` \| `([0-9a-f]{64})` \|$", re.MULTILINE)


def _table_errors(text):
    errors = []
    width = None
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line.startswith("|"):
            width = None
            continue
        cells = [c for c in re.split(r"(?<!\\)\|", line)[1:-1]]
        if width is None:
            width = len(cells)
            if width < 2:
                errors.append(f"line {lineno}: table header has too few columns")
        elif len(cells) != width:
            errors.append(f"line {lineno}: table row has {len(cells)} columns, expected {width}")
    return errors


def _anchor_errors(text, root):
    errors = []
    seen = set()
    expressions = 0
    ranges = 0
    line_counts = {}
    for match in ANCHOR_TOKEN_RE.finditer(text):
        rel, spec = match.group(1), match.group(2)
        if not spec[:1].isdigit():
            # Function/symbol references such as `t_touch_controls.c:world_tap`
            # are test names, not line anchors. They are intentionally not
            # validated as line ranges.
            continue
        if not RANGE_LIST_RE.fullmatch(spec):
            errors.append(f"malformed range anchor: {rel}:{spec}")
            continue
        expressions += 1
        path = root / rel
        if not path.is_file():
            errors.append(f"anchor file missing: {rel}")
            continue
        if rel not in line_counts:
            try:
                line_counts[rel] = sum(1 for _ in path.open(encoding="utf-8", errors="ignore"))
            except OSError as exc:
                errors.append(f"anchor file unreadable: {rel}: {exc}")
                continue
        total = line_counts[rel]
        for part in spec.split(","):
            if "-" in part:
                first, last = (int(v) for v in part.split("-", 1))
            else:
                first = last = int(part)
            key = (rel, first, last)
            if key in seen:
                continue
            seen.add(key)
            ranges += 1
            if first < 1 or last < first or last > total:
                errors.append(
                    f"anchor out of range: {rel}:{first}-{last} (file has {total} lines)"
                )
    return errors, expressions, ranges


def _frozen_hash_errors(text, root):
    errors = []
    rows = HASH_ROW_RE.findall(text)
    if not rows:
        return ["source hash table missing or malformed"], 0
    checked = 0
    for rel, expected in rows:
        try:
            proc = subprocess.run(
                ["git", "-C", str(root), "show", f"{FROZEN_COMMIT}:{rel}"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
        except OSError as exc:
            errors.append(f"frozen hash check could not run: {rel}: {exc}")
            continue
        if proc.returncode != 0:
            detail = proc.stderr.decode("utf-8", "replace").strip().splitlines()
            errors.append(
                f"frozen blob unavailable: {rel}"
                + (f" ({detail[-1]})" if detail else "")
            )
            continue
        checked += 1
        actual = hashlib.sha256(proc.stdout).hexdigest()
        if actual != expected:
            errors.append(f"source hash mismatch: {rel}: expected {expected}, got {actual}")
    return errors, checked


def check(matrix, root, verify_frozen=True):
    text = matrix.read_text(encoding="utf-8")
    errors = []
    if FROZEN_COMMIT not in text:
        errors.append("missing frozen upstream commit")
    if FORK_HEAD not in text:
        errors.append("missing current fork head")
    for issue in REQUIRED_ISSUES:
        if f"#{issue}" not in text:
            errors.append(f"missing issue #{issue}")
    lower = text.lower()
    for term in REQUIRED_TERMS:
        if term not in lower:
            errors.append(f"missing feature term: {term}")
    errors.extend(_table_errors(text))

    anchor_errors, expressions, ranges = _anchor_errors(text, root)
    errors.extend(anchor_errors)
    hashes = 0
    if verify_frozen:
        hash_errors, hashes = _frozen_hash_errors(text, root)
        errors.extend(hash_errors)
    return errors, expressions, ranges, hashes


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--matrix", type=pathlib.Path,
                        default=pathlib.Path(__file__).with_name("touch-parity.md"))
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path("."))
    parser.add_argument("--verify-frozen", action=argparse.BooleanOptionalAction, default=True,
                        help="compare the source hash table with the frozen git blobs")
    args = parser.parse_args(argv)
    try:
        errors, expressions, ranges, hashes = check(args.matrix, args.root, args.verify_frozen)
    except OSError as exc:
        print(f"FAIL: cannot read matrix {args.matrix}: {exc}")
        return 2
    if errors:
        for error in errors:
            print(f"FAIL: {error}")
        print(f"{len(errors)} error(s); {ranges} source ranges in {expressions} anchor expressions checked; {hashes} frozen hashes checked")
        return 1
    print(f"OK: matrix structure, required coverage, {ranges} source ranges "
          f"in {expressions} anchor expressions, and {hashes} frozen hashes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
