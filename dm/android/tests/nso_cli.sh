#!/usr/bin/env bash
# NSO 命令行真实流程：自造输入成功，缺失与截断文件失败；不含游戏内容。
# Real NSO CLI flows: synthetic input succeeds, missing / truncated files fail; contains no game content.
set -euo pipefail
inspect=$1
fixture=$2
work=$(mktemp -d "${TMPDIR:-/tmp}/dm-nso-cli.XXXXXX")
trap 'rm -rf "$work"' EXIT
"$fixture" --fixture "$work/main"
"$inspect" "$work/main" > "$work/valid.log"
grep -q 'sha256=verified' "$work/valid.log"
head -c 200 "$work/main" > "$work/truncated"
if "$inspect" "$work/truncated" > "$work/truncated.log" 2>&1; then exit 1; fi
if "$inspect" "$work/missing" > "$work/missing.log" 2>&1; then exit 1; fi
if "$inspect" > "$work/usage.log" 2>&1; then exit 1; fi
if "$inspect" "$work/main" "$work/missing" > "$work/mixed.log" 2>&1; then exit 1; fi
echo 'PASS: NSO CLI valid / truncated / missing / usage / mixed inputs'
