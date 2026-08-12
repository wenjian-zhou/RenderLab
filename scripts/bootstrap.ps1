[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Set-StrictMode -Version Latest

if ($env:OS -ne 'Windows_NT') {
    throw 'RenderLab bootstrap requires Windows.'
}

$script:RepositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$script:ExternalRoot = Join-Path $script:RepositoryRoot 'external'
$script:NuGetRoot = Join-Path $script:ExternalRoot 'nuget'
$script:VcpkgRoot = Join-Path $script:ExternalRoot 'vcpkg'
$script:OutRoot = Join-Path $script:RepositoryRoot 'out'
$script:VcpkgRelease = '2026.07.29'
$script:VcpkgRepository = 'https://github.com/microsoft/vcpkg.git'
$script:VcpkgTriplet = 'x64-windows-static-md'

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [Parameter()]
        [string[]]$Arguments = @()
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $FilePath $($Arguments -join ' ')"
    }
}

function Assert-RequiredFile {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required dependency file is missing: $Path"
    }
}

function Get-NuspecMetadata {
    param([Parameter(Mandatory = $true)][string]$PackageRoot)

    $nuspecs = @(Get-ChildItem -LiteralPath $PackageRoot -Filter '*.nuspec' -File)
    if ($nuspecs.Count -ne 1) {
        throw "Expected exactly one nuspec in $PackageRoot; found $($nuspecs.Count)."
    }

    [xml]$document = Get-Content -LiteralPath $nuspecs[0].FullName -Raw -Encoding UTF8
    $idNode = $document.SelectSingleNode("/*[local-name()='package']/*[local-name()='metadata']/*[local-name()='id']")
    $versionNode = $document.SelectSingleNode("/*[local-name()='package']/*[local-name()='metadata']/*[local-name()='version']")
    if ($null -eq $idNode -or $null -eq $versionNode) {
        throw "The package metadata is incomplete: $($nuspecs[0].FullName)"
    }

    return [pscustomobject]@{
        Id = $idNode.InnerText
        Version = $versionNode.InnerText
    }
}

function Get-PackageDefinition {
    param([Parameter(Mandatory = $true)][string]$Name)

    switch ($Name) {
        'agility-sdk' {
            return [pscustomobject]@{
                RequiredFiles = @(
                    'build/native/include/d3d12.h',
                    'build/native/include/d3d12sdklayers.h',
                    'build/native/bin/x64/D3D12Core.dll',
                    'build/native/bin/x64/d3d12SDKLayers.dll'
                )
            }
        }
        'dxc' {
            return [pscustomobject]@{
                RequiredFiles = @(
                    'build/native/include/dxcapi.h',
                    'build/native/lib/x64/dxcompiler.lib',
                    'build/native/bin/x64/dxc.exe',
                    'build/native/bin/x64/dxcompiler.dll',
                    'build/native/bin/x64/dxil.dll'
                )
            }
        }
        'pix-runtime' {
            return [pscustomobject]@{
                RequiredFiles = @(
                    'Include/WinPixEventRuntime/pix3.h',
                    'bin/x64/WinPixEventRuntime.lib',
                    'bin/x64/WinPixEventRuntime.dll'
                )
            }
        }
        default {
            throw "dependencies.lock.json contains an unsupported package name: $Name"
        }
    }
}

function Test-VersionPrefix {
    param(
        [Parameter(Mandatory = $true)][string]$Actual,
        [Parameter(Mandatory = $true)][string]$Expected,
        [Parameter(Mandatory = $true)][int]$Components
    )

    $expectedParts = $Expected.Split('.')
    if ($expectedParts.Count -lt $Components) {
        return $false
    }

    $prefix = ($expectedParts[0..($Components - 1)] -join '.')
    return $Actual -match ('^' + [regex]::Escape($prefix) + '([.+-]|$)')
}

function Assert-Package {
    param(
        [Parameter(Mandatory = $true)]$LockEntry,
        [Parameter(Mandatory = $true)][string]$PackageRoot
    )

    $metadata = Get-NuspecMetadata -PackageRoot $PackageRoot
    if (-not [string]::Equals($metadata.Id, [string]$LockEntry.id,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Package ID mismatch in $PackageRoot`: expected $($LockEntry.id), found $($metadata.Id)."
    }
    if ($metadata.Version -ne [string]$LockEntry.version) {
        throw "Package version mismatch in $PackageRoot`: expected $($LockEntry.version), found $($metadata.Version)."
    }

    $definition = Get-PackageDefinition -Name ([string]$LockEntry.name)
    foreach ($relativePath in $definition.RequiredFiles) {
        Assert-RequiredFile -Path (Join-Path $PackageRoot $relativePath)
    }

    $prefixComponents = if ([string]$LockEntry.name -eq 'dxc') { 2 } else { 3 }
    $versionedFiles = @($definition.RequiredFiles | Where-Object {
        [System.IO.Path]::GetExtension($_) -in @('.dll', '.exe')
    })
    foreach ($relativePath in $versionedFiles) {
        $binaryInfo = [System.Diagnostics.FileVersionInfo]::GetVersionInfo(
            (Join-Path $PackageRoot $relativePath))
        if ([string]::IsNullOrWhiteSpace($binaryInfo.ProductVersion)) {
            throw "The package binary has no product version: $relativePath"
        }
        if (-not (Test-VersionPrefix -Actual $binaryInfo.ProductVersion `
                -Expected ([string]$LockEntry.version) -Components $prefixComponents)) {
            throw "Binary version '$($binaryInfo.ProductVersion)' does not match locked package $($LockEntry.version): $relativePath"
        }
    }
}

function Get-FileRecord {
    param(
        [Parameter(Mandatory = $true)][string]$PackageRoot,
        [Parameter(Mandatory = $true)][string]$RelativePath
    )

    $path = Join-Path $PackageRoot $RelativePath
    $record = [ordered]@{
        path = $RelativePath.Replace('\', '/')
        sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    }

    if ([System.IO.Path]::GetExtension($path) -in @('.dll', '.exe')) {
        $versionInfo = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($path)
        $record.fileVersion = $versionInfo.FileVersion
        $record.productVersion = $versionInfo.ProductVersion
    }

    return $record
}

function Assert-NuGetClient {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$LockedVersion
    )

    Assert-RequiredFile -Path $Path
    $versionInfo = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
    if (-not (Test-VersionPrefix -Actual $versionInfo.ProductVersion -Expected $LockedVersion -Components 3)) {
        throw "NuGet CLI version mismatch: expected $LockedVersion, found $($versionInfo.ProductVersion)."
    }

    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
        throw "NuGet CLI Authenticode signature is not valid: $($signature.Status)."
    }
}

function Install-NuGetClient {
    param([Parameter(Mandatory = $true)]$ClientLock)

    $lockedVersion = [string]$ClientLock.version
    if ([string]::IsNullOrWhiteSpace($lockedVersion) -or $lockedVersion.Contains('*')) {
        throw 'The NuGet CLI version must be exact.'
    }

    $toolsRoot = Join-Path $script:NuGetRoot '.tools'
    New-Item -ItemType Directory -Path $toolsRoot -Force | Out-Null
    $nugetPath = Join-Path $toolsRoot 'nuget.exe'

    if (Test-Path -LiteralPath $nugetPath) {
        Assert-NuGetClient -Path $nugetPath -LockedVersion $lockedVersion
        Write-Host "[bootstrap] Reusing NuGet CLI $lockedVersion."
        return $nugetPath
    }

    $downloadPath = "$nugetPath.$PID.download"
    $downloadUrl = "https://dist.nuget.org/win-x86-commandline/v$lockedVersion/nuget.exe"
    try {
        Write-Host "[bootstrap] Downloading NuGet CLI $lockedVersion..."
        Invoke-WebRequest -UseBasicParsing -Uri $downloadUrl -OutFile $downloadPath
        Assert-NuGetClient -Path $downloadPath -LockedVersion $lockedVersion
        Move-Item -LiteralPath $downloadPath -Destination $nugetPath
    }
    finally {
        if (Test-Path -LiteralPath $downloadPath) {
            Remove-Item -LiteralPath $downloadPath -Force
        }
    }

    return $nugetPath
}

function Install-NuGetPackage {
    param(
        [Parameter(Mandatory = $true)][string]$NuGetPath,
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)]$LockEntry
    )

    $name = [string]$LockEntry.name
    $id = [string]$LockEntry.id
    $version = [string]$LockEntry.version
    if ([string]::IsNullOrWhiteSpace($name) -or [string]::IsNullOrWhiteSpace($id) -or
        [string]::IsNullOrWhiteSpace($version) -or $version.Contains('*')) {
        throw 'Every NuGet package lock entry must contain a logical name, package ID, and exact version.'
    }

    $destination = Join-Path $script:NuGetRoot $name
    if (Test-Path -LiteralPath $destination) {
        Assert-Package -LockEntry $LockEntry -PackageRoot $destination
        Write-Host "[bootstrap] Reusing $id $version."
        return
    }

    $stagingRoot = Join-Path $script:NuGetRoot ('.staging-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stagingRoot | Out-Null
    try {
        Write-Host "[bootstrap] Installing $id $version..."
        Invoke-NativeCommand -FilePath $NuGetPath -Arguments @(
            'install', $id,
            '-Version', $version,
            '-Source', $Source,
            '-OutputDirectory', $stagingRoot,
            '-ExcludeVersion',
            '-NonInteractive',
            '-DirectDownload',
            '-NoHttpCache',
            '-PackageSaveMode', 'nuspec;nupkg'
        )

        $packageRoots = @(Get-ChildItem -LiteralPath $stagingRoot -Directory)
        if ($packageRoots.Count -ne 1) {
            throw "NuGet produced $($packageRoots.Count) package directories for $id; expected one."
        }

        Assert-Package -LockEntry $LockEntry -PackageRoot $packageRoots[0].FullName
        Move-Item -LiteralPath $packageRoots[0].FullName -Destination $destination
    }
    finally {
        if (Test-Path -LiteralPath $stagingRoot) {
            Remove-Item -LiteralPath $stagingRoot -Recurse -Force
        }
    }
}

function Install-Vcpkg {
    param([Parameter(Mandatory = $true)][string]$Baseline)

    if ([string]::IsNullOrWhiteSpace($Baseline) -or $Baseline -notmatch '^[0-9a-f]{40}$') {
        throw 'vcpkg.json must contain a full 40-character builtin-baseline.'
    }

    if (-not (Test-Path -LiteralPath $script:VcpkgRoot)) {
        Write-Host "[bootstrap] Cloning vcpkg $script:VcpkgRelease..."
        $cloneRoot = Join-Path $script:OutRoot ('vcpkg-clone-' + [guid]::NewGuid().ToString('N'))
        try {
            Invoke-NativeCommand -FilePath 'git.exe' -Arguments @(
                'clone', '--branch', $script:VcpkgRelease, '--depth', '1',
                $script:VcpkgRepository, $cloneRoot
            ) | Out-Host

            $cloneHead = (& git.exe -C $cloneRoot rev-parse HEAD).Trim()
            if ($LASTEXITCODE -ne 0 -or $cloneHead -ne $Baseline) {
                throw "The downloaded vcpkg tag resolved to $cloneHead instead of $Baseline."
            }
            Move-Item -LiteralPath $cloneRoot -Destination $script:VcpkgRoot
        }
        finally {
            if (Test-Path -LiteralPath $cloneRoot) {
                Remove-Item -LiteralPath $cloneRoot -Recurse -Force
            }
        }
    }

    if (-not (Test-Path -LiteralPath (Join-Path $script:VcpkgRoot '.git'))) {
        throw "$script:VcpkgRoot exists but is not a vcpkg Git checkout; preserving it unchanged."
    }

    $head = (& git.exe -C $script:VcpkgRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $head -ne $Baseline) {
        throw "vcpkg checkout mismatch: expected $Baseline, found $head. The existing directory was not modified."
    }

    $tagCommit = (& git.exe -C $script:VcpkgRoot rev-parse "refs/tags/$script:VcpkgRelease^{commit}").Trim()
    if ($LASTEXITCODE -ne 0 -or $tagCommit -ne $Baseline) {
        throw "vcpkg tag $script:VcpkgRelease does not resolve to locked baseline $Baseline."
    }

    $checkoutChanges = @(& git.exe -C $script:VcpkgRoot status --short)
    if ($LASTEXITCODE -ne 0 -or $checkoutChanges.Count -ne 0) {
        throw 'The repository-owned vcpkg checkout has local changes; preserving it unchanged.'
    }

    $vcpkgPath = Join-Path $script:VcpkgRoot 'vcpkg.exe'
    if (-not (Test-Path -LiteralPath $vcpkgPath)) {
        Write-Host '[bootstrap] Building the locked vcpkg client...'
        Invoke-NativeCommand -FilePath (Join-Path $script:VcpkgRoot 'bootstrap-vcpkg.bat') `
            -Arguments @('-disableMetrics') | Out-Host
    }

    $versionOutput = (& $vcpkgPath version 2>&1 | Out-String).Trim()
    $toolMetadataPath = Join-Path $script:VcpkgRoot 'scripts/vcpkg-tool-metadata.txt'
    Assert-RequiredFile -Path $toolMetadataPath
    $toolReleaseLine = Get-Content -LiteralPath $toolMetadataPath -Encoding UTF8 |
        Where-Object { $_ -match '^VCPKG_TOOL_RELEASE_TAG=' } |
        Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($toolReleaseLine)) {
        throw "The locked checkout has no VCPKG_TOOL_RELEASE_TAG: $toolMetadataPath"
    }
    $toolRelease = ($toolReleaseLine -split '=', 2)[1]
    $expectedToolPattern = 'version\s+' + [regex]::Escape($toolRelease) + '-[0-9a-f]{40}'
    if ($LASTEXITCODE -ne 0 -or $versionOutput -notmatch $expectedToolPattern) {
        throw "The vcpkg executable does not match tool release $toolRelease from the locked checkout: $versionOutput"
    }
    $versionLine = ($versionOutput -split '\r?\n', 2)[0].Trim()

    $downloadsRoot = Join-Path $script:OutRoot 'vcpkg-downloads'
    $binaryCacheRoot = Join-Path $script:OutRoot 'vcpkg-binary-cache'
    $registriesRoot = Join-Path $script:OutRoot 'vcpkg-registries'
    $installedRoot = Join-Path $script:RepositoryRoot 'vcpkg_installed'
    foreach ($directory in @($downloadsRoot, $binaryCacheRoot, $registriesRoot, $installedRoot)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }

    $env:VCPKG_ROOT = $script:VcpkgRoot
    $env:VCPKG_DOWNLOADS = $downloadsRoot
    $env:VCPKG_BINARY_SOURCES = "clear;files,$binaryCacheRoot,readwrite"
    $env:VCPKG_DEFAULT_BINARY_CACHE = $binaryCacheRoot
    $env:X_VCPKG_REGISTRIES_CACHE = $registriesRoot
    $env:VCPKG_DISABLE_METRICS = '1'

    Write-Host "[bootstrap] Installing the vcpkg manifest with $script:VcpkgTriplet..."
    Invoke-NativeCommand -FilePath $vcpkgPath -Arguments @(
        'install',
        "--x-manifest-root=$script:RepositoryRoot",
        "--x-install-root=$installedRoot",
        "--triplet=$script:VcpkgTriplet",
        "--host-triplet=$script:VcpkgTriplet",
        "--overlay-triplets=$(Join-Path $script:RepositoryRoot 'triplets')"
    ) | Out-Host

    return [pscustomobject]@{
        Path = $vcpkgPath
        VersionOutput = $versionLine
    }
}

function Write-VersionsFile {
    param(
        [Parameter(Mandatory = $true)]$Lock,
        [Parameter(Mandatory = $true)][string]$Baseline,
        [Parameter(Mandatory = $true)]$VcpkgInfo,
        [Parameter(Mandatory = $true)][string]$NuGetPath
    )

    $packageRecords = @()
    foreach ($package in $Lock.nuget.packages) {
        $packageRoot = Join-Path $script:NuGetRoot ([string]$package.name)
        Assert-Package -LockEntry $package -PackageRoot $packageRoot
        $metadata = Get-NuspecMetadata -PackageRoot $packageRoot
        $definition = Get-PackageDefinition -Name ([string]$package.name)
        $fileRecords = @()
        foreach ($relativePath in $definition.RequiredFiles) {
            $fileRecords += Get-FileRecord -PackageRoot $packageRoot -RelativePath $relativePath
        }

        $packageRecords += [ordered]@{
            name = [string]$package.name
            id = [string]$package.id
            lockedVersion = [string]$package.version
            actualVersion = $metadata.Version
            files = $fileRecords
        }
    }

    $nugetVersion = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($NuGetPath)
    $document = [ordered]@{
        schemaVersion = 1
        vcpkg = [ordered]@{
            release = $script:VcpkgRelease
            baseline = $Baseline
            executableVersion = $VcpkgInfo.VersionOutput
            executableSha256 = (Get-FileHash -LiteralPath $VcpkgInfo.Path -Algorithm SHA256).Hash.ToLowerInvariant()
        }
        nuget = [ordered]@{
            source = [string]$Lock.nuget.source
            client = [ordered]@{
                lockedVersion = [string]$Lock.nuget.client.version
                productVersion = $nugetVersion.ProductVersion
                sha256 = (Get-FileHash -LiteralPath $NuGetPath -Algorithm SHA256).Hash.ToLowerInvariant()
            }
            packages = $packageRecords
        }
    }

    $versionsPath = Join-Path $script:ExternalRoot 'versions.json'
    $json = ($document | ConvertTo-Json -Depth 10) + [Environment]::NewLine
    $existing = if (Test-Path -LiteralPath $versionsPath) {
        Get-Content -LiteralPath $versionsPath -Raw -Encoding UTF8
    } else {
        $null
    }

    if ($existing -ne $json) {
        [System.IO.File]::WriteAllText(
            $versionsPath,
            $json,
            (New-Object System.Text.UTF8Encoding($false)))
        Write-Host '[bootstrap] Wrote external/versions.json.'
    } else {
        Write-Host '[bootstrap] external/versions.json is already current.'
    }
}

Write-Host "[bootstrap] Repository: $script:RepositoryRoot"
foreach ($command in @('git.exe')) {
    if ($null -eq (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "$command was not found on PATH."
    }
}

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
New-Item -ItemType Directory -Path $script:ExternalRoot, $script:NuGetRoot, $script:OutRoot -Force | Out-Null

$lockPath = Join-Path $script:RepositoryRoot 'dependencies.lock.json'
$manifestPath = Join-Path $script:RepositoryRoot 'vcpkg.json'
Assert-RequiredFile -Path $lockPath
Assert-RequiredFile -Path $manifestPath
$lock = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json

if ([int]$lock.schemaVersion -ne 1) {
    throw "Unsupported dependencies.lock.json schemaVersion: $($lock.schemaVersion)"
}
if ([string]$lock.nuget.source -notmatch '^https://') {
    throw 'The NuGet source must be an HTTPS URL.'
}

$packageNames = @($lock.nuget.packages | ForEach-Object { [string]$_.name })
$expectedPackageNames = @('agility-sdk', 'dxc', 'pix-runtime')
if ($packageNames.Count -ne $expectedPackageNames.Count -or
    @($packageNames | Sort-Object -Unique).Count -ne $expectedPackageNames.Count -or
    @($expectedPackageNames | Where-Object { $_ -notin $packageNames }).Count -ne 0) {
    throw 'dependencies.lock.json must contain exactly agility-sdk, dxc, and pix-runtime.'
}

$vcpkgInfo = Install-Vcpkg -Baseline ([string]$manifest.'builtin-baseline')
$nugetPath = Install-NuGetClient -ClientLock $lock.nuget.client
foreach ($package in $lock.nuget.packages) {
    Install-NuGetPackage -NuGetPath $nugetPath -Source ([string]$lock.nuget.source) -LockEntry $package
}

Write-VersionsFile -Lock $lock -Baseline ([string]$manifest.'builtin-baseline') `
    -VcpkgInfo $vcpkgInfo -NuGetPath $nugetPath
Write-Host '[bootstrap] Locked dependencies are ready.'
