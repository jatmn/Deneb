#!/bin/sh
# SPDX-License-Identifier: MPL-2.0

set -eu

# Keep native dependencies in the ignored build tree, where the checkout user
# can run and clean release builds without root-owned source artifacts.
repo_input=${1:?usage: $0 /absolute/path/to/Deneb}
repo=$(cd "$repo_input" && pwd)
build_root="$repo/build/deneb-cross"
toolchain_root="$build_root/mipsel-linux-musl-cross"

missing=
for command in cc cmake curl file git make pkg-config python3 sha256sum tar xz; do
    if ! command -v "$command" >/dev/null 2>&1; then
        missing="$missing $command"
    fi
done
if [ -n "$missing" ]; then
    echo "Missing host tools:$missing" >&2
    echo "Install them with: sudo apt-get update && sudo apt-get install --no-install-recommends build-essential ca-certificates cmake curl file git make pkg-config python3 tar xz-utils" >&2
    exit 1
fi

DENEB_SKIP_SYSTEM_PACKAGES=1 \
DENEB_BUILD_ROOT="$build_root" \
DENEB_TOOLCHAIN_ROOT="$toolchain_root" \
sh "$(dirname "$0")/setup-wsl-build.sh" "$repo"
