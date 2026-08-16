#pragma once

#include "gfx/d3d12/diagnostics_config.h"

#include <d3d12.h>

namespace renderlab::gfx::d3d12
{
struct DiagnosticsState
{
    HRESULT status = S_OK;
    bool debugLayerEnabled = false;
    bool gpuBasedValidationEnabled = false;
    bool dredConfigured = false;
};

[[nodiscard]] DiagnosticsState ConfigureD3D12Diagnostics(const DiagnosticsConfig &config) noexcept;
} // namespace renderlab::gfx::d3d12