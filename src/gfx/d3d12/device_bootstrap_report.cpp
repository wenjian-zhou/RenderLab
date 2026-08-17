#include "gfx/d3d12/device_bootstrap_report.h"
#include "gfx/d3d12/hresult_error.h"

namespace renderlab::gfx::d3d12
{
DeviceBootstrapReportData MakeDeviceBootstrapReportData(
    const DeviceBootstrapResult &result) noexcept
{
    return {
        .status = result.status,
        .infoQueueStatus = result.infoQueueStatus,
        .directQueueStatus = result.directQueueStatus,
        .directQueueNameStatus = result.directQueueNameStatus,
        .deviceCreated = result.device != nullptr,
        .infoQueueCreated = result.infoQueueCreated,
        .infoQueuePolicyConfigured = result.infoQueuePolicyConfigured,
        .infoQueueBreaksEnabled = result.infoQueueBreaksEnabled,
        .directQueueCreated = result.directQueueCreated,
        .directQueueNamed = result.directQueueNamed,
    };
}

std::string FormatDeviceBootstrapReport(
    const DeviceBootstrapReportData &data)
{
    std::string report = "D3D12 device bootstrap: status=";
    report += FormatHResultCode(data.status);
    report += "; device=";
    report += data.deviceCreated ? "created" : "not-created";
    report += "; info_queue_query_status=";
    report += FormatHResultCode(data.infoQueueStatus);
    report += "; info_queue=";
    report += data.infoQueueCreated ? "created" : "not-created";
    report += "; info_queue_policy=";
    report += data.infoQueuePolicyConfigured ? "configured" : "not-configured";
    report += "; error_corruption_breaks=";
    report += data.infoQueueBreaksEnabled ? "enabled" : "disabled";
    report += "; warning_suppression=none";
    report += "; direct_queue_type=direct";
    report += "; direct_queue_create_status=";
    report += FormatHResultCode(data.directQueueStatus);
    report += "; direct_queue=";
    report += data.directQueueCreated ? "created" : "not-created";
    report += "; direct_queue_name_status=";
    report += FormatHResultCode(data.directQueueNameStatus);
    report += "; direct_queue_name=";
    report += data.directQueueNamed ? "set" : "not-set";
    return report;
}
} // namespace renderlab::gfx::d3d12
