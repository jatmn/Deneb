# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [string]$Distro = "Debian",
    [string]$BuildDirectory = "build-wsl-host",
    [string]$BuildType = "Release",
    [string]$Screen = "all",
    [string]$OutputDirectory = "ui-shots",
    [string]$Lang = "en",
    [switch]$NoBuild
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

function Quote-Bash {
    param([Parameter(Mandatory = $true)][string]$Value)
    return "'" + $Value.Replace("'", "'\''") + "'"
}

if (-not $NoBuild) {
    & powershell -ExecutionPolicy Bypass -File "$PSScriptRoot\build-ui-host.ps1" `
        -Distro $Distro -BuildDirectory $BuildDirectory -BuildType $BuildType
    if ($LASTEXITCODE -ne 0) {
        throw "host UI build failed with exit code $LASTEXITCODE"
    }
}

$repoWsl = ConvertTo-WslPath $repoRoot
$outputPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDirectory))
$outputWsl = ConvertTo-WslPath $outputPath
$tempName = ".tmp-screen-ppm-$([Guid]::NewGuid().ToString('N'))"
$tempWsl = "$repoWsl/$tempName"

$screenArgs = ""
if ($Screen -and $Screen -ne "all") {
    $screenArgs = " --screenshot-screen $(Quote-Bash $Screen)"
}

$pythonSource = @'
import os
import struct
import sys
import zlib


def read_ppm(path):
    with open(path, "rb") as f:
        if f.readline().strip() != b"P6":
            raise ValueError(f"{path}: not a P6 PPM")
        line = f.readline()
        while line.startswith(b"#"):
            line = f.readline()
        width, height = map(int, line.split())
        maxval = int(f.readline().strip())
        if maxval != 255:
            raise ValueError(f"{path}: unsupported maxval {maxval}")
        data = f.read(width * height * 3)
    return width, height, data


def chunk(kind, data):
    return (
        struct.pack(">I", len(data)) +
        kind +
        data +
        struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    )


def write_png(path, width, height, rgb):
    raw = bytearray()
    row_len = width * 3
    for y in range(height):
        raw.append(0)
        raw.extend(rgb[y * row_len:(y + 1) * row_len])
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


src_dir, out_dir = sys.argv[1], sys.argv[2]
converted = 0
for name in sorted(os.listdir(src_dir)):
    if not name.endswith(".ppm"):
        continue
    src = os.path.join(src_dir, name)
    dst = os.path.join(out_dir, os.path.splitext(name)[0] + ".png")
    width, height, data = read_ppm(src)
    write_png(dst, width, height, data)
    converted += 1
    print(dst)

if converted == 0:
    raise SystemExit("no PPM screenshots were produced")
'@
$pythonBase64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($pythonSource))
$pythonRunner = "import base64; exec(base64.b64decode('$pythonBase64'))"

$command = @"
cd $(Quote-Bash $repoWsl) &&
rm -rf $(Quote-Bash $tempWsl) &&
mkdir -p $(Quote-Bash $tempWsl) $(Quote-Bash $outputWsl) &&
ui/$(Quote-Bash $BuildDirectory)/deneb-ui --lang $(Quote-Bash $Lang) --screenshot-dir $(Quote-Bash $tempWsl)$screenArgs &&
python3 -c $(Quote-Bash $pythonRunner) $(Quote-Bash $tempWsl) $(Quote-Bash $outputWsl)
rc=`$?
rm -rf $(Quote-Bash $tempWsl)
exit `$rc
"@
$command = $command -replace "`r`n", "`n"

& wsl -d $Distro -- bash -lc $command
if ($LASTEXITCODE -ne 0) {
    throw "screenshot capture failed with exit code $LASTEXITCODE"
}

Write-Host "PNG screenshots written to $outputPath"
