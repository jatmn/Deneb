#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
SELECTOR="${SCRIPT_DIR}/select-ci-validation.sh"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

repo="${TMP_DIR}/repo"
git init -q "$repo"
git -C "$repo" config user.email ci-selftest@example.invalid
git -C "$repo" config user.name "CI self-test"

mkdir -p "$repo/.github/workflows" "$repo/common" "$repo/docs" "$repo/packages/ssh-bootstrap" "$repo/tools" "$repo/ui"
printf 'int original;\n' > "$repo/common/original.c"
git -C "$repo" add .
git -C "$repo" commit -qm base
base="$(git -C "$repo" rev-parse HEAD)"

run_selector() {
    local event_name="$1"
    local before_sha="$2"
    local output="${TMP_DIR}/output"
    local summary="${TMP_DIR}/summary"
    : > "$output"
    : > "$summary"
    (
        cd "$repo"
        EVENT_NAME="$event_name" \
        BEFORE_SHA="$before_sha" \
        GITHUB_OUTPUT="$output" \
        GITHUB_STEP_SUMMARY="$summary" \
        RUNNER_TEMP="$TMP_DIR" \
        bash "$SELECTOR"
    )
    cat "$output"
}

assert_lanes() {
    local expected_native="$1"
    local expected_shell="$2"
    local expected_bootstrap="$3"
    local actual="$4"
    grep -qx "native=${expected_native}" <<< "$actual"
    grep -qx "shell=${expected_shell}" <<< "$actual"
    grep -qx "bootstrap=${expected_bootstrap}" <<< "$actual"
}

assert_lanes true true true "$(run_selector workflow_dispatch '')"
assert_lanes true true true "$(run_selector push 1111111111111111111111111111111111111111)"

printf 'documentation\n' > "$repo/docs/guide.md"
git -C "$repo" add .
git -C "$repo" commit -qm docs
assert_lanes false false false "$(run_selector push "$base")"

before="$(git -C "$repo" rev-parse HEAD)"
printf '#!/bin/sh\n' > "$repo/packages/ssh-bootstrap/update.sh"
git -C "$repo" add .
git -C "$repo" commit -qm shell
assert_lanes false true true "$(run_selector push "$before")"

before="$(git -C "$repo" rev-parse HEAD)"
printf '# UI notes\n' > "$repo/ui/README.md"
git -C "$repo" add .
git -C "$repo" commit -qm ui-docs
assert_lanes false false false "$(run_selector push "$before")"

before="$(git -C "$repo" rev-parse HEAD)"
printf '#!/bin/sh\n' > "$repo/tools/build-get-started.sh"
git -C "$repo" add .
git -C "$repo" commit -qm bootstrap-builder
assert_lanes false true true "$(run_selector push "$before")"

before="$(git -C "$repo" rev-parse HEAD)"
printf 'Write-Output bootstrap\n' > "$repo/tools/build-ssh-bootstrap.ps1"
git -C "$repo" add .
git -C "$repo" commit -qm bootstrap-packaging
assert_lanes false false true "$(run_selector push "$before")"

before="$(git -C "$repo" rev-parse HEAD)"
printf 'Write-Output wrapper\n' > "$repo/tools/build-get-started.ps1"
git -C "$repo" add .
git -C "$repo" commit -qm bootstrap-wrapper
assert_lanes false false true "$(run_selector push "$before")"

before="$(git -C "$repo" rev-parse HEAD)"
printf '# converter\n' > "$repo/tools/png-to-rgb565.py"
git -C "$repo" add .
git -C "$repo" commit -qm bootstrap-converter
assert_lanes true false true "$(run_selector push "$before")"

before="$(git -C "$repo" rev-parse HEAD)"
mkdir -p "$repo/assets/branding"
printf 'branding\n' > "$repo/assets/branding/splash.png"
git -C "$repo" add .
git -C "$repo" commit -qm bootstrap-branding
assert_lanes false false true "$(run_selector push "$before")"

before="$(git -C "$repo" rev-parse HEAD)"
printf 'manifest\n' > "$repo/packages/ssh-bootstrap/manifest.txt"
git -C "$repo" add .
git -C "$repo" commit -qm bootstrap-payload
assert_lanes false false true "$(run_selector push "$before")"

before="$(git -C "$repo" rev-parse HEAD)"
printf 'Write-Output policy\n' > "$repo/tools/check-publication-boundary.ps1"
git -C "$repo" add .
git -C "$repo" commit -qm policy-tool
assert_lanes false false false "$(run_selector push "$before")"

before="$(git -C "$repo" rev-parse HEAD)"
printf '#!/bin/sh\n' > "$repo/tools/select-ci-validation.sh"
git -C "$repo" add .
git -C "$repo" commit -qm selector
assert_lanes false true false "$(run_selector push "$before")"

before="$(git -C "$repo" rev-parse HEAD)"
printf '#!/bin/sh\n' > "$repo/tools/deneb-compile-all-selftest.sh"
git -C "$repo" add .
git -C "$repo" commit -qm native-tool
assert_lanes true true false "$(run_selector push "$before")"

before="$(git -C "$repo" rev-parse HEAD)"
printf 'int ui;\n' > "$repo/ui/main.c"
git -C "$repo" add .
git -C "$repo" commit -qm ui-source
assert_lanes true false false "$(run_selector push "$before")"

before="$(git -C "$repo" rev-parse HEAD)"
printf 'name: CI\n' > "$repo/.github/workflows/ci.yml"
git -C "$repo" add .
git -C "$repo" commit -qm workflow
assert_lanes true true true "$(run_selector push "$before")"

before="$(git -C "$repo" rev-parse HEAD)"
git -C "$repo" mv common/original.c docs/original.md
git -C "$repo" commit -qm rename
assert_lanes true false false "$(run_selector push "$before")"

printf 'CI validation selector self-test: PASS\n'
