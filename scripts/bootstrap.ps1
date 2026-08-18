#Requires -Version 5.1
[CmdletBinding()]
param(
    [switch]$SkipUpdate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Step([string]$Message) {
    Write-Host "[RenderLab] $Message"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$git = Get-Command git -ErrorAction SilentlyContinue
if (-not $git) {
    throw 'git was not found on PATH. Install Git for Windows and retry.'
}

$lockPath = Join-Path $repoRoot 'dependencies.lock.json'
if (-not (Test-Path $lockPath)) {
    throw "Missing lock file: $lockPath"
}

if (-not $SkipUpdate) {
    Write-Step 'Initializing recursive Git submodules...'
    & git submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) {
        throw "git submodule update --init --recursive failed with exit code $LASTEXITCODE."
    }
}

$lock = Get-Content -Raw $lockPath | ConvertFrom-Json
$expected = @{}
foreach ($dependency in $lock.dependencies) {
    if ($dependency.acquisition -in @('git-submodule', 'donut-git-submodule') -and $dependency.intendedPath -and $dependency.commit) {
        $expected[$dependency.intendedPath.Replace('\', '/')] = [string]$dependency.commit
    }
}

if ($expected.Count -eq 0) {
    throw 'dependencies.lock.json does not contain any git submodule pins.'
}

foreach ($path in ($expected.Keys | Sort-Object)) {
    $fullPath = Join-Path $repoRoot $path
    if (-not (Test-Path (Join-Path $fullPath '.git')) -and -not (Test-Path $fullPath)) {
        throw "Submodule path is missing: $path. Re-run without -SkipUpdate."
    }

    $actual = (& git -C $fullPath rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to read HEAD for $path."
    }
    if ($actual -ne $expected[$path]) {
        throw "Submodule $path is $actual, expected $($expected[$path])."
    }
    Write-Step "$path = $actual"
}

Write-Step 'Checking git submodule status --recursive...'
$status = & git submodule status --recursive
if ($LASTEXITCODE -ne 0) {
    throw "git submodule status --recursive failed with exit code $LASTEXITCODE."
}

$dirty = @()
foreach ($line in $status) {
    if ([string]::IsNullOrWhiteSpace($line)) {
        continue
    }
    $prefix = $line.Substring(0, 1)
    if ($prefix -eq '-' -or $prefix -eq '+') {
        $dirty += $line
    }
    Write-Host $line
}

if ($dirty.Count -gt 0) {
    throw "Submodule status contains uninitialized (-) or drifted (+) entries:`n$($dirty -join "`n")"
}

Write-Step 'Donut/NVRHI recursive submodules match dependencies.lock.json.'
exit 0
