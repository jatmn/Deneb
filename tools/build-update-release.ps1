# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [string]$Distro = "Debian",
    [string]$BuildDirectory = "build-musl",
    [string]$WebBuildDirectory = "build-musl",
    [string]$ZmqRoot = "/root/deneb-build/zeromq-4.3.5",
    [string]$LighttpdRoot = "/root/deneb-build/lighttpd-1.4.76",
    [ValidateSet("experimental", "nightly", "stable")]
    [string]$ReleaseChannel = "experimental",
    [string]$PrintsvcStockSummary = "",
    [string]$PrintsvcNativeSummary = "",
    [string[]]$PrintsvcNativeEvidenceSummary = @(),
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

function ConvertTo-WslInputPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ($Path -match '^/') {
        return $Path
    }

    return ConvertTo-WslPath $Path
}

$repoWsl = ConvertTo-WslPath $repoRoot

if ($ReleaseChannel -ne "experimental" -and
    ([string]::IsNullOrWhiteSpace($PrintsvcStockSummary) -or
     [string]::IsNullOrWhiteSpace($PrintsvcNativeSummary))) {
    throw "Non-experimental release builds require -PrintsvcStockSummary and -PrintsvcNativeSummary."
}

$printsvcStockSummaryWsl = ""
$printsvcNativeSummaryWsl = ""
$printsvcNativeEvidenceSummaryWsl = @()
if (-not [string]::IsNullOrWhiteSpace($PrintsvcStockSummary)) {
    $printsvcStockSummaryWsl = ConvertTo-WslInputPath $PrintsvcStockSummary
}
if (-not [string]::IsNullOrWhiteSpace($PrintsvcNativeSummary)) {
    $printsvcNativeSummaryWsl = ConvertTo-WslInputPath $PrintsvcNativeSummary
}
foreach ($summary in $PrintsvcNativeEvidenceSummary) {
    if (-not [string]::IsNullOrWhiteSpace($summary)) {
        $printsvcNativeEvidenceSummaryWsl += (ConvertTo-WslInputPath $summary)
    }
}

$buildPackageEnv = "DENEB_RELEASE_CHANNEL='$ReleaseChannel'"
if ($printsvcStockSummaryWsl) {
    $buildPackageEnv += " DENEB_PRINTSVC_STOCK_SUMMARY='$printsvcStockSummaryWsl'"
}
if ($printsvcNativeSummaryWsl) {
    $buildPackageEnv += " DENEB_PRINTSVC_NATIVE_SUMMARY='$printsvcNativeSummaryWsl'"
}
if ($printsvcNativeEvidenceSummaryWsl.Count -gt 0) {
    $buildPackageEnv += " DENEB_PRINTSVC_NATIVE_EVIDENCE_SUMMARIES='$($printsvcNativeEvidenceSummaryWsl -join ' ')'"
}

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
           "cd '$repoWsl/dfsvc'; " +
           "mkdir -p '$BuildDirectory'; " +
           "cd '$BuildDirectory'; " +
           "if [ -f CMakeCache.txt ]; then " +
           "grep -q 'mipsel-linux-musl-gcc' CMakeCache.txt || " +
           "{ echo 'Existing dfsvc build directory is not configured for mipsel musl. Use a different -BuildDirectory.' >&2; exit 1; }; " +
           "cmake .. -DCMAKE_BUILD_TYPE=MinSizeRel -DZMQ_STATIC_PATH='$zmqLib' -DZMQ_INCLUDE_DIR='$zmqInclude' -DMBEDTLS_ROOT=/root/deneb-build/mbedtls-2.28.8-mipsel; " +
           "else " +
           "cmake .. -DCMAKE_TOOLCHAIN_FILE=../../ui/cmake/mipsel-musl-toolchain.cmake " +
           "-DCMAKE_BUILD_TYPE=MinSizeRel -DZMQ_STATIC_PATH='$zmqLib' -DZMQ_INCLUDE_DIR='$zmqInclude' -DMBEDTLS_ROOT=/root/deneb-build/mbedtls-2.28.8-mipsel; " +
           "fi; " +
           "make -j`$(nproc); " +
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
           "$buildPackageEnv bash ui/build-package.sh ui/$BuildDirectory/deneb-ui web/$WebBuildDirectory/deneb-api '$lighttpdBinary' web/$WebBuildDirectory/deneb-mdns printsvc/$BuildDirectory/deneb-printsvc dfsvc/$BuildDirectory/deneb-dfsvc"

& wsl -d $Distro -- bash -lc $buildUi
if ($LASTEXITCODE -ne 0) {
    throw "update release build failed with exit code $LASTEXITCODE"
}

$gitShort = (& git -C $repoRoot rev-parse --short HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($gitShort)) {
    throw "failed to determine git short revision for package verification"
}
$packageWsl = "$repoWsl/dist/Deneb_Update_$gitShort.deneb"

$verifyPackage = "set -euo pipefail; " +
                 "test -s '$packageWsl' || { echo 'No Deneb update package found: $packageWsl' >&2; exit 1; }; " +
                 "tar tf '$packageWsl' > /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)update.sh$' /tmp/deneb-release-package-files.txt; " +
                 "! grep -Eq '(^|/)deneb-df-bridge$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)deneb-printsvc$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)deneb-dfsvc$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)deneb-printsvc.init$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)digitalfactory.init$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)deneb-printsvc-smoke$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)deneb-printsvc-smoke-verify$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)deneb-printsvc-smoke-compare$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)deneb-printsvc-smoke-selftest$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)deneb-printsvc-stability$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)deneb-active-physical-soak-runner$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)deneb-printsvc-stock-baseline$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)deneb-printsvc-cli-selftest$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)deneb-printsvc-init-selftest$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)deneb-printsvc-release-gate-selftest$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)deneb-printsvc-native-audit$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)deneb-printsvc-native-audit-selftest$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)deneb-printsvc-integration-audit$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)deneb-printsvc-integration-audit-selftest$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)deneb-runtime-inventory$' /tmp/deneb-release-package-files.txt; " +
                 "tar -xOf '$packageWsl' manifest.txt > /tmp/deneb-release-manifest.txt; " +
                 "grep -Eq '^channel: $ReleaseChannel$' /tmp/deneb-release-manifest.txt; " +
                 "grep -Eq '^native_printsvc: experimental$' /tmp/deneb-release-manifest.txt; " +
                 "grep -Eq '^native_printsvc_release_gate: non-experimental packages require verified stock/native smoke summaries with strict resource reduction$' /tmp/deneb-release-manifest.txt; " +
                 "if grep -Ei '(^|/).*\.py$|(^|/).*python.*|(^|/)print_service\.py$' /tmp/deneb-release-package-files.txt; then " +
                 "echo 'Python driver artifact found in release package:' >&2; " +
                 "printf '%s\n' '$packageWsl' >&2; exit 1; " +
                 "fi; " +
                 "rm -rf /tmp/deneb-release-smoke-selftest; mkdir -p /tmp/deneb-release-smoke-selftest; " +
                 "tar xf '$packageWsl' -C /tmp/deneb-release-smoke-selftest deneb-printsvc-smoke deneb-printsvc-smoke-verify deneb-printsvc-smoke-compare deneb-printsvc-smoke-selftest deneb-printsvc-stability deneb-active-physical-soak-runner deneb-printsvc-stock-baseline deneb-printsvc-init-selftest deneb-printsvc-release-gate-selftest deneb-printsvc-native-audit deneb-printsvc-native-audit-selftest deneb-printsvc-integration-audit deneb-printsvc-integration-audit-selftest deneb-runtime-inventory deneb-printsvc.init digitalfactory.init update.sh manifest.txt; " +
                 "sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-smoke-selftest >/tmp/deneb-release-smoke-selftest.log; " +
                 "sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-stability --selftest >/tmp/deneb-release-stability-selftest.log; " +
                 "DENEB_REPO_ROOT='$repoWsl' sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-release-gate-selftest >/tmp/deneb-release-gate-selftest.log; " +
                 "sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-native-audit --archive '$packageWsl' >/tmp/deneb-release-native-audit.log; " +
                 "sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-native-audit-selftest >/tmp/deneb-release-native-audit-selftest.log; " +
                 "sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-integration-audit --archive '$packageWsl' >/tmp/deneb-release-integration-audit.log; " +
                 "sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-integration-audit-selftest >/tmp/deneb-release-integration-audit-selftest.log; " +
                 "DENEB_REPO_ROOT=/tmp/deneb-release-smoke-selftest DENEB_PRINTSVC_INIT=/tmp/deneb-release-smoke-selftest/deneb-printsvc.init DENEB_INSTALLER=/tmp/deneb-release-smoke-selftest/update.sh sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-init-selftest >/tmp/deneb-release-init-selftest.log; " +
                 "printf 'Verified native-only print service package: %s\n' '$packageWsl'"

& wsl -d $Distro -- bash -lc $verifyPackage
if ($LASTEXITCODE -ne 0) {
    throw "release package native print service verification failed with exit code $LASTEXITCODE"
}
