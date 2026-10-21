#!/usr/bin/env bash
# #253 Apple export regression: compile the real hle_svc.c and assert the
# diag.c dependency is present as a global symbol. No link/device execution.
set -euo pipefail
cc=$1
root=$2
object=$3
"$cc" -std=gnu11 -O2 -fPIC -D_GNU_SOURCE \
    -I "$root/dm/host" -I "$root/dm/gfx/nvn/include" \
    -c "$root/dm/host/hle_svc.c" -o "$object"
nm -g "$object" | grep -Eq '[[:space:]]T[[:space:]]+_?dm_guest_heap_readable$'
echo "PASS Apple hle_svc export: dm_guest_heap_readable"
