#!/bin/sh
# SPDX-License-Identifier: MPL-2.0

set -eu

repo_root=$(cd "$(dirname "$0")/.." && pwd)
installer="$repo_root/packages/ssh-bootstrap/update.sh"
work_dir=$(mktemp -d)
cleanup() {
    rm -rf "$work_dir"
}
trap cleanup EXIT

python_body="$work_dir/patch.py"
awk '
    index($0, "DENEB_PATCH_PREFLIGHT=") && index($0, "python3 - <<") { capture = 1; next }
    capture && $0 == "PY" { exit }
    capture { print }
' "$installer" > "$python_body"

[ -s "$python_body" ]
DENEB_PATCH_SELFTEST=1 python3 "$python_body" |
    grep -Fx 'SSH bootstrap patch helper self-test: PASS'

preflight_line=$(grep -n '^install_deneb_update_lane preflight$' "$installer" | cut -d: -f1)
apply_line=$(grep -n '^install_deneb_update_lane apply$' "$installer" | cut -d: -f1)
credential_line=$(grep -n '^set_shadow_hash root ' "$installer" | cut -d: -f1)
[ -n "$preflight_line" ]
[ -n "$apply_line" ]
[ -n "$credential_line" ]
[ "$preflight_line" -lt "$apply_line" ]
[ "$apply_line" -lt "$credential_line" ]

# GETTING_STARTED and the public getting-started page share the bootstrap
# host-package and locked-Pillow contract. README must not keep a second apt
# recipe that can omit ca-certificates.
bootstrap_docs="$repo_root/docs/GETTING_STARTED.md
$repo_root/website/content/docs/getting-started.md"
bootstrap_markers='ca-certificates python3 python3-venv tar
--require-hashes -r tools/bootstrap-requirements.txt
DENEB_BOOTSTRAP_PYTHON
The builder must see that exact interpreter through `DENEB_BOOTSTRAP_PYTHON`.
System Python is not the locked bootstrap environment.'
while IFS= read -r doc; do
    [ -n "$doc" ] || continue
    [ -f "$doc" ] || {
        echo "Missing bootstrap contract doc: $doc" >&2
        exit 1
    }
    while IFS= read -r marker; do
        [ -n "$marker" ] || continue
        grep -Fq -- "$marker" "$doc" || {
            echo "Bootstrap contract marker missing in ${doc#"$repo_root"/}: $marker" >&2
            exit 1
        }
    done <<EOF
$bootstrap_markers
EOF
done <<EOF
$bootstrap_docs
EOF
if grep -Eq 'apt-get install --no-install-recommends python3 python3-venv tar' \
    "$repo_root/README.md"; then
    echo "README.md must not duplicate a shortened bootstrap apt recipe" >&2
    exit 1
fi

# Active package notes must describe framebuffer-only boot, not the retired
# LVGL welcome timer. The installer must not still write that unused path.
if grep -Fq 'automatically advance to the main UI after about 1 second' \
    "$repo_root/packages/ssh-bootstrap/README.md"; then
    echo "packages/ssh-bootstrap/README.md still documents the retired LVGL welcome timer" >&2
    exit 1
fi
if grep -Eq 'deneb_boot = auto|/home/cygnus/menu/img/deneb_boot.png|show_welcome_link.py' \
    "$installer"; then
    echo "packages/ssh-bootstrap/update.sh still writes the unused Cygnus LVGL welcome path" >&2
    exit 1
fi

printf 'SSH bootstrap patch self-test: PASS\n'
