#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0

# Exercise release compiler reuse through the wrapper with real CMake builds.
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
fixture=$(mktemp -d)
trap 'rm -rf -- "$fixture"' EXIT

repo="$fixture/repo"
export DENEB_BUILD_ROOT="$fixture/deps"
export DENEB_TOOLCHAIN_ROOT="$fixture/pinned compiler"
toolchain="$DENEB_TOOLCHAIN_ROOT/bin/mipsel-linux-musl-gcc"
zmq_root="$DENEB_BUILD_ROOT/zeromq-4.3.5"
mkdir -p "$repo/tools" "$DENEB_TOOLCHAIN_ROOT/bin" "$zmq_root/include" "$zmq_root/build-musl/lib"
cp "$script_dir/build-update-release.sh" "$repo/tools/"
touch "$zmq_root/build-musl/lib/libzmq.a"

# Distinct compiler paths emulate a pinned toolchain and another installation.
# Both invoke host cc so the fixture runs without release cross dependencies.
cat > "$toolchain" <<'EOF'
#!/bin/sh
exec cc "$@"
EOF
chmod +x "$toolchain"
cp "$toolchain" "$toolchain-other"

make_project() {
    local source=$1 target=$2
    mkdir -p "$source"
    printf 'cmake_minimum_required(VERSION 3.16)\nproject(%s C)\nadd_executable(%s main.c)\n' \
        "$target" "$target" > "$source/CMakeLists.txt"
    printf '%s\n' 'int main(void) { return 0; }' > "$source/main.c"
}

make_lighttpd() {
    make_project "$1" lighttpd
    mkdir -p "$1/src"
    cat >> "$1/CMakeLists.txt" <<'EOF'
set_target_properties(lighttpd PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build")
EOF
}

make_project "$repo/web" deneb-api
make_project "$repo/printsvc" deneb-printsvc
make_project "$repo/dfsvc" deneb-dfsvc
make_project "$repo/ui" deneb-ui
for component in web ui; do
    mkdir -p "$repo/$component/cmake"
    cat > "$repo/$component/cmake/mipsel-musl-toolchain.cmake" <<'EOF'
set(CMAKE_C_COMPILER "$ENV{DENEB_TOOLCHAIN_ROOT}/bin/mipsel-linux-musl-gcc")
EOF
done
# Stop after the real configure/build paths. The release package is tested
# separately with the pinned MIPS tools and complete Deneb sources.
cat > "$repo/ui/build-package.sh" <<'EOF'
printf '%s\n' 'Reached fixture packager'
exit 47
EOF

release() {
    if bash "$repo/tools/build-update-release.sh" "$@" > "$fixture/wrapper.log" 2>&1; then
        result=0
    else
        result=$?
    fi
}

expect_result() {
    if [ "$result" -ne "$1" ] || ! grep -Fq -- "$2" "$fixture/wrapper.log"; then
        cat "$fixture/wrapper.log" >&2
        printf 'Expected exit %s and output containing: %s\n' "$1" "$2" >&2
        exit 1
    fi
}

test_reuse() {
    local generator=$1 name=$2 lighttpd_root="$fixture/lighttpd-$2"
    export CMAKE_GENERATOR="$generator"
    make_lighttpd "$lighttpd_root"

    release --build-directory "$name" --web-build-directory "$name" --lighttpd-root "$lighttpd_root"
    expect_result 47 'Reached fixture packager'
    # Keep the real compiler record and exercise reuse without a FILEPATH entry
    # across CMake versions that write different cache layouts.
    sed -i '/^CMAKE_C_COMPILER:FILEPATH=/d' \
        "$repo/web/$name/CMakeCache.txt" "$lighttpd_root/build-musl-static/CMakeCache.txt"
    release --build-directory "$name" --web-build-directory "$name" --lighttpd-root "$lighttpd_root"
    expect_result 47 'Reached fixture packager'
    printf 'Cold and warm compiler checks passed with %s.\n' "$generator"
}

test_reuse 'Unix Makefiles' make
if command -v ninja >/dev/null; then
    test_reuse Ninja ninja
fi

export CMAKE_GENERATOR='Unix Makefiles'
cmake -S "$repo/web" -B "$repo/web/host" -DCMAKE_C_COMPILER:FILEPATH="$(command -v cc)" >/dev/null
# A stale pinned record from another CMake version must never match.
mkdir -p "$repo/web/host/CMakeFiles/0.0.0"
cp "$repo/web/make"/CMakeFiles/*/CMakeCCompiler.cmake "$repo/web/host/CMakeFiles/0.0.0/"
release --build-directory make --web-build-directory host --lighttpd-root "$fixture/lighttpd-make"
expect_result 1 'Existing build directory is not configured for the pinned mipsel musl compiler'

wrong_lighttpd="$fixture/lighttpd-other"
make_lighttpd "$wrong_lighttpd"
cmake -S "$wrong_lighttpd" -B "$wrong_lighttpd/build-musl-static" \
    -DCMAKE_C_COMPILER:FILEPATH="$toolchain-other" >/dev/null
release --build-directory make --web-build-directory make --lighttpd-root "$wrong_lighttpd"
expect_result 1 'Existing lighttpd build directory is not configured for the pinned mipsel musl compiler'
printf '%s\n' 'Compiler reuse rejected the native and alternate compiler directories.'
