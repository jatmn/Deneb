# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$failures = [System.Collections.Generic.List[string]]::new()

Get-ChildItem -LiteralPath $repoRoot -Recurse -File -Filter *.md |
    Where-Object { $_.FullName -notmatch '[\\/](\.git|build[^\\/]*|dist)[\\/]' } |
    ForEach-Object {
        $file = $_
        $text = Get-Content -LiteralPath $file.FullName -Raw
        foreach ($match in [regex]::Matches($text, '(?<!\!)\[[^\]]+\]\(([^)]+)\)')) {
            $target = $match.Groups[1].Value.Trim()
            if ($target -match '^(https?://|mailto:|#)') { continue }
            $target = ($target -split '#', 2)[0]
            if ([string]::IsNullOrWhiteSpace($target)) { continue }
            $target = [uri]::UnescapeDataString($target.Trim('<', '>'))
            $resolved = Join-Path $file.DirectoryName $target
            if (-not (Test-Path -LiteralPath $resolved)) {
                $relative = [IO.Path]::GetRelativePath($repoRoot, $file.FullName)
                $failures.Add("${relative}: missing local link target '$target'")
            }
        }
    }

if ($failures.Count -gt 0) {
    $failures | Sort-Object | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Output "Markdown local links: PASS"
