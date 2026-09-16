#!/usr/bin/env pwsh
#Requires -Version 5.1
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Dev-time deploy: the freshly built dinput8.dll -> every local Outlast install.

.DESCRIPTION
    The mod ships AS dinput8.dll. OLGame.exe statically imports DINPUT8.dll and
    searches its own directory before the system one, so the copy next to the exe
    is the entire loading mechanism - which is why this lands in Binaries\Win64\
    and not the install root. Invoke-DevDeployShim derives that directory per
    install from games.json's executable_relpath rather than joining it here.

.PARAMETER Configuration
    Debug or Release; selects which bin\<config>\dinput8.dll is copied.

.PARAMETER GamePath
    Install root to deploy into. Omitted, every copy of the game on this machine
    is a target - Steam, GOG, Epic and the rest can coexist, and writing to only
    the first one found leaves the copy the user actually launches on yesterday's
    build.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration,

    [Parameter(Position = 1)]
    [string]$GamePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

Import-Module (Join-Path $projectRoot 'cameraunlock-core\powershell\DevDeploy.psm1')

# Present in every build of this mod - it is the first line the mod logs, with the
# version appended - so it recognises a Debug shim left behind by yesterday's deploy
# as ours. Without it a rebuild gets captured as the user's pre-mod dinput8.dll and
# uninstall "restores" head tracking back into a game the user believes is clean.
$shimMarker = 'Outlast Head Tracking'

Invoke-DevDeployShim `
    -GameId 'outlast' `
    -GameDisplayName 'Outlast' `
    -BuildOutputPath (Join-Path $projectRoot "bin\$Configuration") `
    -ModDllName 'dinput8.dll' `
    -ShimMarker $shimMarker `
    -GivenPath $GamePath | Out-Null

Write-Host ''
Write-Host "Deployed the $Configuration build." -ForegroundColor Green
Write-Host 'HeadTracking.ini is written next to the game exe by the mod on first run.'
