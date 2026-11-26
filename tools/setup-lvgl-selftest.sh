#!/bin/sh
# SPDX-License-Identifier: MPL-2.0

set -eu

setup=$(cd "$(dirname "$0")" && pwd)/setup-lvgl.sh
fixture=$(mktemp -d)
trap 'rm -rf "$fixture"' EXIT
upstream="$fixture/upstream"
module=ui/lib/lvgl
export GIT_ALLOW_PROTOCOL=file
export GIT_CONFIG_GLOBAL=/dev/null
export GIT_CONFIG_NOSYSTEM=1

commit_upstream() {
    git -C "$upstream" add .
    git -C "$upstream" -c user.name=Fixture -c user.email=fixture@example.invalid \
        commit -qm "$1"
    git -C "$upstream" rev-parse HEAD
}

git init -q "$upstream"
git -C "$upstream" config uploadpack.allowFilter true
mkdir -p "$upstream/src" "$upstream/include" "$upstream/tests"
echo first > "$upstream/src/source.c"
touch "$upstream/include/lvgl.h" "$upstream/tests/test.c" "$upstream/LICENCE.txt"
first=$(commit_upstream first)

git init -q "$fixture/project"
cd "$fixture/project"
git config -f .gitmodules "submodule.$module.path" "$module"
git config -f .gitmodules "submodule.$module.url" "file://$upstream"
git add .gitmodules
git update-index --add --cacheinfo "160000,$first,$module"

mkdir -p "$module"
echo existing > "$module/local.txt"
if sh "$setup"; then
    echo "Expected a nonempty LVGL directory to block initialization" >&2
    exit 1
fi
test "$(cat "$module/local.txt")" = existing
rm "$module/local.txt"

git update-index --cacheinfo "160000,9999999999999999999999999999999999999999,$module"
if sh "$setup"; then
    echo "Expected the first fetch to fail for an unavailable pin" >&2
    exit 1
fi
git update-index --cacheinfo "160000,$first,$module"

verify_sparse() {
    test "$(git -C "$module" rev-parse HEAD)" = "$1"
    test "$(git submodule status -- "$module" | cut -d ' ' -f2)" = "$1"
    test -z "$(git -C "$module" status --porcelain)"
    test -f "$module/include/lvgl.h"
    test -f "$module/LICENCE.txt"
    test ! -e "$module/tests"
}

sh "$setup"
verify_sparse "$first"
test "$(cat "$module/src/source.c")" = first
sh "$setup"
verify_sparse "$first"

echo second > "$upstream/src/source.c"
second=$(commit_upstream second)
git update-index --cacheinfo "160000,$second,$module"
sh "$setup"
verify_sparse "$second"
test "$(cat "$module/src/source.c")" = second

git -c user.name=Fixture -c user.email=fixture@example.invalid commit -qm 'Record LVGL pin'
git submodule deinit -- "$module"
test ! -e "$module/.git"
mkdir -p "$module/src"
echo local > "$module/src/source.c"
if sh "$setup"; then
    echo "Expected local files to block LVGL reconnection" >&2
    exit 1
fi
test "$(cat "$module/src/source.c")" = local
rm "$module/src/source.c"
rmdir "$module/src"
sh "$setup"
verify_sparse "$second"

git -C "$module" sparse-checkout disable
sh "$setup"
test -f "$module/tests/test.c"
test "$(git -C "$module" rev-parse HEAD)" = "$second"
echo "Pinned LVGL sparse checkout self-test: PASS"
