#include "gfx/d3d12/diagnostics_report.h"

#include <cstdio>
#include <string>

namespace renderlab::gfx::d3d12
{
namespace
{
std::string FormatStatus(HRESULT status)
{
    char buffer[11] = {};
    sprintf_s(
        buffer,
        "0x%08lX",
        static_cast<unsigned long>(status));
    return buffer;
}
} // namespace

std::string FormatDiagnosticsReport(
    const DiagnosticsConfig &config,
    const DiagnosticsState &state)
{
    std::string report = "D3D12 diagnostics: status=";
    report += FormatStatus(state.status);
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
