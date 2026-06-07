param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$pluginId = "DenebUM2CNetworkPrinting"
$pluginSource = Join-Path $repoRoot "cura\plugins\$pluginId"
$packageJson = Join-Path $pluginSource "package.json"
$distDir = Join-Path $repoRoot "dist"
$stageDir = Join-Path $distDir "cura-plugin-stage"
$packagePath = Join-Path $distDir "$pluginId.curapackage"
$zipPath = Join-Path $distDir "$pluginId.zip"

if (!(Test-Path -LiteralPath $packageJson)) {
    throw "Missing package metadata: $packageJson"
}

$metadata = Get-Content -LiteralPath $packageJson -Raw | ConvertFrom-Json
if ($metadata.package_id -ne $pluginId) {
    throw "package_id '$($metadata.package_id)' must match plugin id '$pluginId'"
}

New-Item -ItemType Directory -Force -Path $distDir | Out-Null
if (Test-Path -LiteralPath $stageDir) {
    Remove-Item -LiteralPath $stageDir -Recurse -Force
}
$stagedPluginDir = Join-Path $stageDir "files\plugins\$pluginId"
New-Item -ItemType Directory -Force -Path $stagedPluginDir | Out-Null

Copy-Item -LiteralPath $packageJson -Destination (Join-Path $stageDir "package.json")
Get-ChildItem -LiteralPath $pluginSource -Force |
    Where-Object { $_.Name -ne "package.json" -and $_.Name -ne "__pycache__" } |
    Copy-Item -Destination $stagedPluginDir -Recurse

$stagedPackageJson = Join-Path $stagedPluginDir "package.json"
if (Test-Path -LiteralPath $stagedPackageJson) {
    Remove-Item -LiteralPath $stagedPackageJson -Force
}

if (Test-Path -LiteralPath $packagePath) {
    Remove-Item -LiteralPath $packagePath -Force
}
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $zipPath -Force
Move-Item -LiteralPath $zipPath -Destination $packagePath
Remove-Item -LiteralPath $stageDir -Recurse -Force

Write-Host "Cura package: $packagePath"
