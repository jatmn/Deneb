#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0

set -euo pipefail

: "${EVENT_NAME:?EVENT_NAME must be set}"
: "${GITHUB_OUTPUT:?GITHUB_OUTPUT must be set}"
: "${GITHUB_STEP_SUMMARY:?GITHUB_STEP_SUMMARY must be set}"
: "${RUNNER_TEMP:?RUNNER_TEMP must be set}"

BEFORE_SHA="${BEFORE_SHA:-}"
changed_files="${RUNNER_TEMP}/changed-files"
full_validation=false

if [[ "$EVENT_NAME" == "workflow_dispatch" ]]; then
    full_validation=true
elif [[ "$EVENT_NAME" == "pull_request" ]]; then
    git diff --no-renames --name-only -z HEAD^1 HEAD > "$changed_files"
elif [[ -n "$BEFORE_SHA" &&
        "$BEFORE_SHA" != "0000000000000000000000000000000000000000" ]] &&
     git cat-file -e "${BEFORE_SHA}^{commit}" 2>/dev/null; then
    git diff --no-renames --name-only -z "$BEFORE_SHA" HEAD > "$changed_files"
else
    full_validation=true
fi

native=false
shell=false
bootstrap=false
if [[ "$full_validation" == true ]]; then
    native=true
    shell=true
    bootstrap=true
    : > "$changed_files"
else
    while IFS= read -r -d '' path; do
        case "$path" in
            *.sh) shell=true ;;
            .github/workflows/ci.yml) shell=true; bootstrap=true ;;
        esac
        case "$path" in
            assets/branding/*|packages/ssh-bootstrap/*|tools/bootstrap-requirements.txt|tools/png-to-rgb565.py|tools/build-get-started.sh|tools/build-get-started.ps1|tools/build-ssh-bootstrap.ps1)
                bootstrap=true
                ;;
        esac
        # Native cmake/fixture work is for firmware trees and the tools those
        # steps execute. Operator docs, UI markdown, policy scripts, the lane
        # selector, and USB/bootstrap packaging do not need that lane.
        case "$path" in
            .github/workflows/ci.yml|.gitmodules|common/*|dfsvc/*|printsvc/*|web/*|UM2C_MODDING_CHECKLIST.md|docs/PRINTSVC_EVIDENCE_LEDGER.md|docs/PRINTSVC_INTEGRATION_AUDIT.md)
                native=true
                ;;
            ui/*.md)
                ;;
            ui/*)
                native=true
                ;;
            tools/select-ci-validation.sh|tools/select-ci-validation-selftest.sh|tools/check-publication-boundary.ps1|tools/check-markdown-links.ps1|tools/bootstrap-requirements.txt|tools/build-get-started.sh|tools/build-get-started.ps1|tools/build-ssh-bootstrap.ps1|tools/ssh-bootstrap-patch-selftest.sh|tools/build-cura-plugin.ps1)
                ;;
            tools/*)
                native=true
                ;;
        esac
    done < "$changed_files"
fi

{
    printf 'native=%s\n' "$native"
    printf 'shell=%s\n' "$shell"
    printf 'bootstrap=%s\n' "$bootstrap"
} >> "$GITHUB_OUTPUT"
printf '%s\n' '### Validation selection' >> "$GITHUB_STEP_SUMMARY"
printf '%s\n' "* Native lane: \`$native\`" "* Shell lane: \`$shell\`" "* Bootstrap lane: \`$bootstrap\`" >> "$GITHUB_STEP_SUMMARY"
if [[ "$full_validation" == true ]]; then
    printf '%s\n' '* Changed files: full-validation fallback' >> "$GITHUB_STEP_SUMMARY"
else
    printf '%s\n' '* Changed files:' >> "$GITHUB_STEP_SUMMARY"
    while IFS= read -r -d '' path; do
        printf "%s\n" "  * \`${path//\`/\\\`}\`" >> "$GITHUB_STEP_SUMMARY"
    done < "$changed_files"
fi
