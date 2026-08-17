#pragma once

#include <windows.h>

#include <cstdint>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

namespace renderlab::gfx::d3d12
{
enum class HResultCategory
{
    success,
    deviceRemoved,
    deviceReset,
    deviceHung,
    driverInternalError,
    invalidCall,
    invalidArgument,
    outOfMemory,
    noInterface,
    failure,
};

struct HResultDiagnosticData
{
    HRESULT status = E_FAIL;
    std::string_view operation;
    std::string_view sourceFile;
    std::uint_least32_t sourceLine = 0;
    std::string_view functionName;
};

[[nodiscard]] HResultCategory ClassifyHResult(HRESULT status) noexcept;
[[nodiscard]] std::string_view HResultCategoryName(HResultCategory category) noexcept;
[[nodiscard]] std::string FormatHResultCode(HRESULT status);
[[nodiscard]] std::string FormatHResultSystemMessage(HRESULT status);
[[nodiscard]] std::string FormatHResultDiagnostic(
    const HResultDiagnosticData &data);

class HResultError final : public std::runtime_error
{
public:
    HResultError(
        HRESULT status,
        std::string_view operation,
        std::source_location location = std::source_location::current());

    [[nodiscard]] HRESULT status() const noexcept;
    [[nodiscard]] std::string_view operation() const noexcept;
    [[nodiscard]] const std::source_location &location() const noexcept;

private:
    HRESULT status_ = E_FAIL;
    std::string operation_;
    std::source_location location_;
};

void ThrowIfFailed(
    HRESULT status,
    std::string_view operation,
    std::source_location location = std::source_location::current());
} // namespace renderlab::gfx::d3d12
