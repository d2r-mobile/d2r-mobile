#!/usr/bin/env bash
# 最终共享库必须直接跳到不可抢占的桥入口；PLT 会破坏携带导入 ID 的 x17。
# Final shared-library veneers must branch directly to a non-preemptible bridge entry; PLT dispatch clobbers the import ID in x17.
set -euo pipefail
if [[ $# -ne 3 ]]; then
    echo "usage: bridge_shared.sh <llvm-readelf> <llvm-objdump> <android-shared-library>" >&2
    exit 2
fi
readelf=$1
objdump=$2
library=$3
symbols=$("$readelf" --symbols --wide "$library")
for entry in dm_bridge_import_entry unimpl_entry dm_bridge_fatal_entry; do
if ! printf '%s\n' "$symbols" | awk -v entry="$entry" '
    $NF == entry && $7 != "UND" {
        found = 1
        if ($5 != "LOCAL" && $6 != "HIDDEN") bad = 1
    }
    END { exit !(found && !bad) }
'; then
    echo "FAIL: $entry must be defined and local or hidden" >&2
    exit 1
fi
done
relocations=$("$readelf" --relocs --wide "$library")
if printf '%s\n' "$relocations" | awk '
    /R_AARCH64_JUMP_SLOT/ && /dm_bridge_import_entry|unimpl_entry|dm_bridge_fatal_entry/ { found = 1 }
    END { exit !found }
'; then
    echo "FAIL: a bridge/fatal entry has a dynamic JUMP_SLOT relocation" >&2
    exit 1
fi
disassembly=$("$objdump" -d --disassemble-symbols=dm_bridge_romfs_query,dm_bridge_romfs_mount,dm_bridge_romfs_open,dm_bridge_romfs_read,dm_bridge_romfs_size,dm_bridge_romfs_close "$library")
for name in query mount open read size close; do
    if ! printf '%s\n' "$disassembly" | awk -v symbol="dm_bridge_romfs_$name" '
        $0 ~ "<" symbol ">:" { inside = 1; found = 1; next }
        /^[[:space:]]*[[:xdigit:]]+ <.*>:/ { inside = 0 }
        inside && /[[:space:]]b[[:space:]].*<dm_bridge_import_entry>/ { direct++ }
        inside && /@plt/ { bad = 1 }
        END { exit !(found && direct == 1 && !bad) }
    '; then
        echo "FAIL: dm_bridge_romfs_$name must branch directly to dm_bridge_import_entry" >&2
        exit 1
    fi
done
echo "PASS: all six Android shared RomFS veneers bind directly to the local bridge entry"

# 未解析导入与旧式返回也必须直接陷阱，不能在客体栈进入动态链接器或 libc。
# Unresolved imports and legacy returns must trap directly, never enter the dynamic linker or libc on a guest stack.
fatal_disassembly=$("$objdump" -d --disassemble-symbols=unimpl_stubs,unimpl_entry,dm_thread_enter,dm_bridge_fatal_entry "$library")
if ! printf '%s\n' "$fatal_disassembly" | awk '
    /<unimpl_stubs>:/ { scope=1; found_stubs=1; next }
    /<unimpl_entry>:/ { scope=2; found_entry=1; next }
    /<dm_thread_enter>:/ { scope=3; found_return=1; next }
    /<dm_bridge_fatal_entry>:/ { scope=4; found_fatal=1; next }
    /^[[:space:]]*[[:xdigit:]]+ <.*>:/ { scope=0 }
    scope && /@plt/ { bad=1 }
    scope==1 && /[[:space:]]b[[:space:]].*<unimpl_entry>/ { direct_stubs++ }
    (scope==2 || scope==3) && /[[:space:]]bl[[:space:]].*<dm_bridge_fatal_entry>/ { direct_fatal[scope]++ }
    (scope==1 || scope==4) && /[[:space:]]blr?[[:space:]]/ { bad=1 }
    (scope==2 || scope==3) && /[[:space:]]blr?[[:space:]]/ && !/<dm_bridge_fatal_entry>/ { bad=1 }
    scope==4 && /^[[:space:]]*[[:xdigit:]]+:/ {
        if (!fatal_instruction++ && !/[[:space:]]brk[[:space:]]/) bad=1
    }
    END { exit !(found_stubs && found_entry && found_return && found_fatal && direct_stubs==4096 && direct_fatal[2]==1 && direct_fatal[3]==1 && fatal_instruction && !bad) }
'; then
    echo "FAIL: unresolved/returned paths must bind directly to the bounded assembly trap" >&2
    exit 1
fi
echo "PASS: 4096 unresolved stubs and legacy return bind directly to the assembly trap, without PLT/libc calls"
