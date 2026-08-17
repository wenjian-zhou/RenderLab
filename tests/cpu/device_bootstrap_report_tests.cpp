#include "gfx/d3d12/device_bootstrap_report.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
using renderlab::gfx::d3d12::DeviceBootstrapReportData;
using renderlab::gfx::d3d12::FormatDeviceBootstrapReport;
} // namespace

TEST_CASE("device bootstrap report describes configured info queue breaks",
          "[device-bootstrap-report]")
{
    const DeviceBootstrapReportData data{
        .status = S_OK,
        .infoQueueStatus = S_OK,
        .directQueueStatus = S_OK,
        .directQueueNameStatus = S_OK,
        .deviceCreated = true,
        .infoQueueCreated = true,
        .infoQueuePolicyConfigured = true,
        .infoQueueBreaksEnabled = true,
        .directQueueCreated = true,
        .directQueueNamed = true,
    };

    const auto report = FormatDeviceBootstrapReport(data);

    REQUIRE(report.find("status=0x00000000") != std::string::npos);
    REQUIRE(report.find("device=created") != std::string::npos);
    REQUIRE(report.find("info_queue_query_status=0x00000000") != std::string::npos);
    REQUIRE(report.find("info_queue=created") != std::string::npos);
    REQUIRE(report.find("info_queue_policy=configured") != std::string::npos);
    REQUIRE(report.find("error_corruption_breaks=enabled") != std::string::npos);
    REQUIRE(report.find("warning_suppression=none") != std::string::npos);
    REQUIRE(report.find("direct_queue_type=direct") != std::string::npos);
    REQUIRE(report.find("direct_queue_create_status=0x00000000") !=
            std::string::npos);
    REQUIRE(report.find("direct_queue=created") != std::string::npos);
    REQUIRE(report.find("direct_queue_name_status=0x00000000") !=
            std::string::npos);
    REQUIRE(report.find("direct_queue_name=set") != std::string::npos);
}

TEST_CASE("device bootstrap report distinguishes policy from active breaks",
          "[device-bootstrap-report]")
{
    const DeviceBootstrapReportData data{
        .status = S_OK,
        .infoQueueStatus = S_OK,
        .directQueueStatus = S_OK,
        .directQueueNameStatus = S_OK,
        .deviceCreated = true,
        .infoQueueCreated = true,
        .infoQueuePolicyConfigured = true,
        .infoQueueBreaksEnabled = false,
        .directQueueCreated = true,
        .directQueueNamed = true,
    };

    const auto report = FormatDeviceBootstrapReport(data);

    REQUIRE(report.find("info_queue_policy=configured") != std::string::npos);
    REQUIRE(report.find("error_corruption_breaks=disabled") != std::string::npos);
}

TEST_CASE("device bootstrap report describes release info queue absence",
          "[device-bootstrap-report]")
{
    const DeviceBootstrapReportData data{
        .status = S_OK,
        .infoQueueStatus = E_NOINTERFACE,
        .directQueueStatus = S_OK,
        .directQueueNameStatus = S_OK,
        .deviceCreated = true,
        .infoQueueCreated = false,
        .infoQueuePolicyConfigured = false,
        .infoQueueBreaksEnabled = false,
        .directQueueCreated = true,
        .directQueueNamed = true,
    };

    const auto report = FormatDeviceBootstrapReport(data);

    REQUIRE(report.find("status=0x00000000") != std::string::npos);
    REQUIRE(report.find("device=created") != std::string::npos);
    REQUIRE(report.find("info_queue_query_status=0x80004002") != std::string::npos);
    REQUIRE(report.find("info_queue=not-created") != std::string::npos);
    REQUIRE(report.find("info_queue_policy=not-configured") != std::string::npos);
    REQUIRE(report.find("error_corruption_breaks=disabled") != std::string::npos);
    REQUIRE(report.find("direct_queue=created") != std::string::npos);
    REQUIRE(report.find("direct_queue_name=set") != std::string::npos);
}

TEST_CASE("device bootstrap report preserves initialization failure",
          "[device-bootstrap-report]")
{
    const DeviceBootstrapReportData data{
        .status = E_FAIL,
        .infoQueueStatus = E_NOINTERFACE,
        .deviceCreated = false,
    };

    const auto report = FormatDeviceBootstrapReport(data);

    REQUIRE(report.find("status=0x80004005") != std::string::npos);
    REQUIRE(report.find("device=not-created") != std::string::npos);
    REQUIRE(report.find("direct_queue=not-created") != std::string::npos);
    REQUIRE(report.find("direct_queue_name=not-set") != std::string::npos);
}

TEST_CASE("device bootstrap report preserves direct queue creation failure",
          "[device-bootstrap-report]")
{
    const DeviceBootstrapReportData data{
        .status = E_OUTOFMEMORY,
        .infoQueueStatus = S_OK,
        .directQueueStatus = E_OUTOFMEMORY,
        .directQueueNameStatus = E_FAIL,
        .deviceCreated = true,
        .infoQueueCreated = true,
        .infoQueuePolicyConfigured = true,
        .infoQueueBreaksEnabled = false,
        .directQueueCreated = false,
        .directQueueNamed = false,
    };

    const auto report = FormatDeviceBootstrapReport(data);

    REQUIRE(report.find("status=0x8007000E") != std::string::npos);
    REQUIRE(report.find("device=created") != std::string::npos);
    REQUIRE(report.find("direct_queue_create_status=0x8007000E") !=
            std::string::npos);
    REQUIRE(report.find("direct_queue=not-created") != std::string::npos);
    REQUIRE(report.find("direct_queue_name_status=0x80004005") !=
            std::string::npos);
    REQUIRE(report.find("direct_queue_name=not-set") != std::string::npos);
}

TEST_CASE("device bootstrap report preserves direct queue naming failure",
          "[device-bootstrap-report]")
{
    const DeviceBootstrapReportData data{
        .status = E_FAIL,
        .infoQueueStatus = S_OK,
        .directQueueStatus = S_OK,
        .directQueueNameStatus = E_FAIL,
        .deviceCreated = true,
        .infoQueueCreated = true,
        .infoQueuePolicyConfigured = true,
        .infoQueueBreaksEnabled = false,
        .directQueueCreated = true,
        .directQueueNamed = false,
    };

    const auto report = FormatDeviceBootstrapReport(data);

    REQUIRE(report.find("status=0x80004005") != std::string::npos);
    REQUIRE(report.find("direct_queue_create_status=0x00000000") !=
            std::string::npos);
    REQUIRE(report.find("direct_queue=created") != std::string::npos);
    REQUIRE(report.find("direct_queue_name_status=0x80004005") !=
            std::string::npos);
    REQUIRE(report.find("direct_queue_name=not-set") != std::string::npos);
}
