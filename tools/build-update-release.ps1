# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [string]$Distro = "Debian",
    [string]$BuildDirectory = "build-musl",
    [string]$WebBuildDirectory = "build-musl",
    [string]$ZmqRoot = "/root/deneb-build/zeromq-4.3.5",
    [string]$LighttpdRoot = "/root/deneb-build/lighttpd-1.4.76",
    [switch]$RebuildZmq,
    [switch]$RebuildLighttpd
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
$lighttpdBinary = "$LighttpdRoot/build-musl-static/build/lighttpd"
$lighttpdVersion = "1.4.76"

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

if ($RebuildLighttpd) {
    if ($LighttpdRoot -notmatch "/lighttpd-$lighttpdVersion/?$") {
        throw "Refusing to rebuild lighttpd outside a lighttpd-$lighttpdVersion leaf directory: $LighttpdRoot"
    }

    $buildLighttpdSource = "set -euo pipefail; " +
                           "mkdir -p '$LighttpdRoot'; " +
                           "rm -rf '$LighttpdRoot'/*; " +
                           "curl -L -o '$LighttpdRoot.tar.xz' https://download.lighttpd.net/lighttpd/releases-1.4.x/lighttpd-$lighttpdVersion.tar.xz; " +
                           "tar xf '$LighttpdRoot.tar.xz' -C '$LighttpdRoot' --strip-components=1; " +
                           "rm -f '$LighttpdRoot.tar.xz'"
    & wsl -d $Distro -- bash -lc $buildLighttpdSource
    if ($LASTEXITCODE -ne 0) {
        throw "lighttpd source fetch failed with exit code $LASTEXITCODE"
    }
}

$buildApi = "set -euo pipefail; " +
            "cd '$repoWsl/web'; " +
            "mkdir -p '$WebBuildDirectory'; " +
            "cd '$WebBuildDirectory'; " +
            "if [ -f CMakeCache.txt ]; then " +
            "grep -q '/root/mipsel-linux-musl-cross/bin/mipsel-linux-musl-gcc' CMakeCache.txt || " +
            "{ echo 'Existing web build directory is not configured for mipsel musl. Use a different -WebBuildDirectory.' >&2; exit 1; }; " +
            "cmake .. -DCMAKE_BUILD_TYPE=MinSizeRel -DZMQ_STATIC_PATH='$zmqLib' -DZMQ_INCLUDE_DIR='$zmqInclude'; " +
            "else " +
            "cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/mipsel-musl-toolchain.cmake " +
            "-DCMAKE_BUILD_TYPE=MinSizeRel -DZMQ_STATIC_PATH='$zmqLib' -DZMQ_INCLUDE_DIR='$zmqInclude'; " +
            "fi; " +
            "make -j`$(nproc)"

& wsl -d $Distro -- bash -lc $buildApi
if ($LASTEXITCODE -ne 0) {
    throw "release API build failed with exit code $LASTEXITCODE"
}

$buildPrintsvc = "set -euo pipefail; " +
                 "cd '$repoWsl/printsvc'; " +
                 "mkdir -p '$BuildDirectory'; " +
                 "cd '$BuildDirectory'; " +
                 "if [ -f CMakeCache.txt ]; then " +
                 "grep -q '/root/mipsel-linux-musl-cross/bin/mipsel-linux-musl-gcc' CMakeCache.txt || " +
                 "{ echo 'Existing printsvc build directory is not configured for mipsel musl. Use a different -BuildDirectory.' >&2; exit 1; }; " +
                 "cmake .. -DCMAKE_BUILD_TYPE=MinSizeRel -DZMQ_STATIC_PATH='$zmqLib' -DZMQ_INCLUDE_DIR='$zmqInclude'; " +
                 "else " +
                 "cmake .. -DCMAKE_TOOLCHAIN_FILE='$repoWsl/ui/cmake/mipsel-musl-toolchain.cmake' " +
                 "-DCMAKE_BUILD_TYPE=MinSizeRel -DZMQ_STATIC_PATH='$zmqLib' -DZMQ_INCLUDE_DIR='$zmqInclude'; " +
                 "fi; " +
                 "make -j`$(nproc); " +
                 "test -x deneb-printsvc"

& wsl -d $Distro -- bash -lc $buildPrintsvc
if ($LASTEXITCODE -ne 0) {
    throw "release print service build failed with exit code $LASTEXITCODE"
}

$buildLighttpd = "set -euo pipefail; " +
                 "test -d '$LighttpdRoot/src' || { echo 'Missing lighttpd source at $LighttpdRoot; re-run with -RebuildLighttpd.' >&2; exit 1; }; " +
                 "cd '$LighttpdRoot/src'; " +
                 "printf '%s\n' " +
                 "'PLUGIN_INIT(mod_access)' " +
                 "'PLUGIN_INIT(mod_alias)' " +
                 "'PLUGIN_INIT(mod_indexfile)' " +
                 "'PLUGIN_INIT(mod_staticfile)' " +
                 "'PLUGIN_INIT(mod_setenv)' " +
                 "'PLUGIN_INIT(mod_proxy)' > plugin-static.h; " +
                 "cd '$LighttpdRoot'; " +
                 "mkdir -p build-musl-static; " +
                 "cd build-musl-static; " +
                 "if [ ! -f CMakeCache.txt ]; then " +
                 "cmake .. -DCMAKE_TOOLCHAIN_FILE='$repoWsl/ui/cmake/mipsel-musl-toolchain.cmake' " +
                 "-DCMAKE_BUILD_TYPE=MinSizeRel -DBUILD_STATIC=ON -DWITH_PCRE=OFF -DWITH_PCRE2=OFF " +
                 "-DWITH_ZLIB=OFF -DWITH_BZIP=OFF -DWITH_BROTLI=OFF -DWITH_ZSTD=OFF " +
                 "-DWITH_OPENSSL=OFF -DWITH_MBEDTLS=OFF -DWITH_WOLFSSL=OFF -DWITH_GNUTLS=OFF; " +
                 "fi; " +
                 "make -j`$(nproc) lighttpd; " +
                 "test -x '$lighttpdBinary'"

& wsl -d $Distro -- bash -lc $buildLighttpd
if ($LASTEXITCODE -ne 0) {
    throw "release lighttpd build failed with exit code $LASTEXITCODE"
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
           "bash ui/build-package.sh ui/$BuildDirectory/deneb-ui web/$WebBuildDirectory/deneb-api '$lighttpdBinary' web/$WebBuildDirectory/deneb-mdns printsvc/$BuildDirectory/deneb-printsvc"

& wsl -d $Distro -- bash -lc $buildUi
if ($LASTEXITCODE -ne 0) {
    throw "update release build failed with exit code $LASTEXITCODE"
}
