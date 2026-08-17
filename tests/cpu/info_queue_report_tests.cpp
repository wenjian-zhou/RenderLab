#include <catch2/catch_test_macros.hpp>

#include "gfx/d3d12/info_queue_report.h"

using renderlab::gfx::d3d12::FormatInfoQueueReport;
using renderlab::gfx::d3d12::ClassifyInfoQueueMessages;
using renderlab::gfx::d3d12::InfoQueueCategoryName;
using renderlab::gfx::d3d12::InfoQueueCollectionResult;
using renderlab::gfx::d3d12::InfoQueueMessageData;
using renderlab::gfx::d3d12::InfoQueueSeverityName;

TEST_CASE("InfoQueue severity names cover known and unknown values")
{
    REQUIRE(InfoQueueSeverityName(D3D12_MESSAGE_SEVERITY_CORRUPTION) ==
            "corruption");
    REQUIRE(InfoQueueSeverityName(D3D12_MESSAGE_SEVERITY_ERROR) == "error");
    REQUIRE(InfoQueueSeverityName(D3D12_MESSAGE_SEVERITY_WARNING) == "warning");
    REQUIRE(InfoQueueSeverityName(D3D12_MESSAGE_SEVERITY_INFO) == "information");
    REQUIRE(InfoQueueSeverityName(D3D12_MESSAGE_SEVERITY_MESSAGE) == "message");
    REQUIRE(InfoQueueSeverityName(static_cast<D3D12_MESSAGE_SEVERITY>(99)) ==
            "unknown");
}

TEST_CASE("InfoQueue category names cover known and unknown values")
{
    REQUIRE(InfoQueueCategoryName(D3D12_MESSAGE_CATEGORY_INITIALIZATION) ==
            "initialization");
    REQUIRE(InfoQueueCategoryName(
                D3D12_MESSAGE_CATEGORY_RESOURCE_MANIPULATION) ==
            "resource_manipulation");
    REQUIRE(InfoQueueCategoryName(static_cast<D3D12_MESSAGE_CATEGORY>(99)) ==
            "unknown");
}

TEST_CASE("InfoQueue report describes an unavailable release queue")
{
    const InfoQueueCollectionResult collection = {};

    const auto report = FormatInfoQueueReport(collection);

    REQUIRE(report.find("status=0x00000000") != std::string::npos);
    REQUIRE(report.find("available=false") != std::string::npos);
    REQUIRE(report.find("run_failure=false") != std::string::npos);
    REQUIRE(report.find("warning_suppression=none") != std::string::npos);
}

TEST_CASE("InfoQueue warnings remain visible without failing the run")
{
    InfoQueueCollectionResult collection{
        .status = S_OK,
        .infoQueueAvailable = true,
        .storedMessageCount = 1,
        .storedMessagesCleared = true,
        .warningCount = 1,
        .messages = {
            InfoQueueMessageData{
                .category = D3D12_MESSAGE_CATEGORY_RESOURCE_MANIPULATION,
                .severity = D3D12_MESSAGE_SEVERITY_WARNING,
                .id = D3D12_MESSAGE_ID_UNKNOWN,
                .description = "Synthetic warning",
            },
        },
    };

    const auto report = FormatInfoQueueReport(collection);

    REQUIRE(report.find("warnings=1") != std::string::npos);
    REQUIRE(report.find("run_failure=false") != std::string::npos);
    REQUIRE(report.find("severity=warning") != std::string::npos);
    REQUIRE(report.find("category=resource_manipulation") != std::string::npos);
    REQUIRE(report.find("description=Synthetic warning") != std::string::npos);
}

TEST_CASE("InfoQueue errors and corruption fail the run")
{
    InfoQueueCollectionResult collection{
        .status = S_OK,
        .infoQueueAvailable = true,
        .storedMessageCount = 2,
        .storedMessagesCleared = true,
        .messages = {
            InfoQueueMessageData{
                .severity = D3D12_MESSAGE_SEVERITY_CORRUPTION,
            },
            InfoQueueMessageData{
                .severity = D3D12_MESSAGE_SEVERITY_ERROR,
            },
        },
    };
    ClassifyInfoQueueMessages(collection.messages, collection);

    const auto report = FormatInfoQueueReport(collection);

    REQUIRE(report.find("corruption=1") != std::string::npos);
    REQUIRE(report.find("errors=1") != std::string::npos);
    REQUIRE(report.find("run_failure=true") != std::string::npos);
}

TEST_CASE("InfoQueue classification counts every severity without suppressing warnings")
{
    InfoQueueCollectionResult collection{
        .messages = {
            InfoQueueMessageData{.severity = D3D12_MESSAGE_SEVERITY_WARNING},
            InfoQueueMessageData{.severity = D3D12_MESSAGE_SEVERITY_INFO},
            InfoQueueMessageData{.severity = D3D12_MESSAGE_SEVERITY_MESSAGE},
            InfoQueueMessageData{
                .severity = static_cast<D3D12_MESSAGE_SEVERITY>(99),
            },
        },
    };

    ClassifyInfoQueueMessages(collection.messages, collection);

    REQUIRE(collection.warningCount == 1);
    REQUIRE(collection.informationCount == 1);
    REQUIRE(collection.messageCount == 1);
    REQUIRE(collection.unknownSeverityCount == 1);
    REQUIRE_FALSE(collection.hasRunFailure);
}

TEST_CASE("InfoQueue report preserves a partial collection failure")
{
    const InfoQueueCollectionResult collection{
        .status = HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
        .infoQueueAvailable = true,
        .storedMessageCount = 4,
        .collectionLimitExceeded = true,
        .messages = {
            InfoQueueMessageData{
                .severity = D3D12_MESSAGE_SEVERITY_INFO,
                .description = "Copied before failure",
            },
        },
    };

    const auto report = FormatInfoQueueReport(collection);

    REQUIRE(report.find("stored=4") != std::string::npos);
    REQUIRE(report.find("collected=1") != std::string::npos);
    REQUIRE(report.find("collection_limit_exceeded=true") != std::string::npos);
    REQUIRE(report.find("cleared=false") != std::string::npos);
    REQUIRE(report.find("description=Copied before failure") !=
            std::string::npos);
}
