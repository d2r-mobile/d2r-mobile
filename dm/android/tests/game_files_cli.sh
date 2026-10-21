#!/usr/bin/env bash
# 文件预检负例：空目录和错误版本都必须失败，不能因文件存在就放行。
# File preflight negative cases: empty directories and wrong versions must fail, never accept existence alone.
set -euo pipefail
check=$1
work=$(mktemp -d "${TMPDIR:-/tmp}/dm-files-cli.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/ExeFS" "$work/RomFS/data"
if bash "$check" --exefs-only "$work" > "$work/empty.log" 2>&1; then exit 1; fi
for name in main sdk subsdk0; do printf 'synthetic incorrect version\n' > "$work/ExeFS/$name"; done
if bash "$check" --exefs-only "$work" > "$work/wrong.log" 2>&1; then exit 1; fi
if bash "$check" "$work" > "$work/full.log" 2>&1; then exit 1; fi
set +e
bash "$check" --unknown > "$work/usage.log" 2>&1
status=$?
set -e
test "$status" -eq 2
echo 'PASS: file preflight rejects absent files, wrong program hashes and invalid options'
