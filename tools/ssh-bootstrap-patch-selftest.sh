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

printf 'SSH bootstrap patch self-test: PASS\n'
