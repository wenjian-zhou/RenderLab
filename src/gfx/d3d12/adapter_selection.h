#pragma once

#include <cstddef>
#include <optional>
#include <span>

namespace renderlab::gfx::d3d12
{
struct AdapterCandidate
{
    bool isSoftware = false;
    bool supportsD3D12 = false;
};

[[nodiscard]] std::optional<std::size_t> SelectHardwareAdapter(std::span<const AdapterCandidate> candidates) noexcept;
} // namespace renderlab::gfx::d3d12