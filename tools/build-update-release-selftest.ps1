# SPDX-License-Identifier: MPL-2.0
# Run with pwsh on Linux from a checkout with its pinned LVGL submodule.

$ErrorActionPreference = "Stop"
if (-not $IsLinux) {
    throw "This fixture uses native Linux bash in place of WSL."
}

$sourceRepo = Split-Path -Parent $PSScriptRoot
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) "deneb-wsl-release-$([guid]::NewGuid())"
$fixtureRepo = Join-Path $fixtureRoot "repo"

function wsl {
    # Run the real packager and archive checks. The placeholder binaries
    # replace the MIPS build steps, and the checkout is already a Linux path.
    $command = $args[-1]
    if ($command.Contains(' bash ui/build-package.sh ')) {
        $checkoutCommand = "; cd '$fixtureRepo'; "
        $packageStart = $command.LastIndexOf($checkoutCommand, [StringComparison]::Ordinal)
        if ($packageStart -lt 0) {
            throw "Expected the packager to run from the checkout."
        }
        $command = 'set -euo pipefail' + $command.Substring($packageStart)
    }
    elseif ($command.StartsWith('set -euo pipefail; test -s ')) {
        $command = $command.Replace('/tmp/deneb-release-', "$fixtureRoot/deneb-release-")
    }
    elseif (-not $command.StartsWith("bash '$fixtureRepo/tools/write-package-checksum.sh' ")) {
        $global:LASTEXITCODE = 0
        return
    }

    & bash -c "unset DENEB_PACKAGE_VERSION_OVERRIDE; $command"
}

function Assert-VerifiedPackage {
    param([string]$Version)

    $name = "Deneb_Update_$Version.deneb"
    $archive = Join-Path $fixtureRepo "dist/$name"
    $output = @(& "$fixtureRepo/tools/build-update-release.ps1" -LighttpdRoot "$fixtureRepo/lighttpd-fixture")
    if ($output[-1] -cne "Verified native-only print service package: $archive") {
        throw "The wrapper did not report the newly built archive: $($output[-1])"
    }
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash.ToLowerInvariant()
    if ((Get-Content -LiteralPath "$archive.sha256" -Raw).TrimEnd() -cne "$hash  $name") {
        throw "The sidecar does not name and hash the new archive: $archive.sha256"
    }
    return $hash
}

try {
    & git clone --quiet --no-local --no-tags --depth 1 -- $sourceRepo $fixtureRepo
    if ($LASTEXITCODE -ne 0) {
        throw "Could not prepare the package fixture checkout."
    }

    foreach ($entry in @(
        "ui/lib/lvgl/LICENCE.txt",
        "ui/lib/lvgl/src/stdlib/builtin/LICENSE_SPRINTF.txt",
        "ui/lib/lvgl/src/stdlib/builtin/LICENSE_TLSF.txt",
        "ui/lib/lvgl/src/libs/lodepng/lodepng.c"
    )) {
        $destination = Join-Path $fixtureRepo $entry
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath (Join-Path $sourceRepo $entry) -Destination $destination
    }
    foreach ($entry in @(
        "ui/build-musl/deneb-ui",
        "web/build-musl/deneb-api",
        "web/build-musl/deneb-mdns",
        "printsvc/build-musl/deneb-printsvc",
        "dfsvc/build-musl/deneb-dfsvc",
        "lighttpd-fixture/build-musl-static/build/lighttpd"
    )) {
        $path = Join-Path $fixtureRepo $entry
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
        Set-Content -LiteralPath $path -Value "Fixture binary: $entry"
    }

    # Keep both the staged wrapper's production logic and its script location.
    $wrapper = Get-Content -LiteralPath "$sourceRepo/tools/build-update-release.ps1" -Raw
    $wrapper = $wrapper -creplace '(?m)^\$repoWsl = ConvertTo-WslPath \$repoRoot\r?$', '$repoWsl = $repoRoot'
    Set-Content -LiteralPath "$fixtureRepo/tools/build-update-release.ps1" -Value $wrapper

    $short = (& git -C $fixtureRepo rev-parse --short HEAD).Trim()
    $shortHash = Assert-VerifiedPackage $short

    $tag = "release-handoff'fixture"
    & git -C $fixtureRepo -c tag.gpgSign=false tag $tag
    if ($LASTEXITCODE -ne 0) {
        throw "Could not tag the fixture checkout."
    }
    Set-Content -LiteralPath "$fixtureRepo/ui/build-musl/deneb-ui" -Value "New fixture binary"
    $tagHash = Assert-VerifiedPackage $tag
    if ($tagHash -ceq $shortHash) {
        throw "The tagged build selected the previous short-SHA archive."
    }
    $shortArchive = Join-Path $fixtureRepo "dist/Deneb_Update_$short.deneb"
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $shortArchive).Hash.ToLowerInvariant() -cne $shortHash -or
        (Get-Content -LiteralPath "$shortArchive.sha256" -Raw).TrimEnd() -cne "$shortHash  Deneb_Update_$short.deneb") {
        throw "The tagged build modified the unrelated short-SHA archive or sidecar."
    }

    Write-Output "Windows/WSL release filename and checksum fixture passed with and without a reachable tag."
}
finally {
    if (Test-Path -LiteralPath $fixtureRoot) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
    }
}
