# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [string]$Distro = "Debian",
    [string]$BuildDirectory = "build-musl",
    [string]$ZmqRoot = "/root/deneb-build/zeromq-4.3.5",
    [switch]$RebuildZmq
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

function ConvertTo-WslPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if ($fullPath -notmatch '^([A-Za-z]):\\(.*)$') {
        throw "Only drive-letter Windows paths are supported: $fullPath"
    }

    $drive = $Matches[1].ToLowerInvariant()
    $rest = $Matches[2].Replace('\', '/')
    return "/mnt/$drive/$rest"
}

$repoWsl = ConvertTo-WslPath $repoRoot

$toolchainCheck = "/root/mipsel-linux-musl-cross/bin/mipsel-linux-musl-gcc --version >/dev/null"
& wsl -d $Distro -- bash -lc $toolchainCheck
if ($LASTEXITCODE -ne 0) {
    throw "Missing musl cross-compiler. Follow ui/README.md production build prerequisites first."
}

$zmqLib = "$ZmqRoot/build-musl/lib/libzmq.a"
$zmqInclude = "$ZmqRoot/include"

if ($RebuildZmq) {
    if ($ZmqRoot -notmatch '/zeromq-4\.3\.5/?$') {
        throw "Refusing to rebuild libzmq outside a zeromq-4.3.5 leaf directory: $ZmqRoot"
    }

    $buildZmq = "set -euo pipefail; " +
                "mkdir -p '$ZmqRoot'; " +
                "rm -rf '$ZmqRoot'/*; " +
                "curl -L -o '$ZmqRoot.tar.gz' https://github.com/zeromq/libzmq/releases/download/v4.3.5/zeromq-4.3.5.tar.gz; " +
                "tar xzf '$ZmqRoot.tar.gz' -C '$ZmqRoot' --strip-components=1; " +
                "rm -f '$ZmqRoot.tar.gz'; " +
                "cd '$ZmqRoot'; mkdir build-musl; cd build-musl; " +
                "cmake .. -DCMAKE_TOOLCHAIN_FILE='$repoWsl/ui/cmake/mipsel-musl-toolchain.cmake' " +
                "-DCMAKE_BUILD_TYPE=MinSizeRel -DWITH_LIBSODIUM=OFF -DZMQ_BUILD_TESTS=OFF " +
                "-DWITH_DOCS=OFF -DBUILD_SHARED=OFF -DBUILD_STATIC=ON; " +
                "make -j`$(nproc)"
    & wsl -d $Distro -- bash -lc $buildZmq
    if ($LASTEXITCODE -ne 0) {
        throw "libzmq musl build failed with exit code $LASTEXITCODE"
    }
}

$zmqCheck = "test -f '$zmqLib' && test -d '$zmqInclude'"
& wsl -d $Distro -- bash -lc $zmqCheck
if ($LASTEXITCODE -ne 0) {
    throw "Missing musl libzmq at $zmqLib. Re-run with -RebuildZmq."
}

$buildUi = "set -euo pipefail; " +
           "cd '$repoWsl/ui'; " +
           "mkdir -p '$BuildDirectory'; " +
           "cd '$BuildDirectory'; " +
           "if [ -f CMakeCache.txt ]; then " +
           "grep -q 'mipsel-linux-musl-gcc' CMakeCache.txt || " +
           "{ echo 'Existing build directory is not configured for mipsel musl. Use a different -BuildDirectory.' >&2; exit 1; }; " +
           "cmake .. -DCMAKE_BUILD_TYPE=MinSizeRel -DZMQ_LIBRARY='$zmqLib' -DZMQ_INCLUDE_DIR='$zmqInclude'; " +
           "else " +
           "cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/mipsel-musl-toolchain.cmake " +
           "-DCMAKE_BUILD_TYPE=MinSizeRel -DZMQ_LIBRARY='$zmqLib' -DZMQ_INCLUDE_DIR='$zmqInclude'; " +
           "fi; " +
           "make -j`$(nproc); " +
           "cd '$repoWsl'; " +
           "bash ui/build-package.sh ui/$BuildDirectory/deneb-ui"

& wsl -d $Distro -- bash -lc $buildUi
if ($LASTEXITCODE -ne 0) {
    throw "release UI build failed with exit code $LASTEXITCODE"
}
