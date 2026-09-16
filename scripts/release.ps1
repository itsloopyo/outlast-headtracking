#!/usr/bin/env pwsh
#Requires -Version 5.1
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Release workflow for Outlast Head Tracking.

.DESCRIPTION
    Runs end to end with no operator interaction - the command line is the
    consent, and the preconditions below (on main, clean tree, tag absent,
    semver valid) are the safety net:

      1. Resolve and validate the version.
      2. Verify branch, working tree and tag.
      3. Record the pinned cameraunlock-core commit in THIRD-PARTY-NOTICES.md.
      4. Regenerate CHANGELOG.md from the commits since the last tag.
      5. Bump CMakeLists.txt, pixi.toml and install.cmd's MOD_VERSION.
      6. Build and package (the packager re-checks those three agree).
      7. Commit "Release v<version>" and tag it.
      8. Push the branch and the tag; .github/workflows/release.yml takes over.

    Never destructive: no force push, no amend, no tag overwrite.

.PARAMETER Version
    Semver string (e.g. "1.0.0"), or major|minor|patch, or "nightly" to publish a
    dev build instead (dispatches to release-nightly.ps1 and returns).

.PARAMETER Force
    Ship a release even when there are no user-facing commits since the last tag
    (writes a maintenance changelog entry instead of aborting).

.EXAMPLE
    pixi run release minor
#>
param(
    [Parameter(Position = 0)]
    [string]$Version = '',
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot    = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$cmakePath      = Join-Path $projectRoot 'CMakeLists.txt'
$pixiPath       = Join-Path $projectRoot 'pixi.toml'
$installCmdPath = Join-Path $projectRoot 'scripts\install.cmd'
$changelogPath  = Join-Path $projectRoot 'CHANGELOG.md'

Import-Module (Join-Path $projectRoot 'cameraunlock-core\powershell\ReleaseWorkflow.psm1') -Force

# Mirrors New-ChangelogFromCommits' insertion so a -Force maintenance entry lands
# in the same place with the same shape.
function Add-MaintenanceChangelogEntry {
    param([string]$Path, [string]$NewVersion)
    $date  = Get-Date -Format 'yyyy-MM-dd'
    $entry = "## [$NewVersion] - $date`n`n### Changed`n`n- Maintenance release (no user-facing changes).`n`n"
    $changelog = Get-Content -LiteralPath $Path -Raw
    if ($changelog -match '(?s)(# Changelog.*?)(## \[)') {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n\n)', "`$1$entry"
    } else {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n)', "`$1$entry"
    }
    Set-Content -LiteralPath $Path -Value ($changelog.TrimEnd() + "`n") -NoNewline
}

Write-Host ''
Write-Host '=== Outlast Head Tracking Release ===' -ForegroundColor Cyan
Write-Host ''

# --- Step 1: resolve and validate the version -------------------------------
$current = Get-ProjectVersion -Source 'cmake' -Path $cmakePath

if ([string]::IsNullOrWhiteSpace($Version)) {
    Write-Host "Current version: $current" -ForegroundColor Yellow
    Write-Host 'Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>'
    exit 1
}

if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1')
    exit $LASTEXITCODE
}

try {
    $Version = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $current
} catch {
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
$tag = "v$Version"

# --- Step 2: branch, working tree, tag --------------------------------------
$branch = (& git -C $projectRoot rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne 'main') {
    Write-Host "ERROR: releases are cut from main (currently on '$branch')." -ForegroundColor Red
    exit 1
}
if (-not (Test-CleanGitStatus)) {
    Write-Host 'ERROR: working tree has uncommitted changes - commit or stash first.' -ForegroundColor Red
    & git -C $projectRoot status --short
    exit 1
}
if (Test-GitTagExists -Tag $tag) {
    Write-Host "ERROR: tag '$tag' already exists." -ForegroundColor Red
    exit 1
}

Write-Host "Current version: $current" -ForegroundColor Gray
Write-Host "New version:     $Version" -ForegroundColor Green
Write-Host ''

# --- Step 3: notices name the core commit this build compiles ---------------
# THIRD-PARTY-NOTICES.md ships at the root of both ZIPs, so the cameraunlock-core
# commit it names is the attribution the user receives. Bumping the submodule
# does not touch it, and packaging refuses to ship the mismatch - so re-sync it
# here, before a tag exists, rather than failing in CI after one is pushed.
& (Join-Path $projectRoot 'cameraunlock-core\scripts\sync-core-notices.ps1') -Repo $projectRoot
if ($LASTEXITCODE -ne 0) {
    Write-Host 'ERROR: sync-core-notices.ps1 failed - fix THIRD-PARTY-NOTICES.md before releasing.' -ForegroundColor Red
    exit 1
}
& git -C $projectRoot diff --quiet -- THIRD-PARTY-NOTICES.md
if ($LASTEXITCODE -ne 0) {
    & git -C $projectRoot commit -q -m 'chore: record the cameraunlock-core commit this build compiles' -- THIRD-PARTY-NOTICES.md
    if ($LASTEXITCODE -ne 0) {
        Write-Host 'ERROR: could not commit the re-synced THIRD-PARTY-NOTICES.md.' -ForegroundColor Red
        exit 1
    }
    Write-Host 'THIRD-PARTY-NOTICES.md re-synced to the pinned cameraunlock-core commit.' -ForegroundColor Yellow
}

# --- Step 4: changelog ------------------------------------------------------
# Generated before anything is mutated: this is the step that can legitimately
# abort (all commits filtered as noise), and aborting here leaves the working
# tree clean instead of stranding a half-applied version bump with no tag.
Write-Host 'Generating CHANGELOG from commits...' -ForegroundColor Cyan
$hasTags = & git -C $projectRoot tag -l
if (-not $hasTags) {
    $date = Get-Date -Format 'yyyy-MM-dd'
    Set-Content -LiteralPath $changelogPath -NoNewline -Value "# Changelog`n`n## [$Version] - $date`n`nFirst release.`n"
} else {
    try {
        New-ChangelogFromCommits -ChangelogPath $changelogPath -Version $Version -ArtifactPaths @('src/', 'cameraunlock-core', 'scripts/', 'CMakeLists.txt') | Out-Null
    } catch {
        if (-not $Force) {
            Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
            Write-Host 'No user-facing changes to release. Re-run with -Force for a maintenance release.' -ForegroundColor Yellow
            exit 1
        }
        Write-Host 'No user-facing commits since last tag - writing maintenance entry (-Force).' -ForegroundColor Yellow
        Add-MaintenanceChangelogEntry -Path $changelogPath -NewVersion $Version
    }
}

# --- Step 5: bump the three copies of the version ---------------------------
# CMakeLists.txt is canonical (it compiles into the DLL as HEADTRACKING_VERSION
# and release.yml validates the pushed tag against it); the other two are
# hand-kept in sync here and re-checked by package-release.ps1.
Write-Host "Updating CMakeLists.txt to $Version..." -ForegroundColor Cyan
$cmakeText = Get-Content -LiteralPath $cmakePath -Raw
if ($cmakeText -notmatch '(project\([^)]*?VERSION\s+)\d+\.\d+\.\d+') {
    Write-Host "ERROR: no project(... VERSION x.y.z ...) in $cmakePath." -ForegroundColor Red
    exit 1
}
Set-Content -LiteralPath $cmakePath -NoNewline -Value ($cmakeText -replace '(project\([^)]*?VERSION\s+)\d+\.\d+\.\d+', "`${1}$Version")

Write-Host "Updating pixi.toml to $Version..." -ForegroundColor Cyan
$pixiText = Get-Content -LiteralPath $pixiPath -Raw
if ($pixiText -notmatch '(?m)^version\s*=\s*"\d+\.\d+\.\d+"') {
    Write-Host "ERROR: no workspace version line in $pixiPath." -ForegroundColor Red
    exit 1
}
Set-Content -LiteralPath $pixiPath -NoNewline -Value ($pixiText -replace '(?m)^version\s*=\s*"\d+\.\d+\.\d+"', "version = `"$Version`"")

Write-Host "Updating install.cmd MOD_VERSION to $Version..." -ForegroundColor Cyan
$installText = Get-Content -LiteralPath $installCmdPath -Raw
if ($installText -notmatch 'set "MOD_VERSION=[^"]+"') {
    Write-Host "ERROR: no MOD_VERSION line in $installCmdPath." -ForegroundColor Red
    exit 1
}
# install.cmd is CRLF and must stay CRLF - a .cmd with LF line endings silently
# misbehaves on Windows. The -Raw read plus -NoNewline write preserves whatever
# endings the file already has instead of rewriting them.
Set-Content -LiteralPath $installCmdPath -NoNewline -Value ($installText -replace 'set "MOD_VERSION=[^"]+"', "set `"MOD_VERSION=$Version`"")

# --- Step 6: build and package ----------------------------------------------
Write-Host 'Building and packaging...' -ForegroundColor Cyan
Push-Location $projectRoot
try {
    & pixi run build-release
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    & pixi run package
    if ($LASTEXITCODE -ne 0) { throw 'Packaging failed' }
} finally {
    Pop-Location
}

# --- Step 7: commit and tag -------------------------------------------------
# Named files only, never `git add -A`: the build output and the release/ ZIPs
# sit in this tree and must not reach the commit.
Write-Host 'Committing version + changelog...' -ForegroundColor Cyan
& git -C $projectRoot add -- $cmakePath $pixiPath $installCmdPath $changelogPath
& git -C $projectRoot commit -m "Release v$Version"
if ($LASTEXITCODE -ne 0) {
    Write-Host 'ERROR: commit failed.' -ForegroundColor Red
    exit 1
}

Write-Host "Creating tag $tag..." -ForegroundColor Cyan
& git -C $projectRoot tag -a $tag -m "Release $tag"
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: could not create tag $tag." -ForegroundColor Red
    exit 1
}

# --- Step 8: push -----------------------------------------------------------
& git -C $projectRoot push origin main
if ($LASTEXITCODE -ne 0) {
    Write-Host 'ERROR: push of main failed. The tag exists locally; push it once main is pushed.' -ForegroundColor Red
    exit 1
}
& git -C $projectRoot push origin $tag
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: push of $tag failed." -ForegroundColor Red
    exit 1
}

Write-Host ''
Write-Host "Release $tag pushed - CI will build and publish the release artifacts." -ForegroundColor Green
