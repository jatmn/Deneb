# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [string]$Version = "0.1.0",
    [string]$OutputDirectory = "dist"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$packageDir = Join-Path $repoRoot "packages/ssh-bootstrap"
$distDir = Join-Path $repoRoot $OutputDirectory
$stagingRoot = Join-Path $repoRoot "build/ssh-bootstrap"
$stagingDir = Join-Path $stagingRoot "Deneb_SSH_Bootstrap_$Version"
$artifact = Join-Path $distDir "Deneb_SSH_Bootstrap_$Version.img"
$checksum = "$artifact.sha256"

function Write-LfFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Content
    )

    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($Path, $Content.Replace("`r`n", "`n").Replace("`r", "`n"), $utf8NoBom)
}

function Normalize-LfFile {
    param([Parameter(Mandatory = $true)][string]$Path)

    Write-LfFile -Path $Path -Content ([System.IO.File]::ReadAllText($Path))
}

if (!(Test-Path -LiteralPath $packageDir)) {
    throw "Package directory not found: $packageDir"
}

Remove-Item -LiteralPath $stagingRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $stagingDir | Out-Null
New-Item -ItemType Directory -Path $distDir -Force | Out-Null

Copy-Item -LiteralPath (Join-Path $packageDir "update.sh") -Destination (Join-Path $stagingDir "update.sh")
Copy-Item -LiteralPath (Join-Path $packageDir "README.md") -Destination (Join-Path $stagingDir "README.md")
Copy-Item -LiteralPath (Join-Path $packageDir "manifest.txt") -Destination (Join-Path $stagingDir "manifest.txt")

$manifestPath = Join-Path $stagingDir "manifest.txt"
$manifestContent = ([System.IO.File]::ReadAllText($manifestPath) -replace '(?m)^version=.*$', "version=$Version")
Write-LfFile -Path $manifestPath -Content $manifestContent

Normalize-LfFile -Path (Join-Path $stagingDir "update.sh")
Normalize-LfFile -Path (Join-Path $stagingDir "README.md")

Remove-Item -LiteralPath $artifact -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $checksum -Force -ErrorAction SilentlyContinue

Push-Location $stagingDir
try {
    & tar -cf $artifact update.sh README.md manifest.txt
    if ($LASTEXITCODE -ne 0) {
        throw "tar failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $artifact
Write-LfFile -Path $checksum -Content "$($hash.Hash.ToLowerInvariant())  $(Split-Path -Leaf $artifact)`n"

Write-Output "Built $artifact"
Write-Output "SHA256 $($hash.Hash.ToLowerInvariant())"
