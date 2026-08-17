#include "gfx/d3d12/device_removal_report.h"

#include <wrl/client.h>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>

#include "gfx/d3d12/hresult_error.h"

namespace renderlab::gfx::d3d12
{
namespace
{
constexpr std::size_t maximumDredNodes = 256;

std::string NormalizeName(std::string name)
{
    std::replace(name.begin(), name.end(), '\r', ' ');
    std::replace(name.begin(), name.end(), '\n', ' ');
    return name.empty() ? "unnamed" : name;
}

std::string WideNameToUtf8(const wchar_t *name)
{
    if (name == nullptr || *name == L'\0')
    {
        return "unnamed";
    }

    const int requiredSize = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        name,
        -1,
        nullptr,
        0,
        nullptr,
        nullptr);

    if (requiredSize <= 1)
    {
        return "unavailable";
    }

    std::string utf8(static_cast<std::size_t>(requiredSize), '\0');
    const int writtenSize = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        name,
        -1,
        utf8.data(),
        requiredSize,
        nullptr,
        nullptr);

    if (writtenSize != requiredSize)
    {
        return "unavailable";
    }

    utf8.resize(static_cast<std::size_t>(requiredSize - 1));
    return NormalizeName(std::move(utf8));
}

std::string CopyDredName(const char *ansiName, const wchar_t *wideName)
{
    if (ansiName != nullptr && *ansiName != '\0')
    {
        return NormalizeName(ansiName);
    }

    return WideNameToUtf8(wideName);
}

template <typename Node, typename CopyNode>
void CopyLinkedNodes(
    const Node *head,
    CopyNode copyNode,
    bool &truncated)
{
    const Node *node = head;
    std::size_t count = 0;
    while (node != nullptr && count < maximumDredNodes)
    {
        copyNode(*node);
        node = node->pNext;
        ++count;
    }
    truncated = node != nullptr;
}

void CaptureBreadcrumbs(
    ID3D12DeviceRemovedExtendedData1 &dred,
    DeviceRemovalReportData &result)
{
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 output = {};
    result.breadcrumbsStatus = dred.GetAutoBreadcrumbsOutput1(&output);
    if (FAILED(result.breadcrumbsStatus))
    {
        return;
    }

    CopyLinkedNodes(
        output.pHeadAutoBreadcrumbNode,
        [&result](const D3D12_AUTO_BREADCRUMB_NODE1 &node)
        {
            DredBreadcrumbNodeReport report = {};
            report.commandListName = CopyDredName(
                node.pCommandListDebugNameA,
                node.pCommandListDebugNameW);
            report.commandQueueName = CopyDredName(
                node.pCommandQueueDebugNameA,
                node.pCommandQueueDebugNameW);
            report.breadcrumbCount = node.BreadcrumbCount;

            if (node.pLastBreadcrumbValue != nullptr)
            {
                report.lastBreadcrumbValue = *node.pLastBreadcrumbValue;
                report.lastBreadcrumbValueAvailable = true;
            }

            result.breadcrumbNodes.push_back(std::move(report));
        },
        result.breadcrumbNodesTruncated);
}

void CaptureLegacyBreadcrumbs(
    ID3D12DeviceRemovedExtendedData &dred,
    DeviceRemovalReportData &result)
{
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT output = {};
    result.breadcrumbsStatus = dred.GetAutoBreadcrumbsOutput(&output);
    if (FAILED(result.breadcrumbsStatus))
    {
        return;
    }

    CopyLinkedNodes(
        output.pHeadAutoBreadcrumbNode,
        [&result](const D3D12_AUTO_BREADCRUMB_NODE &node)
        {
            DredBreadcrumbNodeReport report = {};
            report.commandListName = CopyDredName(
                node.pCommandListDebugNameA,
                node.pCommandListDebugNameW);
            report.commandQueueName = CopyDredName(
                node.pCommandQueueDebugNameA,
                node.pCommandQueueDebugNameW);
            report.breadcrumbCount = node.BreadcrumbCount;

            if (node.pLastBreadcrumbValue != nullptr)
            {
                report.lastBreadcrumbValue = *node.pLastBreadcrumbValue;
                report.lastBreadcrumbValueAvailable = true;
            }

            result.breadcrumbNodes.push_back(std::move(report));
        },
        result.breadcrumbNodesTruncated);
}

void CopyAllocations(
    const D3D12_DRED_ALLOCATION_NODE1 *head,
    std::vector<DredAllocationReport> &allocations,
    bool &truncated)
{
    CopyLinkedNodes(
        head,
        [&allocations](const D3D12_DRED_ALLOCATION_NODE1 &node)
        {
            allocations.push_back({
                .objectName = CopyDredName(node.ObjectNameA, node.ObjectNameW),
                .allocationType = static_cast<std::uint32_t>(node.AllocationType),
            });
        },
        truncated);
}

void CopyLegacyAllocations(
    const D3D12_DRED_ALLOCATION_NODE *head,
    std::vector<DredAllocationReport> &allocations,
    bool &truncated)
{
    CopyLinkedNodes(
        head,
        [&allocations](const D3D12_DRED_ALLOCATION_NODE &node)
        {
            allocations.push_back({
                .objectName = CopyDredName(node.ObjectNameA, node.ObjectNameW),
                .allocationType = static_cast<std::uint32_t>(node.AllocationType),
            });
        },
        truncated);
}

void CapturePageFault(
    ID3D12DeviceRemovedExtendedData1 &dred,
    DeviceRemovalReportData &result)
{
    D3D12_DRED_PAGE_FAULT_OUTPUT1 output = {};
    result.pageFaultStatus = dred.GetPageFaultAllocationOutput1(&output);
    if (FAILED(result.pageFaultStatus))
    {
        return;
    }

    result.pageFaultAddress = output.PageFaultVA;
    CopyAllocations(
        output.pHeadExistingAllocationNode,
        result.existingAllocations,
        result.existingAllocationsTruncated);
    CopyAllocations(
        output.pHeadRecentFreedAllocationNode,
        result.recentFreedAllocations,
        result.recentFreedAllocationsTruncated);
}

void CaptureLegacyPageFault(
    ID3D12DeviceRemovedExtendedData &dred,
    DeviceRemovalReportData &result)
{
    D3D12_DRED_PAGE_FAULT_OUTPUT output = {};
    result.pageFaultStatus = dred.GetPageFaultAllocationOutput(&output);
    if (FAILED(result.pageFaultStatus))
    {
        return;
    }

    result.pageFaultAddress = output.PageFaultVA;
    CopyLegacyAllocations(
        output.pHeadExistingAllocationNode,
        result.existingAllocations,
        result.existingAllocationsTruncated);
    CopyLegacyAllocations(
        output.pHeadRecentFreedAllocationNode,
        result.recentFreedAllocations,
        result.recentFreedAllocationsTruncated);
}

std::string FormatAddress(std::uint64_t address)
{
    std::ostringstream stream;
    stream << "0x"
           << std::uppercase
           << std::hex
           << std::setw(16)
           << std::setfill('0')
           << address;
    return stream.str();
}

std::string BooleanName(bool value)
{
    return value ? "true" : "false";
}
} // namespace

DeviceRemovalReportData CaptureDeviceRemovalReport(ID3D12Device *device)
{
    DeviceRemovalReportData result = {};
    if (device == nullptr)
    {
        return result;
    }

    result.deviceAvailable = true;
    result.removalReason = device->GetDeviceRemovedReason();
    result.removalDetected = FAILED(result.removalReason);
    if (!result.removalDetected)
    {
        return result;
    }

    result.dredAttempted = true;

    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData1> dred1;
    result.dredInterfaceStatus = device->QueryInterface(IID_PPV_ARGS(&dred1));
    if (SUCCEEDED(result.dredInterfaceStatus) && dred1)
    {
        result.dredInterfaceAvailable = true;
        result.dredInterface1Available = true;
        CaptureBreadcrumbs(*dred1.Get(), result);
        CapturePageFault(*dred1.Get(), result);
        return result;
    }

    if (SUCCEEDED(result.dredInterfaceStatus))
    {
        result.dredInterfaceStatus = E_UNEXPECTED;
    }

    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData> dred;
    result.dredInterfaceStatus = device->QueryInterface(IID_PPV_ARGS(&dred));
    if (FAILED(result.dredInterfaceStatus) || !dred)
    {
        if (SUCCEEDED(result.dredInterfaceStatus))
        {
            result.dredInterfaceStatus = E_UNEXPECTED;
        }
        return result;
    }

    result.dredInterfaceAvailable = true;
    CaptureLegacyBreadcrumbs(*dred.Get(), result);
    CaptureLegacyPageFault(*dred.Get(), result);
    return result;
}

std::string FormatDeviceRemovalReport(const DeviceRemovalReportData &data)
{
    std::string report = "D3D12 device removal: device_available=";
    report += BooleanName(data.deviceAvailable);
    report += "; removal_detected=";
    report += BooleanName(data.removalDetected);
    report += "; reason=";
    report += FormatHResultCode(data.removalReason);
    report += "; reason_category=";
    report += HResultCategoryName(ClassifyHResult(data.removalReason));
    report += "; dred_attempted=";
    report += BooleanName(data.dredAttempted);

    if (!data.dredAttempted)
    {
        return report;
    }

    report += "; dred_interface_status=";
    report += FormatHResultCode(data.dredInterfaceStatus);
    report += "; dred_interface_available=";
    report += BooleanName(data.dredInterfaceAvailable);
    report += "; dred_interface_version=";
    report += data.dredInterfaceAvailable
                  ? (data.dredInterface1Available ? "1.1" : "1.0")
                  : "unavailable";

    if (!data.dredInterfaceAvailable)
    {
        return report;
    }

    report += "; breadcrumbs_status=";
    report += FormatHResultCode(data.breadcrumbsStatus);
    report += "; breadcrumb_nodes=";
    report += std::to_string(data.breadcrumbNodes.size());
    report += "; breadcrumb_nodes_truncated=";
    report += BooleanName(data.breadcrumbNodesTruncated);
    report += "; page_fault_status=";
    report += FormatHResultCode(data.pageFaultStatus);
    report += "; page_fault_va=";
    report += SUCCEEDED(data.pageFaultStatus)
                  ? FormatAddress(data.pageFaultAddress)
                  : "unavailable";
    report += "; existing_allocations=";
    report += std::to_string(data.existingAllocations.size());
    report += "; existing_allocations_truncated=";
    report += BooleanName(data.existingAllocationsTruncated);
    report += "; recent_freed_allocations=";
    report += std::to_string(data.recentFreedAllocations.size());
    report += "; recent_freed_allocations_truncated=";
    report += BooleanName(data.recentFreedAllocationsTruncated);

    for (std::size_t index = 0; index < data.breadcrumbNodes.size(); ++index)
    {
        const auto &node = data.breadcrumbNodes[index];
        report += "\n  breadcrumb[";
        report += std::to_string(index);
        report += "]: command_list=";
        report += node.commandListName;
        report += "; command_queue=";
        report += node.commandQueueName;
        report += "; breadcrumb_count=";
        report += std::to_string(node.breadcrumbCount);
        report += "; last_breadcrumb=";
        report += node.lastBreadcrumbValueAvailable
                      ? std::to_string(node.lastBreadcrumbValue)
                      : "unavailable";
    }

    const auto appendAllocations = [&report](
                                       std::string_view label,
                                       const std::vector<DredAllocationReport> &allocations)
    {
        for (std::size_t index = 0; index < allocations.size(); ++index)
        {
            report += "\n  ";
            report += label;
            report += "[";
            report += std::to_string(index);
            report += "]: name=";
            report += allocations[index].objectName;
            report += "; allocation_type=";
            report += std::to_string(allocations[index].allocationType);
        }
    };

    appendAllocations("existing_allocation", data.existingAllocations);
    appendAllocations("recent_freed_allocation", data.recentFreedAllocations);
    return report;
}
} // namespace renderlab::gfx::d3d12
