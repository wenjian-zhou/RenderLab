param(
    [Parameter(Mandatory = $true)]
    [string]$Executable
)

$ErrorActionPreference = 'Stop'

$executablePath = (Resolve-Path -LiteralPath $Executable).Path
$process = Start-Process `
    -FilePath $executablePath `
    -ArgumentList '--smoke-test=window-lifecycle' `
    -WindowStyle Hidden `
    -PassThru

try {
    if (-not $process.WaitForExit(10000)) {
        Stop-Process -Id $process.Id -Force
        throw "RenderLab smoke test timed out after 10 seconds."
    }

    if ($process.ExitCode -ne 0) {
        throw "RenderLab smoke test exited with code $($process.ExitCode)."
    }
}
finally {
    $process.Dispose()
}
