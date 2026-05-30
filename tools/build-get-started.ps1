# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [string]$Version = "0.2.4",
    [string]$OutputDirectory = "dist"
)

$ErrorActionPreference = "Stop"

& (Join-Path $PSScriptRoot "build-ssh-bootstrap.ps1") -Version $Version -OutputDirectory $OutputDirectory
