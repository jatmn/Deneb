# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [string]$Distro = "Debian",
    [string]$BuildDirectory = "build-musl-openwrt",
    [string]$WebBuildDirectory = "build-musl-openwrt",
    [string]$ZmqRoot = "",
    [string]$LighttpdRoot = "",
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
$buildPackageEvidenceArgs = ""
if (-not [string]::IsNullOrWhiteSpace($PrintsvcStockSummary)) {
    $printsvcStockSummaryWsl = ConvertTo-WslInputPath $PrintsvcStockSummary
}
if (-not [string]::IsNullOrWhiteSpace($PrintsvcNativeSummary)) {
    $printsvcNativeSummaryWsl = ConvertTo-WslInputPath $PrintsvcNativeSummary
}
foreach ($summary in $PrintsvcNativeEvidenceSummary) {
    if (-not [string]::IsNullOrWhiteSpace($summary)) {
        $summaryWsl = ConvertTo-WslInputPath $summary
        $buildPackageEvidenceArgs += " '$($summaryWsl.Replace("'", "'\''"))'"
    }
}

$buildPackageEnv = "DENEB_RELEASE_CHANNEL='$ReleaseChannel' DENEB_PACKAGE_PATH_ENCODING=base64"
if ($printsvcStockSummaryWsl) {
    $buildPackageEnv += " DENEB_PRINTSVC_STOCK_SUMMARY='$printsvcStockSummaryWsl'"
}
if ($printsvcNativeSummaryWsl) {
    $buildPackageEnv += " DENEB_PRINTSVC_NATIVE_SUMMARY='$printsvcNativeSummaryWsl'"
}
$loadPins = ". '$($repoWsl.Replace("'", "'\''"))/tools/native-dependency-pins.sh'"
# Keep the shell formatting in a script so Windows PowerShell can pass a plain path.
$pins = @(& wsl -d $Distro -u root --exec sh "$repoWsl/tools/print-native-dependency-pins.sh")
if ($LASTEXITCODE -ne 0 -or $pins.Count -ne 10) {
    throw "Could not read the native dependency pins in WSL."
}
$zmqVersion, $zmqUrl, $zmqSha256, $lighttpdVersion, $lighttpdUrl, $lighttpdSha256, $mbedtlsVersion, $toolchainRelease, $toolchainMember, $toolchainTarget = $pins
$buildRoot = "/root/deneb-build/openwrt-$toolchainRelease"
$toolchainRoot = "$buildRoot/$toolchainMember"
$toolchainCheck = "test -x '$toolchainRoot/bin/$toolchainTarget-gcc'"
& wsl -d $Distro -u root -- bash -lc $toolchainCheck
if ($LASTEXITCODE -ne 0) {
    throw "Missing OpenWrt musl cross-compiler. Run tools/setup-wsl-build.sh first."
}
$buildStart = "set -euo pipefail; export STAGING_DIR='$toolchainRoot'; "
$cmakeToolchainRoot = "-DMUSL_CROSS='$toolchainRoot'"
$checkCachedToolchain = "grep -Fxq 'MUSL_CROSS:PATH=$toolchainRoot' CMakeCache.txt"
$buildPackageEnv += " DENEB_PACKAGE_TOOLCHAIN_ROOT='$toolchainRoot' STRIP='$toolchainRoot/bin/$toolchainTarget-strip'"
if (!$ZmqRoot) {
    $ZmqRoot = "$buildRoot/zeromq-$zmqVersion"
}
if (!$LighttpdRoot) {
    $LighttpdRoot = "$buildRoot/lighttpd-$lighttpdVersion"
}
$mbedtlsRoot = "$buildRoot/mbedtls-$mbedtlsVersion-mipsel"
$zmqLib = "$ZmqRoot/build-musl/lib/libzmq.a"
$zmqInclude = "$ZmqRoot/include"
$lighttpdBinary = "$LighttpdRoot/build-musl-static/build/lighttpd"

if ($RebuildZmq) {
    if ($ZmqRoot -cnotmatch "/zeromq-$([regex]::Escape($zmqVersion))/?$") {
        throw "Refusing to rebuild libzmq outside a zeromq-$zmqVersion leaf directory: $ZmqRoot"
    }

    $buildZmq = $buildStart +
                "mkdir -p '$ZmqRoot'; " +
                "rm -rf '$ZmqRoot'/*; " +
                "curl --fail --location -o '$ZmqRoot.tar.gz' '$zmqUrl'; " +
                "printf '%s  %s\n' '$zmqSha256' '$ZmqRoot.tar.gz' | sha256sum -c -; " +
                "tar xzf '$ZmqRoot.tar.gz' -C '$ZmqRoot' --strip-components=1; " +
                "rm -f '$ZmqRoot.tar.gz'; " +
                "cd '$ZmqRoot'; mkdir build-musl; cd build-musl; " +
                "cmake .. -DCMAKE_TOOLCHAIN_FILE='$repoWsl/ui/cmake/mipsel-musl-toolchain.cmake' $cmakeToolchainRoot " +
                "-DCMAKE_BUILD_TYPE=MinSizeRel -DWITH_LIBSODIUM=OFF -DZMQ_BUILD_TESTS=OFF " +
                "-DWITH_DOCS=OFF -DBUILD_SHARED=OFF -DBUILD_STATIC=ON; " +
                "make -j`$(nproc); " +
                "$loadPins; record_native_dependency libzmq '$ZmqRoot'"
    & wsl -d $Distro -u root --exec bash -lc $buildZmq
    if ($LASTEXITCODE -ne 0) {
        throw "libzmq musl build failed with exit code $LASTEXITCODE"
    }
}

$zmqCheck = "test -f '$zmqLib' && test -d '$zmqInclude'"
& wsl -d $Distro -u root -- bash -lc $zmqCheck
if ($LASTEXITCODE -ne 0) {
    throw "Missing musl libzmq at $zmqLib. Re-run with -RebuildZmq."
}

if ($RebuildLighttpd) {
    if ($LighttpdRoot -cnotmatch "/lighttpd-$([regex]::Escape($lighttpdVersion))/?$") {
        throw "Refusing to rebuild lighttpd outside a lighttpd-$lighttpdVersion leaf directory: $LighttpdRoot"
    }

    $buildLighttpdSource = "set -euo pipefail; " +
                           "mkdir -p '$LighttpdRoot'; " +
                           "rm -rf '$LighttpdRoot'/*; " +
                           "curl --fail --location -o '$LighttpdRoot.tar.xz' '$lighttpdUrl'; " +
                           "printf '%s  %s\n' '$lighttpdSha256' '$LighttpdRoot.tar.xz' | sha256sum -c -; " +
                           "tar xf '$LighttpdRoot.tar.xz' -C '$LighttpdRoot' --strip-components=1; " +
                           "rm -f '$LighttpdRoot.tar.xz'"
    & wsl -d $Distro -u root --exec bash -lc $buildLighttpdSource
    if ($LASTEXITCODE -ne 0) {
        throw "lighttpd source fetch failed with exit code $LASTEXITCODE"
    }
}

$buildApi = $buildStart +
            "cd '$repoWsl/web'; " +
            "mkdir -p '$WebBuildDirectory'; " +
            "cd '$WebBuildDirectory'; " +
            "if [ -f CMakeCache.txt ]; then " +
            "$checkCachedToolchain || " +
            "{ echo 'Existing web build directory is not configured for mipsel musl. Use a different -WebBuildDirectory.' >&2; exit 1; }; " +
            "cmake .. -DCMAKE_BUILD_TYPE=MinSizeRel -DZMQ_STATIC_PATH='$zmqLib' -DZMQ_INCLUDE_DIR='$zmqInclude'; " +
            "else " +
            "cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/mipsel-musl-toolchain.cmake $cmakeToolchainRoot " +
            "-DCMAKE_BUILD_TYPE=MinSizeRel -DZMQ_STATIC_PATH='$zmqLib' -DZMQ_INCLUDE_DIR='$zmqInclude'; " +
            "fi; " +
            "make -j`$(nproc)"

& wsl -d $Distro -u root -- bash -lc $buildApi
if ($LASTEXITCODE -ne 0) {
    throw "release API build failed with exit code $LASTEXITCODE"
}

$buildPrintsvc = $buildStart +
                 "cd '$repoWsl/printsvc'; " +
                 "mkdir -p '$BuildDirectory'; " +
                 "cd '$BuildDirectory'; " +
                 "if [ -f CMakeCache.txt ]; then " +
                 "$checkCachedToolchain || " +
                 "{ echo 'Existing printsvc build directory is not configured for mipsel musl. Use a different -BuildDirectory.' >&2; exit 1; }; " +
                 "cmake .. -DCMAKE_BUILD_TYPE=MinSizeRel -DZMQ_STATIC_PATH='$zmqLib' -DZMQ_INCLUDE_DIR='$zmqInclude'; " +
                 "else " +
                 "cmake .. -DCMAKE_TOOLCHAIN_FILE='$repoWsl/ui/cmake/mipsel-musl-toolchain.cmake' $cmakeToolchainRoot " +
                 "-DCMAKE_BUILD_TYPE=MinSizeRel -DZMQ_STATIC_PATH='$zmqLib' -DZMQ_INCLUDE_DIR='$zmqInclude'; " +
                 "fi; " +
                 "make -j`$(nproc); " +
                 "test -x deneb-printsvc"

& wsl -d $Distro -u root -- bash -lc $buildPrintsvc
if ($LASTEXITCODE -ne 0) {
    throw "release print service build failed with exit code $LASTEXITCODE"
}

$buildLighttpd = $buildStart +
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
                 "if [ -f CMakeCache.txt ]; then " +
                 "$checkCachedToolchain || " +
                 "{ echo 'Existing lighttpd build directory is not configured for pinned mipsel musl. Rebuild with -RebuildLighttpd.' >&2; exit 1; }; " +
                 "else " +
                 "cmake .. -DCMAKE_TOOLCHAIN_FILE='$repoWsl/ui/cmake/mipsel-musl-toolchain.cmake' $cmakeToolchainRoot " +
                 "-DCMAKE_BUILD_TYPE=MinSizeRel -DBUILD_STATIC=ON -DWITH_PCRE=OFF -DWITH_PCRE2=OFF " +
                 "-DWITH_ZLIB=OFF -DWITH_BZIP=OFF -DWITH_BROTLI=OFF -DWITH_ZSTD=OFF " +
                 "-DWITH_OPENSSL=OFF -DWITH_MBEDTLS=OFF -DWITH_WOLFSSL=OFF -DWITH_GNUTLS=OFF; " +
                 "fi; " +
                 "make -j`$(nproc) lighttpd; " +
                 "test -x '$lighttpdBinary'"

if ($RebuildLighttpd) {
    $buildLighttpd += "; $loadPins; record_native_dependency lighttpd '$LighttpdRoot'"
}
& wsl -d $Distro -u root --exec bash -lc $buildLighttpd
if ($LASTEXITCODE -ne 0) {
    throw "release lighttpd build failed with exit code $LASTEXITCODE"
}

$buildUi = $buildStart +
           "cd '$repoWsl/dfsvc'; " +
           "mkdir -p '$BuildDirectory'; " +
           "cd '$BuildDirectory'; " +
           "if [ -f CMakeCache.txt ]; then " +
           "$checkCachedToolchain || " +
           "{ echo 'Existing dfsvc build directory is not configured for mipsel musl. Use a different -BuildDirectory.' >&2; exit 1; }; " +
           "cmake .. -DCMAKE_BUILD_TYPE=MinSizeRel -DZMQ_STATIC_PATH='$zmqLib' -DZMQ_INCLUDE_DIR='$zmqInclude' -DMBEDTLS_ROOT='$mbedtlsRoot'; " +
           "else " +
           "cmake .. -DCMAKE_TOOLCHAIN_FILE=../../ui/cmake/mipsel-musl-toolchain.cmake $cmakeToolchainRoot " +
           "-DCMAKE_BUILD_TYPE=MinSizeRel -DZMQ_STATIC_PATH='$zmqLib' -DZMQ_INCLUDE_DIR='$zmqInclude' -DMBEDTLS_ROOT='$mbedtlsRoot'; " +
           "fi; " +
           "make -j`$(nproc); " +
           "cd '$repoWsl/ui'; " +
           "mkdir -p '$BuildDirectory'; " +
           "cd '$BuildDirectory'; " +
           "if [ -f CMakeCache.txt ]; then " +
           "$checkCachedToolchain || " +
           "{ echo 'Existing build directory is not configured for mipsel musl. Use a different -BuildDirectory.' >&2; exit 1; }; " +
           "cmake .. -DCMAKE_BUILD_TYPE=MinSizeRel -DZMQ_LIBRARY='$zmqLib' -DZMQ_INCLUDE_DIR='$zmqInclude'; " +
           "else " +
           "cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/mipsel-musl-toolchain.cmake $cmakeToolchainRoot " +
           "-DCMAKE_BUILD_TYPE=MinSizeRel -DZMQ_LIBRARY='$zmqLib' -DZMQ_INCLUDE_DIR='$zmqInclude'; " +
           "fi; " +
           "make -j`$(nproc); " +
           "cd '$repoWsl'; " +
           "$buildPackageEnv bash ui/build-package.sh ui/$BuildDirectory/deneb-ui web/$WebBuildDirectory/deneb-api '$lighttpdBinary' web/$WebBuildDirectory/deneb-mdns printsvc/$BuildDirectory/deneb-printsvc dfsvc/$BuildDirectory/deneb-dfsvc${buildPackageEvidenceArgs}"

$packagePaths = @()
# The packager sends the UTF-8 path as ASCII so it can pass through any console code page.
$packagePathEncoding = [Text.UTF8Encoding]::new($false, $true)
# Start Bash directly so the default WSL shell cannot expand evidence filenames.
& wsl -d $Distro -u root --exec bash -lc $buildUi | ForEach-Object {
    if ($_ -cmatch '^Package path \(base64\): (.+)$') {
        $reportedPath = $packagePathEncoding.GetString([Convert]::FromBase64String($Matches[1]))
        $packagePaths += $reportedPath
        "Package: $reportedPath"
    }
    else {
        $_
    }
}
if ($LASTEXITCODE -ne 0) {
    throw "update release build failed with exit code $LASTEXITCODE"
}

$packagePattern = "^$([regex]::Escape($repoWsl))/dist/(Deneb_Update_[^/\\]+\.deneb)$"
if ($packagePaths.Count -ne 1 -or $packagePaths[0] -cnotmatch $packagePattern) {
    throw "update release build did not report exactly one Deneb archive in $repoWsl/dist"
}
$packageName = $Matches[1]
# The verification and checksum commands put this path inside Bash single quotes.
$packageWsl = $packagePaths[0].Replace("'", "'\''")

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
                 "grep -Eq '(^|/)deneb-printsvc-native-audit$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)deneb-printsvc-native-audit-selftest$' /tmp/deneb-release-package-files.txt; " +
                 "grep -Eq '(^|/)deneb-printsvc-integration-audit$' /tmp/deneb-release-package-files.txt; " +
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
                 "tar xf '$packageWsl' -C /tmp/deneb-release-smoke-selftest deneb-printsvc-smoke deneb-printsvc-smoke-verify deneb-printsvc-smoke-compare deneb-printsvc-smoke-selftest deneb-printsvc-stability deneb-active-physical-soak-runner deneb-printsvc-stock-baseline deneb-printsvc-init-selftest deneb-printsvc-native-audit deneb-printsvc-native-audit-selftest deneb-printsvc-integration-audit deneb-runtime-inventory deneb-printsvc.init digitalfactory.init update.sh manifest.txt; " +
                 "sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-smoke-selftest >/tmp/deneb-release-smoke-selftest.log; " +
                 "sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-stability --selftest >/tmp/deneb-release-stability-selftest.log; " +
                 "DENEB_REPO_ROOT='$repoWsl' sh '$repoWsl/tools/deneb-printsvc-release-gate-selftest.sh' >/tmp/deneb-release-gate-selftest.log; " +
                 "sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-native-audit --archive '$packageWsl' >/tmp/deneb-release-native-audit.log; " +
                 "sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-native-audit-selftest >/tmp/deneb-release-native-audit-selftest.log; " +
                 "sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-integration-audit --archive '$packageWsl' >/tmp/deneb-release-integration-audit.log; " +
                 "sh '$repoWsl/tools/deneb-printsvc-integration-audit-selftest.sh' >/tmp/deneb-release-integration-audit-selftest.log; " +
                 "DENEB_REPO_ROOT=/tmp/deneb-release-smoke-selftest DENEB_PRINTSVC_INIT=/tmp/deneb-release-smoke-selftest/deneb-printsvc.init DENEB_INSTALLER=/tmp/deneb-release-smoke-selftest/update.sh sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-init-selftest >/tmp/deneb-release-init-selftest.log"

& wsl -d $Distro -u root --exec bash -lc $verifyPackage
if ($LASTEXITCODE -ne 0) {
    throw "release package native print service verification failed with exit code $LASTEXITCODE"
}

# Win32 Git reads the checkout as its owner. WSL root need not trust the
# repositories on the mounted Windows drive.
$revision = & git -C $repoRoot rev-parse --verify HEAD
if ($LASTEXITCODE -ne 0 -or $revision -cnotmatch '^[0-9a-f]{40}$') {
    throw "Could not read the Deneb checkout revision."
}
$lvglPath = "ui/lib/lvgl"
$gitlink = & git -C $repoRoot ls-tree HEAD -- $lvglPath
if ($LASTEXITCODE -ne 0 -or $gitlink -cnotmatch '^160000 commit ([0-9a-f]{40})\tui/lib/lvgl$') {
    throw "Could not read the pinned LVGL revision."
}
$expectedLvgl = $Matches[1]
$lvglRevision = & git -C (Join-Path $repoRoot $lvglPath) rev-parse --verify HEAD
if ($LASTEXITCODE -ne 0 -or $lvglRevision -cne $expectedLvgl) {
    throw "LVGL checkout does not match the gitlink in HEAD. Initialize the pinned LVGL submodule."
}

& wsl -d $Distro -u root --exec sh "$repoWsl/tools/write-native-sbom.sh" ($packagePaths[0]) `
    $ZmqRoot $LighttpdRoot $mbedtlsRoot $toolchainRoot $revision $lvglRevision
if ($LASTEXITCODE -ne 0) {
    throw "release package SBOM generation failed with exit code $LASTEXITCODE"
}

$writeChecksum = "bash '$repoWsl/tools/write-package-checksum.sh' '$packageWsl'"
& wsl -d $Distro -u root --exec bash -lc $writeChecksum
if ($LASTEXITCODE -ne 0) {
    throw "release package checksum publication failed with exit code $LASTEXITCODE"
}

$package = Join-Path $repoRoot "dist/$packageName"
Write-Output "Verified native-only print service package: $package"
