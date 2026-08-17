#pragma once

#include <d3d12.h>
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace renderlab::gfx::d3d12
{
struct DredBreadcrumbNodeReport
{
    std::string commandListName;
    std::string commandQueueName;
    std::uint32_t breadcrumbCount = 0;
    std::uint32_t lastBreadcrumbValue = 0;
    bool lastBreadcrumbValueAvailable = false;
};

struct DredAllocationReport
{
    std::string objectName;
    std::uint32_t allocationType = 0;
};

struct DeviceRemovalReportData
{
    bool deviceAvailable = false;
    HRESULT removalReason = E_POINTER;
    bool removalDetected = false;

    bool dredAttempted = false;
    HRESULT dredInterfaceStatus = E_NOINTERFACE;
    bool dredInterfaceAvailable = false;
    bool dredInterface1Available = false;

    HRESULT breadcrumbsStatus = E_NOTIMPL;
    std::vector<DredBreadcrumbNodeReport> breadcrumbNodes;
    bool breadcrumbNodesTruncated = false;

    HRESULT pageFaultStatus = E_NOTIMPL;
    std::uint64_t pageFaultAddress = 0;
    std::vector<DredAllocationReport> existingAllocations;
    bool existingAllocationsTruncated = false;
    std::vector<DredAllocationReport> recentFreedAllocations;
    bool recentFreedAllocationsTruncated = false;
};

[[nodiscard]] DeviceRemovalReportData CaptureDeviceRemovalReport(
    ID3D12Device *device);

[[nodiscard]] std::string FormatDeviceRemovalReport(
    const DeviceRemovalReportData &data);
} // namespace renderlab::gfx::d3d12
