#!/usr/bin/env bash
# 只读检查 Android 移植所需文件；版本摘要与最低资源清单取自仓库 setup.sh。
# Read-only check of files needed by the Android port; version digests and minimum resource list come from setup.sh.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
source "$here/paths.sh"
exefs_only=0
if [[ ${1:-} == --exefs-only ]]; then exefs_only=1; shift; fi
if [[ $# -gt 1 || ${1:-} == --* ]]; then
    echo 'Usage: check_game_files.sh [--exefs-only] [Update-directory]' >&2; exit 2
fi
game=${1:-$DM_ROOT/data/switch/3.3.94012/Update}
if [[ ! -d "$game" ]]; then printf 'Missing directory: %s\n' "$game" >&2; exit 1; fi
game=$(cd "$game" && pwd)
if command -v shasum >/dev/null; then hash=(shasum -a 256)
elif command -v sha256sum >/dev/null; then hash=(sha256sum)
else echo 'Install shasum or sha256sum to verify program files.' >&2; exit 2; fi
missing=0
while read -r name expected; do
    path=$game/ExeFS/$name
    if [[ ! -s "$path" ]]; then
        printf 'MISSING ExeFS/%s\n' "$name"; missing=$((missing + 1)); continue
    fi
    if ! actual=$("${hash[@]}" "$path"); then
        printf 'UNREADABLE ExeFS/%s\n' "$name"; missing=$((missing + 1)); continue
    fi
    if [[ ${actual%% *} == "$expected" ]]; then printf 'OK ExeFS/%s SHA-256\n' "$name"
    else printf 'WRONG VERSION ExeFS/%s\n' "$name"; missing=$((missing + 1)); fi
done <<'HASHES'
main 55fd909bb7230b002357e5b0d8499c11f06888f345c49e69eb4734df44a27d0e
sdk 1c9f198a91c0bcfee5cf8f5b53fb941895e91334dfb766ce79fc68576797f017
subsdk0 67a38fcf89cd13e040a021eb3d157072f38ebf6680a907874c35c7b231b5d4e2
HASHES
if [[ $exefs_only == 0 ]]; then
    for name in build_manifest.psv \
        00-00000001.data 00-00000002.data 00-00000003.data 00-00000004.data 00-00000005.data 00-00000006.data \
        01-00000001.data 01-00000002.data 02-00000001.data 02-00000002.data \
        03-00000001.data 03-00000002.data 04-00000001.data 04-00000002.data \
        05-00000001.data 05-00000002.data 17-00000001.data 17-00000002.data; do
        if [[ ! -s "$game/RomFS/data/$name" ]]; then
            printf 'MISSING RomFS/data/%s\n' "$name"; missing=$((missing + 1))
        fi
    done
fi
if [[ $missing != 0 ]]; then printf 'FAIL: %s missing, unreadable or wrong-version file(s) under %s\n' "$missing" "$game"; exit 1; fi
echo 'PASS: program hashes match 3.3.94012; requested files are present. RomFS contents are not hash-verified.'
