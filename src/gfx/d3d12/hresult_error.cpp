#include "gfx/d3d12/hresult_error.h"

#include <dxgi.h>

#include <array>
#include <cstdio>
#include <utility>

namespace renderlab::gfx::d3d12
{
HResultCategory ClassifyHResult(HRESULT status) noexcept
{
    if (SUCCEEDED(status))
    {
        return HResultCategory::success;
    }

    switch (status)
    {
    case DXGI_ERROR_DEVICE_REMOVED:
        return HResultCategory::deviceRemoved;
    case DXGI_ERROR_DEVICE_RESET:
        return HResultCategory::deviceReset;
    case DXGI_ERROR_DEVICE_HUNG:
        return HResultCategory::deviceHung;
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
        return HResultCategory::driverInternalError;
    case DXGI_ERROR_INVALID_CALL:
        return HResultCategory::invalidCall;
    case E_INVALIDARG:
        return HResultCategory::invalidArgument;
    case E_OUTOFMEMORY:
        return HResultCategory::outOfMemory;
    case E_NOINTERFACE:
        return HResultCategory::noInterface;
    default:
        return HResultCategory::failure;
    }
}

std::string_view HResultCategoryName(HResultCategory category) noexcept
{
    switch (category)
    {
    case HResultCategory::success:
        return "success";
    case HResultCategory::deviceRemoved:
        return "device_removed";
    case HResultCategory::deviceReset:
        return "device_reset";
    case HResultCategory::deviceHung:
        return "device_hung";
    case HResultCategory::driverInternalError:
        return "driver_internal_error";
    case HResultCategory::invalidCall:
        return "invalid_call";
    case HResultCategory::invalidArgument:
        return "invalid_argument";
    case HResultCategory::outOfMemory:
        return "out_of_memory";
    case HResultCategory::noInterface:
        return "no_interface";
    case HResultCategory::failure:
        return "failure";
    }

    return "unknown";
}

std::string FormatHResultCode(HRESULT status)
{
    char buffer[11] = {};
    sprintf_s(
        buffer,
        "0x%08lX",
        static_cast<unsigned long>(status));
    return buffer;
}

std::string FormatHResultSystemMessage(HRESULT status)
{
    std::array<char, 1024> buffer{};
    const DWORD characterCount = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(status),
        0,
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        nullptr);

    if (characterCount == 0)
    {
        return "unavailable";
    }

    std::string message(buffer.data(), characterCount);
    for (char &character : message)
    {
        if (character == '\r' || character == '\n' || character == '\t')
        {
            character = ' ';
        }
    }

    while (!message.empty() && message.back() == ' ')
    {
        message.pop_back();
    }

    return message.empty() ? "unavailable" : message;
}

std::string FormatHResultDiagnostic(
    const HResultDiagnosticData &data)
{
    std::string diagnostic = "D3D12 HRESULT failure: operation=";
    diagnostic += data.operation;
    diagnostic += "; status=";
    diagnostic += FormatHResultCode(data.status);
    diagnostic += "; category=";
    diagnostic += HResultCategoryName(ClassifyHResult(data.status));
    diagnostic += "; message=";
    diagnostic += FormatHResultSystemMessage(data.status);
    diagnostic += "; source=";
    diagnostic += data.sourceFile.empty() ? "unavailable" : data.sourceFile;
    diagnostic += ':';
    diagnostic += std::to_string(data.sourceLine);

    if (!data.functionName.empty())
    {
        diagnostic += "; function=";
        diagnostic += data.functionName;
    }

    return diagnostic;
}

HResultError::HResultError(
    HRESULT status,
    std::string_view operation,
    std::source_location location)
    : std::runtime_error(FormatHResultDiagnostic({
          .status = status,
          .operation = operation,
          .sourceFile = location.file_name(),
          .sourceLine = location.line(),
          .functionName = location.function_name(),
      })),
      status_(status),
      operation_(operation),
      location_(location)
{
}

HRESULT HResultError::status() const noexcept
{
    return status_;
}

std::string_view HResultError::operation() const noexcept
{
    return operation_;
}

const std::source_location &HResultError::location() const noexcept
{
    return location_;
}

void ThrowIfFailed(
    HRESULT status,
    std::string_view operation,
    std::source_location location)
{
    if (FAILED(status))
    {
        throw HResultError(status, operation, location);
    }
}
} // namespace renderlab::gfx::d3d12
