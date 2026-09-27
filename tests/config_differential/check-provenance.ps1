# Fails when a file provenance.txt lists no longer has the hash it records. SHA256 through
# .NET, because Get-FileHash is not found when a runner's pwsh runs this under Windows
# PowerShell.
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '../..')
$sha256 = [System.Security.Cryptography.SHA256]::Create()
foreach ($line in Get-Content (Join-Path $PSScriptRoot 'provenance.txt')) {
    if ($line -match '^\s*(#|$)') { continue }
    $hash, $path = ($line -split '\s+', 3)[0, 1]
    $bytes = [System.IO.File]::ReadAllBytes((Join-Path $root $path))
    $actual = -join ($sha256.ComputeHash($bytes) | ForEach-Object { $_.ToString('x2') })
    if ($actual -ne $hash) { throw "$path has changed: sha256 $actual, provenance.txt records $hash" }
}
Write-Host 'provenance.txt: every listed file has its recorded hash'
