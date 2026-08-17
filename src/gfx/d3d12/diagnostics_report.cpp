#include "gfx/d3d12/diagnostics_report.h"

#include <string>

#include "gfx/d3d12/hresult_error.h"

namespace renderlab::gfx::d3d12
{
std::string FormatDiagnosticsReport(
    const DiagnosticsConfig &config,
    const DiagnosticsState &state)
{
    std::string report = "D3D12 diagnostics: status=";
    report += FormatHResultCode(state.status);
    report += "; debug_layer=";
    report += config.enableDebugLayer ? "requested," : "not-requested,";
    report += state.debugLayerEnabled ? "enabled;" : "not-enabled;";
    report += " gpu_based_validation=";
    report += config.enableGpuBasedValidation ? "requested," : "not-requested,";
    report += state.gpuBasedValidationEnabled ? "enabled;" : "not-enabled;";
    report += " dred=";
    report += config.enableDred ? "requested," : "not-requested,";
    report += state.dredConfigured ? "configured" : "not-configured";
    report += "; info_queue=not-created";
    return report;
}
} // namespace renderlab::gfx::d3d12
