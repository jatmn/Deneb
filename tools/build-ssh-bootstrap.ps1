# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [string]$Version = "0.2.8",
    [string]$OutputDirectory = "dist"
)

$ErrorActionPreference = "Stop"

# Keep this in lockstep with tools/build-get-started.sh: first character must
# be alphanumeric so Linux and Windows builders accept the same tokens.
if ($Version -notmatch '^[A-Za-z0-9](?:[A-Za-z0-9._+-]*[A-Za-z0-9_+-])?$') {
    throw "Invalid -Version '$Version'. Use a token of letters, digits, '.', '_', '+', or '-' (for example 0.2.8)."
}

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

function Set-UnixExecuteMode {
    param([Parameter(Mandatory = $true)][string]$Path)

    $chmod = Get-Command chmod -ErrorAction SilentlyContinue
    if ($chmod) {
        & chmod 0755 $Path
        if ($LASTEXITCODE -ne 0) {
            throw "chmod 0755 failed for $Path"
        }
    }
}

function Get-UstarOctal {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][int]$Offset,
        [Parameter(Mandatory = $true)][int]$Length
    )

    $text = [System.Text.Encoding]::ASCII.GetString($Bytes, $Offset, $Length).Trim([char]0, [char]' ')
    if ([string]::IsNullOrEmpty($text)) {
        return [int64]0
    }

    return [Convert]::ToInt64($text, 8)
}

function Set-UstarMemberUnixMode {
    param(
        [Parameter(Mandatory = $true)][string]$ArchivePath,
        [Parameter(Mandatory = $true)][string]$MemberName,
        [Parameter(Mandatory = $true)][string]$OctalMode
    )

    $bytes = [System.IO.File]::ReadAllBytes($ArchivePath)
    $offset = 0
    $modeField = $OctalMode.PadLeft(7, '0').Substring(0, 7) + [char]0

    while ($offset + 512 -le $bytes.Length) {
        $allZero = $true
        for ($i = 0; $i -lt 512; $i++) {
            if ($bytes[$offset + $i] -ne 0) {
                $allZero = $false
                break
            }
        }
        if ($allZero) {
            break
        }

        $nameLength = 0
        while ($nameLength -lt 100 -and $bytes[$offset + $nameLength] -ne 0) {
            $nameLength++
        }
        $name = [System.Text.Encoding]::ASCII.GetString($bytes, $offset, $nameLength)
        if ($name -eq $MemberName) {
            $modeBytes = [System.Text.Encoding]::ASCII.GetBytes($modeField)
            [System.Buffer]::BlockCopy($modeBytes, 0, $bytes, $offset + 100, 8)
            for ($i = 0; $i -lt 8; $i++) {
                $bytes[$offset + 148 + $i] = 32
            }
            $sum = 0
            for ($i = 0; $i -lt 512; $i++) {
                $sum += $bytes[$offset + $i]
            }
            $checksumOctal = [Convert]::ToString($sum, 8)
            if ($checksumOctal.Length -gt 6) {
                throw "ustar checksum $checksumOctal does not fit the 6-digit checksum field"
            }
            $checksum = $checksumOctal.PadLeft(6, '0') + ([char]0) + ' '
            $checksumBytes = [System.Text.Encoding]::ASCII.GetBytes($checksum)
            [System.Buffer]::BlockCopy($checksumBytes, 0, $bytes, $offset + 148, 8)
            [System.IO.File]::WriteAllBytes($ArchivePath, $bytes)
            return
        }

        $size = Get-UstarOctal -Bytes $bytes -Offset ($offset + 124) -Length 12
        $padded = [int](([Math]::Floor(($size + 511) / 512)) * 512)
        $offset += 512 + $padded
    }

    throw "tar member '$MemberName' not found in $ArchivePath"
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
Set-UnixExecuteMode -Path (Join-Path $stagingDir "update.sh")

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

# Windows tar does not store Unix execute bits from NTFS. Force update.sh to
# 0755 in the ustar header so stock firmware can run /tmp/update/update.sh.
Set-UstarMemberUnixMode -ArchivePath $artifact -MemberName "update.sh" -OctalMode "755"

$hash = (& certutil -hashfile $artifact SHA256)[1]
Write-LfFile -Path $checksum -Content "$($hash.ToLowerInvariant())  $(Split-Path -Leaf $artifact)`n"

Write-Output "Built $artifact"
Write-Output "SHA256 $($hash.ToLowerInvariant())"
