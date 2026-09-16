#!/usr/bin/env pwsh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo
# Thin shim. Determine version, delegate to the shared publisher.
# See cameraunlock-core/powershell/NightlyRelease.psm1 for what it does.

[CmdletBinding()]
param(
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

# CMakeLists.txt is this mod's version source: it compiles into the DLL as
# HEADTRACKING_VERSION and package-release.ps1 checks pixi.toml and install.cmd
# against it. There is no src/core/constants.h here.
$cmakeFile = Join-Path $ProjectRoot 'CMakeLists.txt'
$versionMatch = Select-String -Path $cmakeFile -Pattern 'project\([^)]*?VERSION\s+(\d+\.\d+\.\d+)'
if (-not $versionMatch) {
    throw "Could not extract version from $cmakeFile"
}
$version = $versionMatch.Matches[0].Groups[1].Value

Publish-NightlyBuild `
    -ModId 'outlast' `
    -ModName 'OutlastHeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -AllowDirty:$AllowDirty
