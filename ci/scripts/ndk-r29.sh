#!/usr/bin/env bash
# Fetch the pinned Android NDK r29 Linux package for x86_64 CI runners.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
work=${DM_CI_WORK:-${RUNNER_TEMP:-/tmp}/dm-ci}
dest=${DM_CI_NDK:-$work/android-ndk-r29}
sha1=87e2bb7e9be5d6a1c6cdf5ec40dd4e0c6d07c30b
url=https://dl.google.com/android/repository/android-ndk-r29-linux.zip
if [[ -f "$dest/source.properties" ]] &&
   grep -q '^Pkg.Revision = 29\.0\.14206865$' "$dest/source.properties"; then
    printf '%s\n' "$dest"
    exit 0
fi
mkdir -p "$work"
zip=$work/android-ndk-r29-linux.zip
if [[ ! -s "$zip" ]]; then
    curl --fail --location --retry 3 --output "$zip" "$url"
fi
printf '%s  %s\n' "$sha1" "$zip" | sha1sum -c - >&2
tmp=$work/ndk-extract
rm -rf "$tmp" "$dest"
mkdir -p "$tmp"
unzip -q "$zip" -d "$tmp"
src=$(find "$tmp" -maxdepth 1 -type d -name 'android-ndk-r29*' | head -1)
[[ -n "$src" ]]
mv "$src" "$dest"
grep -q '^Pkg.Revision = 29\.0\.14206865$' "$dest/source.properties" >/dev/null
printf '%s\n' "$dest"
