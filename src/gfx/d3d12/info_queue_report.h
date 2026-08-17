#pragma once

#include "gfx/d3d12/info_queue_messages.h"

#include <string>
#include <string_view>

namespace renderlab::gfx::d3d12
{
[[nodiscard]] std::string_view InfoQueueSeverityName(
    D3D12_MESSAGE_SEVERITY severity) noexcept;

[[nodiscard]] std::string_view InfoQueueCategoryName(
    D3D12_MESSAGE_CATEGORY category) noexcept;

[[nodiscard]] std::string FormatInfoQueueReport(
    const InfoQueueCollectionResult &collection);
} // namespace renderlab::gfx::d3d12
