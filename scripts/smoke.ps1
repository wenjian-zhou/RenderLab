#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'Both')]
    [string]$Configuration = 'Both',
    [uint32]$Frames = 8
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Step([string]$Message) {
    Write-Host "[RenderLab] $Message"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$configurations = if ($Configuration -eq 'Both') { @('Debug', 'Release') } else { @($Configuration) }

foreach ($config in $configurations) {
    $exe = Join-Path $repoRoot "out\build\windows-vs2022\bin\$config\RenderLab.exe"
    if (-not (Test-Path $exe)) {
        throw "Missing $exe. Build $config first: cmake --build --preset windows-$($config.ToLowerInvariant())"
    }

    Write-Step "Running $config headless smoke ($Frames frames)..."
    & $exe --headless --frames $Frames
    if ($LASTEXITCODE -ne 0) {
        throw "RenderLab $config --headless exited $LASTEXITCODE."
    }
}

Write-Step 'Smoke passed.'
