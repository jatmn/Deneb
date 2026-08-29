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
$rgb565DigestFile = Join-Path $brandingDir "deneb-splash.rgb565.sha256"
$requirementsFile = Join-Path $repoRoot "tools/bootstrap-requirements.txt"
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
    if ($env:DENEB_BOOTSTRAP_PYTHON) {
        if (!(Test-Path -LiteralPath $env:DENEB_BOOTSTRAP_PYTHON -PathType Leaf)) {
            throw "DENEB_BOOTSTRAP_PYTHON does not exist: $env:DENEB_BOOTSTRAP_PYTHON"
        }
        $explicit = @{ Exe = $env:DENEB_BOOTSTRAP_PYTHON; Args = @() }
        & $explicit.Exe -c "import PIL, sys; sys.exit(PIL.__version__ != sys.argv[1])" $lockedPillowVersion 2>$null
        if ($LASTEXITCODE -ne 0) {
            throw "DENEB_BOOTSTRAP_PYTHON must provide locked Pillow $lockedPillowVersion"
        }
        return $explicit
    }

    $candidates = @()

    $pathPython = (Get-Command python -ErrorAction SilentlyContinue).Source
    if ($pathPython) {
        $candidates += @{ Exe = $pathPython; Args = @() }
    }

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

    foreach ($candidate in $candidates) {
        $checkArgs = @($candidate.Args) + @("-c", "import PIL, sys; sys.exit(PIL.__version__ != sys.argv[1])", $lockedPillowVersion)
        & $candidate.Exe @checkArgs 2>$null
        if ($LASTEXITCODE -eq 0) {
            return $candidate
        }
    }

    throw "Python with locked Pillow $lockedPillowVersion is required. Install tools/bootstrap-requirements.txt with pip --require-hashes."
}

if (!(Test-Path -LiteralPath $packageDir)) {
    throw "Package directory not found: $packageDir"
}

if (!(Test-Path -LiteralPath $brandingDir)) {
    throw "Branding directory not found: $brandingDir"
}

if (!(Test-Path -LiteralPath $rgb565DigestFile)) {
    throw "Missing expected RGB565 digest: $rgb565DigestFile"
}

if (!(Test-Path -LiteralPath $requirementsFile)) {
    throw "Missing bootstrap dependency lock: $requirementsFile"
}

$requirementsLine = Get-Content -LiteralPath $requirementsFile |
    Where-Object { $_ -match '^Pillow==([0-9]+(?:[.][0-9]+)+)\s' } |
    Select-Object -First 1
if (!$requirementsLine -or $requirementsLine -notmatch '^Pillow==([0-9]+(?:[.][0-9]+)+)\s') {
    throw "Could not read the locked Pillow version from $requirementsFile"
}
$lockedPillowVersion = $Matches[1]

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
$expectedRgbHash = ((Get-Content -LiteralPath $rgb565DigestFile | Select-Object -First 1) -split '\s+')[0].ToLowerInvariant()
if ($expectedRgbHash -notmatch '^[0-9a-f]{64}$') {
    throw "Invalid expected RGB565 SHA256 in $rgb565DigestFile"
}
$actualRgbHash = (Get-FileHash -LiteralPath $rgb565Output -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualRgbHash -ne $expectedRgbHash) {
    throw "RGB565 digest mismatch: expected $expectedRgbHash, got $actualRgbHash"
}

$manifestPath = Join-Path $stagingDir "manifest.txt"
$manifestContent = ([System.IO.File]::ReadAllText($manifestPath) -replace '(?m)^version=.*$', "version=$Version")
Write-LfFile -Path $manifestPath -Content $manifestContent

Normalize-LfFile -Path (Join-Path $stagingDir "update.sh")
Normalize-LfFile -Path (Join-Path $stagingDir "README.md")
Set-UnixExecuteMode -Path (Join-Path $stagingDir "update.sh")

$publishId = [Guid]::NewGuid().ToString("N")
$tempArtifact = Join-Path $distDir ".Deneb_get_started.img.$publishId.tmp"
$tempChecksum = Join-Path $distDir ".Deneb_get_started.img.sha256.$publishId.tmp"
$expectedMembers = @(
    "update.sh",
    "README.md",
    "manifest.txt",
    "deneb-boot-320x240.png",
    "deneb-splash-128x102.jpg",
    "deneb-splash.rgb565"
)

try {
    Push-Location $stagingDir
    try {
        & tar -cf $tempArtifact @expectedMembers
        if ($LASTEXITCODE -ne 0) {
            throw "tar failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }

    # Windows tar does not store Unix execute bits from NTFS. Force update.sh
    # to 0755 so stock firmware can run /tmp/update/update.sh.
    Set-UstarMemberUnixMode -ArchivePath $tempArtifact -MemberName "update.sh" -OctalMode "755"

    $actualMembers = @(& tar -tf $tempArtifact)
    if ($LASTEXITCODE -ne 0) {
        throw "tar validation failed with exit code $LASTEXITCODE"
    }
    if ((Compare-Object -ReferenceObject $expectedMembers -DifferenceObject $actualMembers -SyncWindow 0)) {
        throw "Bootstrap archive member validation failed"
    }

    $hash = (Get-FileHash -LiteralPath $tempArtifact -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -notmatch '^[0-9a-f]{64}$') {
        throw "Get-FileHash returned an invalid SHA256 digest"
    }
    Write-LfFile -Path $tempChecksum -Content "$hash  $(Split-Path -Leaf $artifact)`n"

    # The checksum is the publication marker. Keep it absent until the
    # validated image is at its final path so interruption cannot expose a
    # mismatched pair.
    Remove-Item -LiteralPath $checksum -Force -ErrorAction SilentlyContinue
    Move-Item -LiteralPath $tempArtifact -Destination $artifact -Force
    $tempArtifact = $null
    Move-Item -LiteralPath $tempChecksum -Destination $checksum -Force
    $tempChecksum = $null
}
finally {
    if ($tempArtifact -and (Test-Path -LiteralPath $tempArtifact)) {
        Remove-Item -LiteralPath $tempArtifact -Force
    }
    if ($tempChecksum -and (Test-Path -LiteralPath $tempChecksum)) {
        Remove-Item -LiteralPath $tempChecksum -Force
    }
}

Write-Output "Built $artifact"
Write-Output "SHA256 $hash"
