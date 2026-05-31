# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [string]$Version = "0.2.8",
    [string]$OutputDirectory = "dist"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$packageDir = Join-Path $repoRoot "packages/ssh-bootstrap"
$brandingDir = Join-Path $repoRoot "assets/branding"
$distDir = Join-Path $repoRoot $OutputDirectory
$stagingRoot = Join-Path $repoRoot "build/ssh-bootstrap"
$stagingDir = Join-Path $stagingRoot "Deneb_get_started_$Version"
$artifact = Join-Path $distDir "Deneb_get_started.img"
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

function Get-PythonWithPillow {
    $candidates = @()

    $pyLauncher = (Get-Command py -ErrorAction SilentlyContinue).Source
    if ($pyLauncher) {
        $candidates += @{ Exe = $pyLauncher; Args = @("-3") }
    }

    foreach ($version in @("Python314", "Python313", "Python312", "Python311")) {
        $candidate = Join-Path $env:LOCALAPPDATA "Programs\Python\$version\python.exe"
        if (Test-Path -LiteralPath $candidate) {
            $candidates += @{ Exe = $candidate; Args = @() }
        }
    }

    $pathPython = (Get-Command python -ErrorAction SilentlyContinue).Source
    if ($pathPython) {
        $candidates += @{ Exe = $pathPython; Args = @() }
    }

    foreach ($candidate in $candidates) {
        $checkArgs = @($candidate.Args) + @("-c", "import PIL")
        & $candidate.Exe @checkArgs 2>$null
        if ($LASTEXITCODE -eq 0) {
            return $candidate
        }
    }

    throw "Python with Pillow is required. Install it with: py -3 -m pip install Pillow"
}

if (!(Test-Path -LiteralPath $packageDir)) {
    throw "Package directory not found: $packageDir"
}

if (!(Test-Path -LiteralPath $brandingDir)) {
    throw "Branding directory not found: $brandingDir"
}

Remove-Item -LiteralPath $stagingRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null
New-Item -ItemType Directory -Path $distDir -Force | Out-Null

Copy-Item -LiteralPath (Join-Path $packageDir "update.sh") -Destination (Join-Path $stagingDir "update.sh")
Copy-Item -LiteralPath (Join-Path $packageDir "README.md") -Destination (Join-Path $stagingDir "README.md")
Copy-Item -LiteralPath (Join-Path $packageDir "manifest.txt") -Destination (Join-Path $stagingDir "manifest.txt")
Copy-Item -LiteralPath (Join-Path $brandingDir "deneb-boot-320x240.png") -Destination (Join-Path $stagingDir "deneb-boot-320x240.png")
Copy-Item -LiteralPath (Join-Path $brandingDir "deneb-splash-128x102.jpg") -Destination (Join-Path $stagingDir "deneb-splash-128x102.jpg")

# Convert the 320x240 PNG splash to raw RGB565 for direct /dev/fb0 writes during early boot.
# The ILI9341 framebuffer is 320x240 RGB565 LE = 153,600 bytes.
$rgb565Script = Join-Path (Join-Path $repoRoot "tools") "png-to-rgb565.py"
$rgb565Output = Join-Path $stagingDir "deneb-splash.rgb565"
$pngSource = Join-Path $stagingDir "deneb-boot-320x240.png"
$python = Get-PythonWithPillow
& $python.Exe @($python.Args) $rgb565Script $pngSource $rgb565Output
if ($LASTEXITCODE -ne 0) {
    throw "png-to-rgb565 conversion failed with exit code $LASTEXITCODE"
}
if ((Get-Item $rgb565Output).Length -ne 153600) {
    throw "RGB565 output size mismatch: expected 153600 bytes, got $((Get-Item $rgb565Output).Length)"
}

$manifestPath = Join-Path $stagingDir "manifest.txt"
$manifestContent = ([System.IO.File]::ReadAllText($manifestPath) -replace '(?m)^version=.*$', "version=$Version")
Write-LfFile -Path $manifestPath -Content $manifestContent

Normalize-LfFile -Path (Join-Path $stagingDir "update.sh")
Normalize-LfFile -Path (Join-Path $stagingDir "README.md")

Remove-Item -LiteralPath $artifact -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $checksum -Force -ErrorAction SilentlyContinue

Push-Location $stagingDir
try {
    & tar -cf $artifact update.sh README.md manifest.txt deneb-boot-320x240.png deneb-splash-128x102.jpg deneb-splash.rgb565
    if ($LASTEXITCODE -ne 0) {
        throw "tar failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

$hash = (& certutil -hashfile $artifact SHA256)[1]
Write-LfFile -Path $checksum -Content "$($hash.ToLowerInvariant())  $(Split-Path -Leaf $artifact)`n"

Write-Output "Built $artifact"
Write-Output "SHA256 $($hash.ToLowerInvariant())"
