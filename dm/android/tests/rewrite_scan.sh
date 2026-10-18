#!/usr/bin/env bash
# 合成 NSO 审计／内存准备端到端测试；核对全窗口改写、拒绝非法字／分支和成功产物释放。
# End-to-end synthetic NSO audit / preparation; check whole-window rewriting, invalid-word / branch rejection and successful artifact disposal.
set -euo pipefail
scan=$1
fixture=$2
work=$(mktemp -d "${TMPDIR:-/tmp}/dm-rewrite-cli.XXXXXX")
trap 'rm -rf "$work"' EXIT
"$fixture" --fixture "$work/main"
if "$scan" "$work/main" > "$work/audit.log" 2>&1; then exit 1; else status=$?; fi
[[ $status == 3 ]]
grep -q 'system=1 x18_plain=1 x18_pcrel=1 x18_conditional=1 x18_jump=0 exclusive_windows=1 rewritten_windows=1 rewritten_window_sites=3 window_sites=0 unmatched_loads=0 deferred_pcrel=0 deferred_branch=1 deferred_other=0 undecodable_words=1 emitter_errors=0' "$work/audit.log"
if "$scan" --prepare-check "$work/main" > "$work/prepare.log" 2>&1; then exit 1; else status=$?; fi
[[ $status == 3 ]]
sed '$d' "$work/audit.log" > "$work/audit-counts"
sed '$d' "$work/prepare.log" > "$work/prepare-counts"
cmp "$work/audit-counts" "$work/prepare-counts"
"$fixture" --safe-fixture "$work/safe"
"$scan" --prepare-check "$work/safe" > "$work/safe.log" 2>&1
grep -q 'undecodable_words=0 emitter_errors=0 unsupported_system=0' "$work/safe.log"
grep -q 'Preparation check only: discard in-memory output' "$work/safe.log"
"$scan" --export "$work/safe.dma" "$work/safe" > "$work/export.log" 2>&1
[[ -s "$work/safe.dma" ]]
if "$scan" --export "$work/safe.dma" "$work/safe" > "$work/existing.log" 2>&1; then exit 1; else status=$?; fi
[[ $status == 1 ]]
if "$scan" --export "$work/blocked.dma" "$work/main" > "$work/blocked.log" 2>&1; then exit 1; else status=$?; fi
[[ $status == 3 && ! -e "$work/blocked.dma" ]]
if "$scan" "$work/missing" > "$work/missing.log" 2>&1; then exit 1; else status=$?; fi
[[ $status == 1 ]]
if "$scan" > "$work/usage.log" 2>&1; then exit 1; else status=$?; fi
[[ $status == 2 ]]
if "$scan" --prepare-check > "$work/usage-prepare.log" 2>&1; then exit 1; else status=$?; fi
[[ $status == 2 ]]
echo 'PASS: shared audit/preparation ordinary/system/window/PC-relative/branch/invalid/missing/usage paths'
