param(
    [Parameter(Mandatory = $true)]
    [string]$DebugExecutable,

    [Parameter(Mandatory = $true)]
    [string]$ReleaseExecutable
)

$ErrorActionPreference = 'Stop'

function Invoke-RenderLabValidationRun {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Executable,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,

        [Parameter(Mandatory = $true)]
        [string]$Label,

        [Parameter(Mandatory = $true)]
        [string[]]$RequiredDiagnostics
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = (Resolve-Path -LiteralPath $Executable).Path
    $startInfo.Arguments = $Arguments -join ' '
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardError = $true

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo

    try {
        if (-not $process.Start()) {
            throw "$Label failed to start."
        }

        $diagnosticTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(10000)) {
            $process.Kill()
            throw "$Label timed out after 10 seconds."
        }

        $process.WaitForExit()
        $diagnostics = $diagnosticTask.GetAwaiter().GetResult()

        if ($process.ExitCode -ne 0) {
            throw "$Label exited with code $($process.ExitCode).`n$diagnostics"
        }

        foreach ($requiredDiagnostic in $RequiredDiagnostics) {
            if (-not $diagnostics.Contains($requiredDiagnostic)) {
                throw "$Label did not contain '$requiredDiagnostic'.`n$diagnostics"
            }
        }

        $infoQueueReports = [regex]::Matches(
            $diagnostics,
            'D3D12 InfoQueue messages:[^\r\n]*')
        if ($infoQueueReports.Count -ne 2) {
            throw "$Label expected two InfoQueue checkpoints but found $($infoQueueReports.Count)."
        }

        foreach ($infoQueueReport in $infoQueueReports) {
            if (-not $infoQueueReport.Value.Contains('corruption=0') -or
                -not $infoQueueReport.Value.Contains('errors=0') -or
                -not $infoQueueReport.Value.Contains('warnings=0') -or
                -not $infoQueueReport.Value.Contains('run_failure=false')) {
                throw "$Label reported unexpected InfoQueue diagnostics: $($infoQueueReport.Value)"
            }
        }

        Write-Host "$Label passed."
    }
    finally {
        $process.Dispose()
    }
}

$commonDiagnostics = @(
    'D3D12 device bootstrap: status=0x00000000; device=created',
    'direct_queue_type=direct',
    'direct_queue_create_status=0x00000000',
    'direct_queue=created',
    'direct_queue_name_status=0x00000000',
    'direct_queue_name=set',
    'D3D12 device features: status=0x00000000',
    'actual_feature_level=',
    'shader_model_query_status=0x00000000',
    'shader_model=',
    'resource_binding_tier_query_status=0x00000000',
    'resource_binding_tier=',
    'root_signature_version_query_status=0x00000000',
    'root_signature_version=',
    'raytracing_tier_query_status=0x00000000',
    'raytracing_tier=',
    'D3D12 device removal: device_available=true; removal_detected=false',
    'warning_suppression=none'
)

$debugDiagnostics = $commonDiagnostics + @(
    'debug_layer=requested,enabled',
    'gpu_based_validation=not-requested,not-enabled',
    'dred=requested,configured',
    'info_queue_query_status=0x00000000',
    'info_queue=created',
    'info_queue_policy=configured'
)

for ($iteration = 1; $iteration -le 5; ++$iteration) {
    Invoke-RenderLabValidationRun `
        -Executable $DebugExecutable `
        -Arguments @('--smoke-test=window-lifecycle') `
        -Label "Debug run $iteration/5" `
        -RequiredDiagnostics $debugDiagnostics
}

$gpuValidationDiagnostics = $commonDiagnostics + @(
    'debug_layer=requested,enabled',
    'gpu_based_validation=requested,enabled',
    'dred=requested,configured',
    'info_queue_query_status=0x00000000',
    'info_queue=created',
    'info_queue_policy=configured'
)

Invoke-RenderLabValidationRun `
    -Executable $DebugExecutable `
    -Arguments @('--smoke-test=window-lifecycle', '--gpu-validation') `
    -Label 'GPU Validation run 1/1' `
    -RequiredDiagnostics $gpuValidationDiagnostics

$releaseDiagnostics = $commonDiagnostics + @(
    'debug_layer=not-requested,not-enabled',
    'gpu_based_validation=not-requested,not-enabled',
    'dred=requested,configured'
)

Invoke-RenderLabValidationRun `
    -Executable $ReleaseExecutable `
    -Arguments @('--smoke-test=window-lifecycle') `
    -Label 'Release run 1/1' `
    -RequiredDiagnostics $releaseDiagnostics

Write-Host 'Stage 2 Loop 2 local validation passed: 5 Debug, 1 GPU Validation, 1 Release.'
