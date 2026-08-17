param(
    [Parameter(Mandatory = $true)]
    [string]$Executable
)

$ErrorActionPreference = 'Stop'

$executablePath = (Resolve-Path -LiteralPath $Executable).Path
$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $executablePath
$startInfo.Arguments = '--smoke-test=window-lifecycle --force-device-init-failure'
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardError = $true

$process = [System.Diagnostics.Process]::new()
$process.StartInfo = $startInfo

try {
    if (-not $process.Start()) {
        throw 'Failed to start the forced device initialization failure process.'
    }

    $diagnosticTask = $process.StandardError.ReadToEndAsync()

    if (-not $process.WaitForExit(10000)) {
        $process.Kill()
        throw "Forced device initialization failure timed out after 10 seconds."
    }

    $process.WaitForExit()
    $diagnostics = $diagnosticTask.GetAwaiter().GetResult()

    if ($process.ExitCode -eq 0) {
        throw 'Forced device initialization failure unexpectedly exited successfully.'
    }

    if ($process.ExitCode -ne 1) {
        throw "Forced device initialization failure exited with unexpected code $($process.ExitCode)."
    }

    $requiredDiagnostics = @(
        'D3D12 InfoQueue messages:',
        'run_failure=false',
        'D3D12 device removal:',
        'removal_detected=false',
        'operation=ForcedDeviceInitializationFailure',
        'status=0x80004005'
    )

    foreach ($requiredDiagnostic in $requiredDiagnostics) {
        if (-not $diagnostics.Contains($requiredDiagnostic)) {
            throw "Forced failure diagnostics did not contain '$requiredDiagnostic'."
        }
    }
}
finally {
    $process.Dispose()
}
