#include <catch2/catch_test_macros.hpp>

#include <dxgi.h>

#include "gfx/d3d12/device_removal_report.h"

using renderlab::gfx::d3d12::DeviceRemovalReportData;
using renderlab::gfx::d3d12::DredAllocationReport;
using renderlab::gfx::d3d12::DredBreadcrumbNodeReport;
using renderlab::gfx::d3d12::FormatDeviceRemovalReport;

TEST_CASE("device removal report records a healthy device")
{
    const DeviceRemovalReportData data{
        .deviceAvailable = true,
        .removalReason = S_OK,
    };

    const auto report = FormatDeviceRemovalReport(data);

    REQUIRE(report.find("device_available=true") != std::string::npos);
    REQUIRE(report.find("removal_detected=false") != std::string::npos);
    REQUIRE(report.find("reason=0x00000000") != std::string::npos);
    REQUIRE(report.find("dred_attempted=false") != std::string::npos);
    REQUIRE(report.find("breadcrumbs_status") == std::string::npos);
}

TEST_CASE("device removal report preserves an unavailable DRED interface")
{
    const DeviceRemovalReportData data{
        .deviceAvailable = true,
        .removalReason = DXGI_ERROR_DEVICE_REMOVED,
        .removalDetected = true,
        .dredAttempted = true,
        .dredInterfaceStatus = E_NOINTERFACE,
    };

    const auto report = FormatDeviceRemovalReport(data);

    REQUIRE(report.find("removal_detected=true") != std::string::npos);
    REQUIRE(report.find("reason_category=device_removed") != std::string::npos);
    REQUIRE(report.find("dred_interface_status=0x80004002") != std::string::npos);
    REQUIRE(report.find("dred_interface_available=false") != std::string::npos);
    REQUIRE(report.find("dred_interface_version=unavailable") != std::string::npos);
}

TEST_CASE("device removal report preserves partial DRED output")
{
    DeviceRemovalReportData data{
        .deviceAvailable = true,
        .removalReason = DXGI_ERROR_DEVICE_HUNG,
        .removalDetected = true,
        .dredAttempted = true,
        .dredInterfaceStatus = S_OK,
        .dredInterfaceAvailable = true,
        .dredInterface1Available = true,
        .breadcrumbsStatus = S_OK,
        .breadcrumbNodes = {
            DredBreadcrumbNodeReport{
                .commandListName = "RenderLab list",
                .commandQueueName = "RenderLab D3D12 Direct Queue",
                .breadcrumbCount = 8,
                .lastBreadcrumbValue = 5,
                .lastBreadcrumbValueAvailable = true,
            },
        },
        .pageFaultStatus = E_FAIL,
    };

    const auto report = FormatDeviceRemovalReport(data);

    REQUIRE(report.find("breadcrumbs_status=0x00000000") != std::string::npos);
    REQUIRE(report.find("dred_interface_version=1.1") != std::string::npos);
    REQUIRE(report.find("breadcrumb_nodes=1") != std::string::npos);
    REQUIRE(report.find("command_list=RenderLab list") != std::string::npos);
    REQUIRE(report.find("last_breadcrumb=5") != std::string::npos);
    REQUIRE(report.find("page_fault_status=0x80004005") != std::string::npos);
    REQUIRE(report.find("page_fault_va=unavailable") != std::string::npos);
}

TEST_CASE("device removal report formats page fault allocations")
{
    DeviceRemovalReportData data{
        .deviceAvailable = true,
        .removalReason = DXGI_ERROR_DEVICE_RESET,
        .removalDetected = true,
        .dredAttempted = true,
        .dredInterfaceStatus = S_OK,
        .dredInterfaceAvailable = true,
        .breadcrumbsStatus = E_FAIL,
        .pageFaultStatus = S_OK,
        .pageFaultAddress = 0x1234,
        .existingAllocations = {
            DredAllocationReport{
                .objectName = "resident texture",
                .allocationType = 34,
            },
        },
        .recentFreedAllocations = {
            DredAllocationReport{
                .objectName = "retired buffer",
                .allocationType = 34,
            },
        },
        .recentFreedAllocationsTruncated = true,
    };

    const auto report = FormatDeviceRemovalReport(data);

    REQUIRE(report.find("page_fault_va=0x0000000000001234") != std::string::npos);
    REQUIRE(report.find("dred_interface_version=1.0") != std::string::npos);
    REQUIRE(report.find("existing_allocation[0]: name=resident texture") !=
            std::string::npos);
    REQUIRE(report.find("recent_freed_allocation[0]: name=retired buffer") !=
            std::string::npos);
    REQUIRE(report.find("recent_freed_allocations_truncated=true") !=
            std::string::npos);
}
