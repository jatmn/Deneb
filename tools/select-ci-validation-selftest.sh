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

mkdir -p "$repo/.github/workflows" "$repo/common" "$repo/docs" "$repo/packages/bootstrap"
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
    local actual="$3"
    grep -qx "native=${expected_native}" <<< "$actual"
    grep -qx "shell=${expected_shell}" <<< "$actual"
}

assert_lanes true true "$(run_selector workflow_dispatch '')"
assert_lanes true true "$(run_selector push 1111111111111111111111111111111111111111)"

printf 'documentation\n' > "$repo/docs/guide.md"
git -C "$repo" add .
git -C "$repo" commit -qm docs
assert_lanes false false "$(run_selector push "$base")"

before="$(git -C "$repo" rev-parse HEAD)"
printf '#!/bin/sh\n' > "$repo/packages/bootstrap/update.sh"
git -C "$repo" add .
git -C "$repo" commit -qm shell
assert_lanes false true "$(run_selector push "$before")"

before="$(git -C "$repo" rev-parse HEAD)"
printf 'name: CI\n' > "$repo/.github/workflows/ci.yml"
git -C "$repo" add .
git -C "$repo" commit -qm workflow
assert_lanes true true "$(run_selector push "$before")"

before="$(git -C "$repo" rev-parse HEAD)"
git -C "$repo" mv common/original.c docs/original.md
git -C "$repo" commit -qm rename
assert_lanes true false "$(run_selector push "$before")"

printf 'CI validation selector self-test: PASS\n'
