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
