#!/usr/bin/env pwsh
#Requires -Version 5.1
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo
#
# Builds two ZIPs in release/:
#   OutlastHeadTracking-v<version>-installer.zip  GitHub Releases (install.cmd + payload)
#   OutlastHeadTracking-v<version>-nexus.zip      drop-in, extracted over the game folder
#
# Runs unattended: no prompts, exit 0 on success, non-zero with a one-line
# diagnostic on any failure.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

Import-Module (Join-Path $projectRoot 'cameraunlock-core\powershell\ReleaseWorkflow.psm1') -Force

$modName = 'OutlastHeadTracking'

# Windows PowerShell 5.1's Compress-Archive writes the entry names with
# backslashes, which the ZIP format does not allow: Explorer and 7-Zip tolerate
# it, but `unzip` on a Linux box takes `plugins\dinput8.dll` as one flat
# filename and the extracted tree has no plugins directory at all. Anyone
# installing under Proton hits that, and .NET Framework's own CreateFromDirectory
# separates on the platform character too, so the entry names are built by hand.
Add-Type -AssemblyName System.IO.Compression.FileSystem
function New-ZipFromDirectory {
    param([string]$SourceDir, [string]$DestinationPath)
    if (Test-Path -LiteralPath $DestinationPath) { Remove-Item -LiteralPath $DestinationPath -Force }
    $root = (Resolve-Path -LiteralPath $SourceDir).Path.TrimEnd('\') + '\'
    $zip  = [System.IO.Compression.ZipFile]::Open($DestinationPath, 'Create')
    try {
        foreach ($file in Get-ChildItem -LiteralPath $SourceDir -Recurse -File) {
            $entryName = $file.FullName.Substring($root.Length).Replace('\', '/')
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $zip, $file.FullName, $entryName,
                [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally {
        $zip.Dispose()
    }
}

# CMakeLists.txt is the canonical version: it is what compiles into the DLL as
# HEADTRACKING_VERSION, and what .github/workflows/release.yml validates the
# pushed tag against (version-source: cmake).
$cmakePath = Join-Path $projectRoot 'CMakeLists.txt'
$version   = Get-ProjectVersion -Source 'cmake' -Path $cmakePath

# The other two copies of the version are hand-kept in sync by release.ps1.
# Checked here rather than trusted, because a wrong version reads exactly like a
# right one: pixi.toml is what `pixi run release` reports as current, and
# install.cmd's MOD_VERSION is what the install writes into the state file.
$pixiPath    = Join-Path $projectRoot 'pixi.toml'
$pixiVersion = Get-ProjectVersion -Source 'pixi' -Path $pixiPath
if ($pixiVersion -ne $version) {
    throw "Version drift: CMakeLists.txt=$version but pixi.toml=$pixiVersion. Bump both."
}

$installCmdPath = Join-Path $projectRoot 'scripts\install.cmd'
$installCmdText = Get-Content -LiteralPath $installCmdPath -Raw
if ($installCmdText -notmatch 'set "MOD_VERSION=([^"]+)"') {
    throw "No MOD_VERSION line in $installCmdPath, so the installed state file would record no version."
}
if ($matches[1] -ne $version) {
    throw "Version drift: CMakeLists.txt=$version but install.cmd MOD_VERSION=$($matches[1]). Bump both."
}

$modDll = Join-Path $projectRoot 'bin\Release\dinput8.dll'
if (-not (Test-Path $modDll)) {
    throw "Missing build output: $modDll. Run 'pixi run build-release' first."
}

$releaseDir = Join-Path $projectRoot 'release'
if (Test-Path $releaseDir) { Remove-Item -Recurse -Force $releaseDir }
New-Item -ItemType Directory -Path $releaseDir | Out-Null

$stagingRoot = Join-Path $releaseDir '_staging'
New-Item -ItemType Directory -Path $stagingRoot | Out-Null

# ---------------- Installer ZIP ----------------
$instStaging = Join-Path $stagingRoot 'installer'
New-Item -ItemType Directory -Path $instStaging | Out-Null

# Payload. install-body-shim.cmd copies plugins\* into the game's exe directory,
# which for Outlast is Binaries\Win64\. HeadTracking.ini is not seeded: the mod
# writes it there itself on first run, and shipping a copy would reset every key
# the user tuned on the next update.
$pluginsDir = Join-Path $instStaging 'plugins'
New-Item -ItemType Directory -Path $pluginsDir | Out-Null
Copy-Item -LiteralPath $modDll -Destination (Join-Path $pluginsDir 'dinput8.dll') -Force

# install.cmd and uninstall.cmd are thin wrappers: the body they call lives in
# shared/ at the ZIP root, and without it the installer aborts at its own layout
# check and exits 1 on every run. Copy-SharedBundle stages every body there,
# alongside find-game.ps1, GamePathDetection.psm1 and games.json at the paths
# find-game.ps1 actually looks in.
Copy-SharedBundle -StagingDir $instStaging

Copy-Item -LiteralPath $installCmdPath -Destination $instStaging -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'scripts\uninstall.cmd') -Destination $instStaging -Force

foreach ($doc in 'README.md', 'LICENSE', 'CHANGELOG.md', 'THIRD-PARTY-NOTICES.md') {
    Copy-Item -LiteralPath (Join-Path $projectRoot $doc) -Destination $instStaging -Force
}

$installerZip = Join-Path $releaseDir "$modName-v$version-installer.zip"
New-ZipFromDirectory -SourceDir $instStaging -DestinationPath $installerZip
Write-Host "Created: $installerZip" -ForegroundColor Green

# ---------------- Nexus ZIP ----------------
# Extracted over the game folder. The DLL sits under Binaries\Win64\ because that
# is where OLGame.exe is - the exe's own directory is what the loader searches,
# and a dinput8.dll at the install root is never looked at.
$nexusStaging = Join-Path $stagingRoot 'nexus'
$nexusPayload = Join-Path $nexusStaging 'Binaries\Win64'
New-Item -ItemType Directory -Path $nexusPayload -Force | Out-Null
Copy-Item -LiteralPath $modDll -Destination (Join-Path $nexusPayload 'dinput8.dll') -Force

# The Nexus ZIP is a binary distribution too. dinput8.dll statically links MinHook
# (BSD-2-Clause), whose binary-redistribution clause requires the copyright notice
# to travel with the binary, so the notices ship at this ZIP's root as well.
foreach ($noticeDoc in 'README.md', 'LICENSE', 'THIRD-PARTY-NOTICES.md') {
    Copy-Item -LiteralPath (Join-Path $projectRoot $noticeDoc) -Destination $nexusStaging -Force
}

$nexusZip = Join-Path $releaseDir "$modName-v$version-nexus.zip"
New-ZipFromDirectory -SourceDir $nexusStaging -DestinationPath $nexusZip
Write-Host "Created: $nexusZip" -ForegroundColor Green

Remove-Item -Recurse -Force $stagingRoot
