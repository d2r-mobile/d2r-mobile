#!/usr/bin/env bash
# Control-flow test for ndk-r29.sh using owned stubs; never downloads the NDK.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/dm-ndk-stub.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/bin" "$work/fake/android-ndk-r29" "$work/fakezip"
cat >"$work/fake/android-ndk-r29/source.properties" <<'EOF'
Pkg.Revision = 29.0.14206865
EOF
(cd "$work/fake" && zip -qr "$work/fake.zip" android-ndk-r29)
cat >"$work/bin/curl" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
out=
while [[ $# -gt 0 ]]; do
    case "$1" in
        --output) out=$2; shift 2;;
        *) shift;;
    esac
done
[[ -n "$out" ]]
cp "$DM_CI_FAKE_ZIP" "$out"
EOF
cat >"$work/bin/sha1sum" <<'EOF'
#!/usr/bin/env bash
cat >/dev/null
printf 'android-ndk-r29-linux.zip: OK\n'
EOF
chmod +x "$work/bin/curl" "$work/bin/sha1sum"
export PATH="$work/bin:$PATH"
export DM_CI_FAKE_ZIP="$work/fake.zip"
export DM_CI_WORK="$work/work"
export DM_CI_NDK="$work/ndk"
out=$(bash "$root/ci/scripts/ndk-r29.sh" 2>"$work/stderr")
[[ "$out" == "$DM_CI_NDK" ]]
[[ "$(printf '%s\n' "$out" | wc -l | tr -d ' ')" == 1 ]]
! grep -q 'OK' <<<"$out"
out2=$(bash "$root/ci/scripts/ndk-r29.sh" 2>"$work/stderr2")
[[ "$out2" == "$DM_CI_NDK" ]]
[[ "$(printf '%s\n' "$out2" | wc -l | tr -d ' ')" == 1 ]]
echo 'PASS: ndk-r29 cold/cached control flow keeps stdout path-only'
