#!/usr/bin/env bash
# Boundary test for elf_pages.py using owned files and a fake readelf.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/dm-elf-pages.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/bin" "$work/good" "$work/bad" "$work/zero" "$work/congruence" "$work/empty"
printf '\177ELF' >"$work/good/a"; printf '\177ELF' >"$work/bad/a"; printf '\177ELF' >"$work/zero/a"
printf '\177ELF' >"$work/congruence/a"; printf '\177ELF' >"$work/empty/a"
chmod +x "$work"/{good,bad,zero,congruence,empty}/a
cat >"$work/bin/readelf" <<'EOF'
#!/usr/bin/env bash
case "$2" in
  */good/*) echo 'LOAD 0x000000 0x000000 0x000000 0x100 0x100 R E 0x4000';;
  */bad/*) echo 'LOAD 0x000000 0x000000 0x000000 0x100 0x100 R E 0x3000';;
  */zero/*) echo 'LOAD 0x000000 0x000000 0x000000 0x100 0x100 R E 0x0';;
  */congruence/*) echo 'LOAD 0x010000 0x020000 0x020000 0x100 0x100 R E 0x1000';;
  *) echo 'NOTE not a load';;
esac
EOF
chmod +x "$work/bin/readelf"
python3 "$root/ci/scripts/elf_pages.py" "$work/bin/readelf" "$work/good" >/dev/null
set +e
python3 "$root/ci/scripts/elf_pages.py" "$work/bin/readelf" "$work/bad" >/dev/null 2>&1
bad=$?
python3 "$root/ci/scripts/elf_pages.py" "$work/bin/readelf" "$work/zero" >/dev/null 2>&1
zero=$?
python3 "$root/ci/scripts/elf_pages.py" "$work/bin/readelf" "$work/congruence" >/dev/null 2>&1
congruence=$?
python3 "$root/ci/scripts/elf_pages.py" "$work/bin/readelf" "$work/empty" >/dev/null 2>&1
empty=$?
set -e
[[ $bad -ne 0 && $zero -ne 0 && $congruence -ne 0 && $empty -ne 0 ]]
echo 'PASS: ELF page gate rejects zero/non-power2/congruence/empty cases'
