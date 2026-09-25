#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Prove host ESLint accepts the existing browser scripts and rejects an
# undefined name. Install first:
#   npm ci --ignore-scripts --prefix tools/eslint

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ESLINT="$REPO_ROOT/tools/eslint/node_modules/.bin/eslint"
CONFIG="$REPO_ROOT/tools/eslint/eslint.config.js"

if ! command -v node >/dev/null 2>&1; then
    echo "FAIL: node is required for the ESLint web self-test" >&2
    exit 1
fi
if [ ! -x "$ESLINT" ]; then
    echo "FAIL: ESLint is not installed. Run: npm ci --ignore-scripts --prefix tools/eslint" >&2
    exit 1
fi
if [ -e "$REPO_ROOT/web/www/package.json" ] || [ -d "$REPO_ROOT/web/www/node_modules" ]; then
    echo "FAIL: host npm files must not live in web/www (that tree is copied onto the printer)" >&2
    exit 1
fi

cd "$REPO_ROOT"
"$ESLINT" -c "$CONFIG" web/www/js website/assets/js
echo "PASS: browser JavaScript matches the ESLint rules"

if printf '%s\n' 'var broken = notDeclared;' \
    | "$ESLINT" -c "$CONFIG" --stdin --stdin-filename web/www/js/eslint-selftest-probe.js
then
    echo "FAIL: ESLint accepted an undefined name" >&2
    exit 1
fi
echo "PASS: ESLint rejects an undefined name"

if ! printf '%s\n' 'var Deneb = Deneb || {};' 'Deneb.ready = true;' \
    | "$ESLINT" -c "$CONFIG" --stdin --stdin-filename web/www/js/eslint-selftest-ok.js
then
    echo "FAIL: ESLint rejected the existing Deneb browser script header" >&2
    exit 1
fi
echo "PASS: ESLint accepts the existing browser script header"
echo "ESLint web self-test: PASS"
