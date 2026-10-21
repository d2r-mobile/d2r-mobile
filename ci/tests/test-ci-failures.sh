#!/usr/bin/env bash
# Failure-boundary tests for ci.sh: configure failure preserves its log, and a
# failed manifest generator must fail the CI entry point.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/dm-ci-failure.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/bin-cmake" "$work/bin-manifest"
cat >"$work/bin-cmake/cmake" <<'EOF'
#!/usr/bin/env bash
echo 'configure-fail synthetic' >&2
exit 1
EOF
chmod +x "$work/bin-cmake/cmake"
PATH="$work/bin-cmake:$PATH" DM_CI_WORK="$work/first" \
    DM_CI_BUILD_DIR="$work/first/build" TMPDIR="$work/tmp" \
    bash "$root/ci/scripts/ci.sh" host >"$work/first.out" 2>&1 && exit 1
grep -q 'configure-fail synthetic' "$work/first/artifacts/host-ctest.log"
[[ -s "$work/first/artifacts/manifest.json" ]]

cat >"$work/bin-manifest/python3" <<'EOF'
#!/usr/bin/env bash
case "${1:-}" in
  */manifest.py) exit 1;;
  *) exec /usr/bin/python3 "$@";;
esac
EOF
chmod +x "$work/bin-manifest/python3"
PATH="$work/bin-manifest:$PATH" DM_CI_WORK="$work/second" \
    DM_CI_BUILD_DIR="$work/second/build" TMPDIR="$work/tmp" \
    bash "$root/ci/scripts/ci.sh" host >"$work/second.out" 2>&1 && exit 1
[[ ! -s "$work/second/artifacts/manifest.json" ]]
grep -q 'manifest not produced' "$work/second.out"
echo 'PASS: ci.sh fails on configure error and manifest-generator failure with logs'
