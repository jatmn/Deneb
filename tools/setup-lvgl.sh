#!/bin/sh
# SPDX-License-Identifier: MPL-2.0

set -eu

path=ui/lib/lvgl
cd "$(git rev-parse --show-toplevel)"
pin=$(git ls-files --stage -- "$path" | awk '$1 == "160000" && $3 == "0" { print $2 }')
if [ -z "$pin" ]; then
    echo "No resolved LVGL gitlink in the repository index: $path" >&2
    exit 1
fi

checkout_sparse_pin() {
    git submodule absorbgitdirs -- "$path"
    git -C "$path" fetch --no-tags --depth 1 --filter=blob:none origin "$pin"
    git -C "$path" checkout --detach "$pin"
}

# An interrupted first fetch leaves an unborn, configured sparse submodule.
# Submodule update needs a current revision, so finish the initial checkout.
if [ -e "$path/.git" ] &&
    ! git -C "$path" rev-parse --verify --quiet HEAD >/dev/null &&
    [ "$(git -C "$path" config --get remote.origin.partialclonefilter)" = blob:none ] &&
    [ "$(git -C "$path" config --bool core.sparseCheckout)" = true ]; then
    checkout_sparse_pin
    exit
fi

# Git can retain submodule metadata after the worktree was deinitialized.
if [ ! -e "$path/.git" ]; then
    mkdir -p "$path"
    existing_entry=$(find "$path" -mindepth 1 -maxdepth 1 -print -quit)
    if [ -n "$existing_entry" ]; then
        echo "Refusing to initialize LVGL in a nonempty directory: $path" >&2
        exit 1
    fi
fi

# Keep the user's sparse-checkout configuration when Git already manages LVGL.
if [ -e "$path/.git" ] || [ -d "$(git rev-parse --git-path "modules/$path")" ]; then
    git submodule update --init --checkout --depth 1 --filter=blob:none -- "$path"
    exit
fi

git submodule init -- "$path"
url=$(git config --get "submodule.$path.url")
git init --quiet "$path"
git -C "$path" remote add origin "$url"
git -C "$path" config remote.origin.promisor true
git -C "$path" config remote.origin.partialclonefilter blob:none
git -C "$path" sparse-checkout set --cone src include
checkout_sparse_pin
