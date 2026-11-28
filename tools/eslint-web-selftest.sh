#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Prove host ESLint applies rules to every tracked JavaScript file, accepts
# the existing browser and host scripts, and rejects an undefined name in
# each file type. Install first:
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
eslint_version=$(node -p "require('./tools/eslint/package-lock.json').packages['node_modules/eslint'].version")
eslint_notice="notices/ESLINT-$eslint_version-LICENSE.txt"
if ! cmp -s "$eslint_notice" tools/eslint/node_modules/eslint/LICENSE ||
    ! grep -Fq "eslint-$eslint_version.tgz" docs/SOURCE_PROVENANCE.md ||
    ! grep -Fq "$eslint_notice" THIRD_PARTY_NOTICES.md ||
    ! grep -Fq "$eslint_notice" REUSE.toml
then
    echo "FAIL: update ESLint provenance and notices for $eslint_version" >&2
    exit 1
fi

# ESLint lints a .js, .mjs, or .cjs file with an empty rule set when no
# config block matches it, so a passing run alone does not prove coverage.
js_files=$(mktemp)
trap 'rm -f "$js_files"' EXIT
git ls-files -z -- '*.js' '*.mjs' '*.cjs' > "$js_files"
if [ ! -s "$js_files" ]; then
    echo "FAIL: git ls-files found no tracked JavaScript files" >&2
    exit 1
fi
if ! node --input-type=module - "$CONFIG" "$js_files" <<'JS_COVERAGE'
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { pathToFileURL } from "node:url";

const [config, list] = process.argv.slice(2);
const require = createRequire(pathToFileURL(config));
const { ESLint } = await import(pathToFileURL(require.resolve("eslint")));
const eslint = new ESLint({ overrideConfigFile: config });
let uncovered = 0;
for (const file of readFileSync(list, "utf8").split("\0").filter(Boolean)) {
    if (await eslint.isPathIgnored(file)) {
        continue;
    }
    const rules = (await eslint.calculateConfigForFile(file))?.rules ?? {};
    if (Object.keys(rules).length === 0) {
        console.error(`FAIL: no ESLint rules apply to ${file}`);
        uncovered += 1;
    }
}
process.exit(uncovered === 0 ? 0 : 1);
JS_COVERAGE
then
    echo "FAIL: add an ESLint config block for every tracked JavaScript file" >&2
    exit 1
fi
echo "PASS: every tracked JavaScript file has ESLint rules"

xargs -0 "$ESLINT" -c "$CONFIG" --max-warnings 0 -- < "$js_files"
echo "PASS: tracked JavaScript matches the ESLint rules"

if printf '%s\n' 'var broken = notDeclared;' \
    | "$ESLINT" -c "$CONFIG" --stdin --stdin-filename web/www/js/eslint-selftest-probe.js
then
    echo "FAIL: ESLint accepted an undefined name" >&2
    exit 1
fi
echo "PASS: ESLint rejects an undefined name"

for probe in web/tests/eslint-selftest-probe.cjs scripts/eslint-selftest-probe.mjs; do
    if printf '%s\n' 'var broken = notDeclared;' 'console.log(broken);' \
        | "$ESLINT" -c "$CONFIG" --stdin --stdin-filename "$probe"
    then
        echo "FAIL: ESLint accepted an undefined name in $probe" >&2
        exit 1
    fi
done
echo "PASS: ESLint rejects an undefined name in host Node.js scripts"

if ! printf '%s\n' 'var Deneb = Deneb || {};' 'Deneb.ready = true;' \
    | "$ESLINT" -c "$CONFIG" --stdin --stdin-filename web/www/js/eslint-selftest-ok.js
then
    echo "FAIL: ESLint rejected the existing Deneb browser script header" >&2
    exit 1
fi
echo "PASS: ESLint accepts the existing browser script header"
echo "ESLint web self-test: PASS"
