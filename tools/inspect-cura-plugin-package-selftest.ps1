# SPDX-License-Identifier: MPL-2.0

$ErrorActionPreference = "Stop"
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$inspect = Join-Path $repoRoot "tools/inspect-cura-plugin-package.sh"
$bash = (Get-Command bash -ErrorAction Stop).Source
$tempRoot = if ($env:TMPDIR) { $env:TMPDIR } else { [System.IO.Path]::GetTempPath() }
$work = Join-Path $tempRoot ("cura-plugin-inspect-" + [guid]::NewGuid().ToString("n"))
New-Item -ItemType Directory -Force -Path $work | Out-Null

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Write-Zip {
    param(
        [string]$Path,
        [string[]]$Names,
        [string[]]$Bodies
    )
    if ($Names.Count -ne $Bodies.Count) {
        throw "zip entry count mismatch"
    }
    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Force
    }
    $zip = [System.IO.Compression.ZipFile]::Open($Path, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        for ($i = 0; $i -lt $Names.Count; $i++) {
            $entry = $zip.CreateEntry($Names[$i])
            $stream = $entry.Open()
            try {
                $bytes = [System.Text.Encoding]::UTF8.GetBytes($Bodies[$i])
                $stream.Write($bytes, 0, $bytes.Length)
            } finally {
                $stream.Dispose()
            }
        }
    } finally {
        $zip.Dispose()
    }
}

function Invoke-Inspect {
    param([string]$Path)
    $log = Join-Path $work "inspect.log"
    & $bash $inspect $Path > $log 2>&1
    return $LASTEXITCODE
}

function Assert-Pass {
    param([string]$Path)
    if ((Invoke-Inspect $Path) -ne 0) {
        throw "expected Cura package layout to pass: $Path"
    }
}

function Assert-Fail {
    param([string]$Path)
    if ((Invoke-Inspect $Path) -eq 0) {
        throw "expected Cura package layout to fail: $Path"
    }
}

$plugin = "files/plugins/DenebUM2CNetworkPrinting"
$meta = '{"package_id":"DenebUM2CNetworkPrinting","package_type":"plugin"}'
$goodNames = @(
    "package.json",
    "$plugin/__init__.py",
    "$plugin/plugin.json",
    "$plugin/resources/definitions/deneb_ultimaker2_plus_connect.def.json"
)
$goodBodies = @($meta, "init", "{}", "{}")

try {
    $good = Join-Path $work "good.curapackage"
    Write-Zip $good $goodNames $goodBodies
    Assert-Pass $good

    $missing = Join-Path $work "missing.curapackage"
    Write-Zip $missing @(
        "package.json",
        "$plugin/__init__.py",
        "$plugin/resources/definitions/deneb_ultimaker2_plus_connect.def.json"
    ) @($meta, "init", "{}")
    Assert-Fail $missing

    $cache = Join-Path $work "cache.curapackage"
    Write-Zip $cache ($goodNames + @("$plugin/__pycache__/x.pyc")) ($goodBodies + @("cache"))
    Assert-Fail $cache

    $nested = Join-Path $work "nested.curapackage"
    Write-Zip $nested ($goodNames + @("$plugin/package.json")) ($goodBodies + @($meta))
    Assert-Fail $nested

    $identity = Join-Path $work "identity.curapackage"
    Write-Zip $identity @("package.json", "$plugin/__init__.py", "$plugin/plugin.json", "$plugin/resources/definitions/deneb_ultimaker2_plus_connect.def.json") @(
        '{"package_id":"Other","package_type":"plugin"}',
        "init",
        "{}",
        "{}"
    )
    Assert-Fail $identity

    $slashes = Join-Path $work "slashes.curapackage"
    Write-Zip $slashes @(
        "package.json",
        "files\plugins\DenebUM2CNetworkPrinting\__init__.py",
        "files\plugins\DenebUM2CNetworkPrinting\plugin.json",
        "files\plugins\DenebUM2CNetworkPrinting\resources\definitions\deneb_ultimaker2_plus_connect.def.json"
    ) $goodBodies
    Assert-Fail $slashes
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force
}

Write-Output "Cura package layout self-test: PASS"
