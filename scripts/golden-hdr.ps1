#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Capture', 'Compare', 'Swap', 'Verify')]
    [string]$Mode = 'Verify',
    [ValidateSet('Debug', 'Release', 'Both')]
    [string]$Configuration = 'Debug',
    [string]$Candidate = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Step([string]$Message) {
    Write-Host "[RenderLab] $Message"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$goldenDir = Join-Path $repoRoot 'tests\golden-hdr\cesium-milk-truck\s04-default\1280x720'
$compareName = 'RenderLabGoldenCompare.exe'

function Get-Exe([string]$Config, [string]$Name) {
    $path = Join-Path $repoRoot "out\build\windows-vs2022\bin\$Config\$Name"
    if (-not (Test-Path $path)) {
        throw "Missing $path. Build $Config first: cmake --build --preset windows-$($Config.ToLowerInvariant())"
    }
    return $path
}

function Invoke-Capture([string]$Config, [string]$OutputDir) {
    $exe = Get-Exe $Config 'RenderLab.exe'
    Write-Step "Capturing $Config golden views to $OutputDir"
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    & $exe --headless --lock-camera --output-hdr $OutputDir
    if ($LASTEXITCODE -ne 0) {
        throw "RenderLab $Config --output-hdr exited $LASTEXITCODE."
    }

    $required = @(
        'hdr-scene-color.rlhdr',
        'lighting-lit.png',
        'final.png',
        'hdr-capture-metadata.json'
    )
    foreach ($name in $required) {
        $path = Join-Path $OutputDir $name
        if (-not (Test-Path $path)) {
            throw "Golden capture is missing $name under $OutputDir"
        }
    }
}

function Invoke-Native([string]$FilePath, [string[]]$ArgumentList) {
    $process = Start-Process -FilePath $FilePath -ArgumentList $ArgumentList -Wait -PassThru -NoNewWindow
    return [int]$process.ExitCode
}

function Invoke-Compare([string]$Config, [string]$CandidateDir) {
    $compare = Get-Exe $Config $compareName
    Write-Step "Comparing $CandidateDir against $goldenDir"
    return Invoke-Native $compare @('--mode', 'hdr', '--candidate', $CandidateDir, '--reference', $goldenDir)
}

function Invoke-ChannelSwap([string]$Config, [string]$CandidateDir) {
    $compare = Get-Exe $Config $compareName
    Write-Step "Proving a channel swap fails ($CandidateDir R/B swap of HDR)"
    $code = Invoke-Native $compare @('--mode', 'hdr', '--channel-swap', '--candidate', $CandidateDir, '--reference', $goldenDir)
    if ($code -ne 0) {
        throw "Channel-swap proof did not fail as expected (exit $code)."
    }
}

function Invoke-Verify([string]$Config) {
    if (-not (Test-Path (Join-Path $goldenDir 'hdr-scene-color.rlhdr'))) {
        throw "Approved goldens are missing under $goldenDir"
    }

    $run1 = Join-Path $repoRoot "results\s24-$($Config.ToLowerInvariant())-run1"
    $run2 = Join-Path $repoRoot "results\s24-$($Config.ToLowerInvariant())-run2"

    Invoke-Capture $Config $run1
    $exit1 = Invoke-Compare $Config $run1
    if ($exit1 -ne 0) {
        throw "Unchanged $Config capture run 1 did not pass (exit $exit1)."
    }

    Invoke-Capture $Config $run2
    $exit2 = Invoke-Compare $Config $run2
    if ($exit2 -ne 0) {
        throw "Unchanged $Config capture run 2 did not pass (exit $exit2)."
    }

    Invoke-ChannelSwap $Config $run1
    Write-Step "$Config golden verify passed (two captures + channel-swap fail)."
}

$configurations = if ($Configuration -eq 'Both') { @('Debug', 'Release') } else { @($Configuration) }

foreach ($config in $configurations) {
    switch ($Mode) {
        'Capture' {
            $output = if ($Candidate) { $Candidate } else { Join-Path $repoRoot "results\s24-$($config.ToLowerInvariant())" }
            Invoke-Capture $config $output
        }
        'Compare' {
            $candidateDir = if ($Candidate) { $Candidate } else { Join-Path $repoRoot "results\s24-$($config.ToLowerInvariant())" }
            $exit = Invoke-Compare $config $candidateDir
            if ($exit -ne 0) {
                throw "Golden compare exited $exit."
            }
        }
        'Swap' {
            $candidateDir = if ($Candidate) { $Candidate } else { Join-Path $repoRoot "results\s24-$($config.ToLowerInvariant())" }
            Invoke-ChannelSwap $config $candidateDir
        }
        'Verify' {
            Invoke-Verify $config
        }
    }
}

Write-Step "golden-hdr.ps1 $Mode passed."
