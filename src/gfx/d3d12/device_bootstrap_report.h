#pragma once

#include "gfx/d3d12/device_bootstrap.h"

#include <string>

namespace renderlab::gfx::d3d12
{
struct DeviceBootstrapReportData
{
    HRESULT status = E_FAIL;
    HRESULT infoQueueStatus = E_NOINTERFACE;
    HRESULT directQueueStatus = E_FAIL;
    HRESULT directQueueNameStatus = E_FAIL;

    bool deviceCreated = false;
    bool infoQueueCreated = false;
    bool infoQueuePolicyConfigured = false;
    bool infoQueueBreaksEnabled = false;
    bool directQueueCreated = false;
    bool directQueueNamed = false;
};

[[nodiscard]] DeviceBootstrapReportData MakeDeviceBootstrapReportData(
    const DeviceBootstrapResult &result) noexcept;

[[nodiscard]] std::string FormatDeviceBootstrapReport(
    const DeviceBootstrapReportData &data);
} // namespace renderlab::gfx::d3d12
