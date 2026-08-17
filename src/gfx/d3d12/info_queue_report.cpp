#include "gfx/d3d12/info_queue_report.h"

#include <cstdint>

#include "gfx/d3d12/hresult_error.h"

namespace renderlab::gfx::d3d12
{
namespace
{
std::string_view BooleanName(bool value) noexcept
{
    return value ? "true" : "false";
}
} // namespace

std::string_view InfoQueueSeverityName(
    D3D12_MESSAGE_SEVERITY severity) noexcept
{
    switch (severity)
    {
    case D3D12_MESSAGE_SEVERITY_CORRUPTION:
        return "corruption";
    case D3D12_MESSAGE_SEVERITY_ERROR:
        return "error";
    case D3D12_MESSAGE_SEVERITY_WARNING:
        return "warning";
    case D3D12_MESSAGE_SEVERITY_INFO:
        return "information";
    case D3D12_MESSAGE_SEVERITY_MESSAGE:
        return "message";
    default:
        return "unknown";
    }
}

std::string_view InfoQueueCategoryName(
    D3D12_MESSAGE_CATEGORY category) noexcept
{
    switch (category)
    {
    case D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED:
        return "application_defined";
    case D3D12_MESSAGE_CATEGORY_MISCELLANEOUS:
        return "miscellaneous";
    case D3D12_MESSAGE_CATEGORY_INITIALIZATION:
        return "initialization";
    case D3D12_MESSAGE_CATEGORY_CLEANUP:
        return "cleanup";
    case D3D12_MESSAGE_CATEGORY_COMPILATION:
        return "compilation";
    case D3D12_MESSAGE_CATEGORY_STATE_CREATION:
        return "state_creation";
    case D3D12_MESSAGE_CATEGORY_STATE_SETTING:
        return "state_setting";
    case D3D12_MESSAGE_CATEGORY_STATE_GETTING:
        return "state_getting";
    case D3D12_MESSAGE_CATEGORY_RESOURCE_MANIPULATION:
        return "resource_manipulation";
    case D3D12_MESSAGE_CATEGORY_EXECUTION:
        return "execution";
    case D3D12_MESSAGE_CATEGORY_SHADER:
        return "shader";
    default:
        return "unknown";
    }
}

std::string FormatInfoQueueReport(
    const InfoQueueCollectionResult &collection)
{
    std::string report = "D3D12 InfoQueue messages: status=";
    report += FormatHResultCode(collection.status);
    report += "; available=";
    report += BooleanName(collection.infoQueueAvailable);
    report += "; stored=";
    report += std::to_string(collection.storedMessageCount);
    report += "; collected=";
    report += std::to_string(collection.messages.size());
    report += "; collection_limit_exceeded=";
    report += BooleanName(collection.collectionLimitExceeded);
    report += "; cleared=";
    report += BooleanName(collection.storedMessagesCleared);
    report += "; corruption=";
    report += std::to_string(collection.corruptionCount);
    report += "; errors=";
    report += std::to_string(collection.errorCount);
    report += "; warnings=";
    report += std::to_string(collection.warningCount);
    report += "; information=";
    report += std::to_string(collection.informationCount);
    report += "; messages=";
    report += std::to_string(collection.messageCount);
    report += "; unknown_severity=";
    report += std::to_string(collection.unknownSeverityCount);
    report += "; run_failure=";
    report += BooleanName(collection.hasRunFailure);
    report += "; warning_suppression=none";

    for (std::size_t index = 0; index < collection.messages.size(); ++index)
    {
        const auto &message = collection.messages[index];
        report += "\n  message[";
        report += std::to_string(index);
        report += "]: severity=";
        report += InfoQueueSeverityName(message.severity);
        report += "; category=";
        report += InfoQueueCategoryName(message.category);
        report += "; id=";
        report += std::to_string(static_cast<std::int32_t>(message.id));
        report += "; description=";
        report += message.description;
    }

    return report;
}
} // namespace renderlab::gfx::d3d12
