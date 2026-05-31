# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [string]$Distro = "Debian",
    [string]$BuildDirectory = "build-wsl-host",
    [string]$BuildType = "Release"
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

$command = "cd '$repoWsl/ui' && " +
           "mkdir -p '$BuildDirectory' && " +
           "cd '$BuildDirectory' && " +
           "cmake .. -DCMAKE_BUILD_TYPE=$BuildType -DBACKEND_COMM_STUB=ON && " +
           "make -j`$(nproc)"

& wsl -d $Distro -- bash -lc $command
if ($LASTEXITCODE -ne 0) {
    throw "host UI build failed with exit code $LASTEXITCODE"
}
