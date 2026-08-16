#include "gfx/d3d12/adapter_selection.h"

namespace renderlab::gfx::d3d12
{
std::optional<std::size_t> SelectHardwareAdapter(std::span<const AdapterCandidate> candidates) noexcept
{
    for (std::size_t index = 0; index < candidates.size(); ++index)
    {
        const AdapterCandidate& candidate = candidates[index];

        if (candidate.isSoftware)
        {
            continue;
        }

        if (!candidate.supportsD3D12)
        {
            continue;
        }

        return index;
    }

    return std::nullopt;
}
}