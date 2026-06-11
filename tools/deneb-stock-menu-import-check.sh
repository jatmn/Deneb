#!/bin/bash
# SPDX-License-Identifier: MPL-2.0
#
# Import-dependency check for retained stock Python constants.
#
# Scans the rootfs stock Python source tree for imports from
# cygnus.menu.menu_settings and reports which constants are still
# needed by surviving (non-menu) services versus only used by the
# dormant UI that Deneb prunes.
#
# Usage:
#   ./deneb-stock-menu-import-check.sh [--repo-root <path>]
#
# Default repo root is the parent of the tools/ directory.

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="${DENEB_REPO_ROOT:-$(cd "${SCRIPT_DIR}/.." && pwd)}"
ROOTFS_PYTHON="${REPO_ROOT}/rootfs/home/cygnus"

usage() {
    echo "Usage: $(basename "$0") [--repo-root <path>]"
    exit 2
}

while [ $# -gt 0 ]; do
    case "$1" in
        --repo-root) REPO_ROOT="$2"; shift 2 ;;
        --help|-h) usage ;;
        *) usage ;;
    esac
done

# Files kept because they back surviving stock services
SURVIVING_DIRS="coordinator|util"

pass()  { printf '  PASS: %s\n' "$1"; }
info()  { printf '  INFO: %s\n' "$1"; }
warn()  { printf '  WARN: %s\n' "$1"; }
fail()  { printf '  FAIL: %s\n' "$1" >&2; exit 1; }

if [ ! -d "$ROOTFS_PYTHON" ]; then
    fail "stock Python tree not found: $ROOTFS_PYTHON"
fi

echo "=== Stock Menu Import-Dependency Check ==="
echo "Rootfs: ${ROOTFS_PYTHON}"
echo

# ------------------------------------------------------------------
# Phase 1 — Find all files that import menu_settings
# ------------------------------------------------------------------

echo "--- Files importing cygnus.menu.menu_settings ---"

IMPORTING_FILES=$(
    find "$ROOTFS_PYTHON" -name '*.py' \
        -exec grep -El 'from cygnus\.menu import .*menu_settings|from cygnus\.menu\.menu_settings import|import cygnus\.menu\.menu_settings' {} \; \
        2>/dev/null | sort
)

if [ -z "$IMPORTING_FILES" ]; then
    fail "no files import menu_settings — retained file may be dead"
fi

for f in $IMPORTING_FILES; do
    rel="${f#$ROOTFS_PYTHON/}"
    # Determine if this is a surviving service or pruned UI file
    surviving=0
    for dir in $(echo "$SURVIVING_DIRS" | tr '|' ' '); do
        case "$rel" in
            ${dir}/*)
                surviving=1
                break
                ;;
        esac
    done
    if [ "$surviving" -eq 1 ]; then
        echo "  SURVIVING: ${rel}"
    else
        echo "  PRUNED-UI: ${rel}"
    fi
done
echo

# ------------------------------------------------------------------
# Phase 2 — Catalogue every constant accessed via menu_settings.<NAME>
# ------------------------------------------------------------------

echo "--- Constants accessed per file ---"

declare -A SURVIVING_CONSTANTS
declare -A PRUNED_CONSTANTS

while IFS= read -r f; do
    rel="${f#$ROOTFS_PYTHON/}"
    consts=$(
        {
            grep -oE 'menu_settings\.[A-Za-z_][A-Za-z0-9_]*' "$f" 2>/dev/null |
                sed 's/^menu_settings\.//'
            grep -E '^[[:space:]]*from cygnus\.menu\.menu_settings import ' "$f" 2>/dev/null |
                sed -E 's/^[[:space:]]*from cygnus\.menu\.menu_settings import //' |
                tr ',' '\n' |
                sed -E 's/#.*//; s/[()\\]//g; s/^[[:space:]]*//; s/[[:space:]]*$//'
        } | grep -E '^[A-Za-z_][A-Za-z0-9_]*$' | sort -u || true
    )

    if [ -z "$consts" ]; then
        echo "  ${rel}: (no direct menu_settings constant access)"
        continue
    fi

    echo "  ${rel}:"
    for c in $consts; do
        echo "    - menu_settings.${c}"
        stripped="$c"
        surviving=0
        for dir in $(echo "$SURVIVING_DIRS" | tr '|' ' '); do
            case "$rel" in
                ${dir}/*)
                    surviving=1
                    break
                    ;;
            esac
        done
        if [ "$surviving" -eq 1 ]; then
            SURVIVING_CONSTANTS["$stripped"]=1
        else
            PRUNED_CONSTANTS["$stripped"]=1
        fi
    done
done <<EOF
$IMPORTING_FILES
EOF
echo

# ------------------------------------------------------------------
# Phase 3 — Report which constants are truly needed
# ------------------------------------------------------------------

echo "--- Summary ---"

if [ ${#SURVIVING_CONSTANTS[@]} -eq 0 ]; then
    echo "  WARNING: No surviving import of menu_settings constants found."
    info "The retained menu_settings.py may still be needed if accessed"
    info "via string-based imports or at runtime through sys.path."
fi

for c in "${!SURVIVING_CONSTANTS[@]}"; do
    info "Constant still required by surviving services: menu_settings.${c}"
done

for c in "${!PRUNED_CONSTANTS[@]}"; do
    if [ -z "${SURVIVING_CONSTANTS[$c]:-}" ]; then
        info "Constant only used by pruned UI (candidate for future removal): menu_settings.${c}"
    fi
done

echo
echo "--- Retained file dependency map ---"
echo
echo "  Retained:   menu_settings.py"
echo "  Retained:   machine_config.json (referenced as path from menu_settings.MACHINE_CONFIG)"
echo
echo "  menu_settings constants used by surviving coordinator services:"
if [ ${#SURVIVING_CONSTANTS[@]} -eq 0 ]; then
    echo "    (none — retained for defensive/import-chain compatibility)"
else
    for c in "${!SURVIVING_CONSTANTS[@]}"; do
        echo "    - ${c}"
    done
fi

echo
info "To verify: run 'find ${ROOTFS_PYTHON} -name '*.py' -exec grep -l 'menu_settings' {} +'"
echo "=== Check complete ==="
