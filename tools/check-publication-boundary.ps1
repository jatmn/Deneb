# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$tracked = @(git -C $repoRoot ls-files)
if ($LASTEXITCODE -ne 0) { throw "git ls-files failed" }

$forbidden = $tracked | Where-Object {
    (Test-Path -LiteralPath (Join-Path $repoRoot $_)) -and (
        $_ -match '(?i)(^|/)(rootfs|extracted[-_]?firmware)(/|$)' -or
        $_ -match '(?i)\.(img|iso|qcow2|vdi|vmdk|key|pem|p12|pfx)$' -or
        $_ -eq 'docs/ultimaker-api-v1.json'
    )
}
if ($forbidden) {
    $forbidden | ForEach-Object { Write-Error "Forbidden publication artifact is tracked: $_" }
    exit 1
}

$historyOutput = @(git -C $repoRoot log --all --name-only --format=)
if ($LASTEXITCODE -ne 0) { throw "git history path scan failed" }
$historicalPaths = @($historyOutput | Where-Object { $_ } | Sort-Object -Unique)
$historicalAllowlist = @('docs/ultimaker-api-v1.json')
$unexpectedHistoricalArtifacts = $historicalPaths | Where-Object {
    $_ -notin $historicalAllowlist -and (
        $_ -match '(?i)(^|/)(rootfs|extracted[-_]?firmware)(/|$)' -or
        $_ -match '(?i)\.(img|iso|qcow2|vdi|vmdk|key|pem|p12|pfx|hex|deneb|tar|tar\.gz|zip)$'
    )
}
if ($unexpectedHistoricalArtifacts) {
    $unexpectedHistoricalArtifacts | ForEach-Object {
        Write-Error "Unexpected forbidden artifact exists in Git history: $_"
    }
    exit 1
}

$provenance = Get-Content -LiteralPath (Join-Path $repoRoot 'docs/SOURCE_PROVENANCE.md') -Raw
foreach ($allowedPath in $historicalAllowlist) {
    if ($historicalPaths -contains $allowedPath -and
        ($provenance -notmatch [regex]::Escape($allowedPath) -or
         $provenance -notmatch '77,019-byte Swagger')) {
        throw "Historical publication allowlist entry is not documented: $allowedPath"
    }
}

$license = Get-Content -LiteralPath (Join-Path $repoRoot 'LICENSE') -Raw
if ($license -notmatch 'Mozilla Public License Version 2\.0' -or
    $license -notmatch 'Exhibit B - .*Incompatible With Secondary Licenses.*Notice') {
    throw "Root LICENSE is not the complete canonical MPL-2.0 text"
}

$catalog = Get-Content -LiteralPath (Join-Path $repoRoot 'web/src/api_cluster_materials.h') -Raw
$records = [regex]::Matches($catalog, '\\"guid\\":\\"[0-9a-f-]+\\",\\"version\\":\d+')
if ($records.Count -ne 280) {
    throw "Expected 280 provenance-audited material records; found $($records.Count)"
}

& (Join-Path $PSScriptRoot 'check-markdown-links.ps1')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Output "Publication boundary: PASS"
