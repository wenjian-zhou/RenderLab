#pragma once

#include "gfx/d3d12/diagnostics_bootstrap.h"

#include <string>

namespace renderlab::gfx::d3d12
{
[[nodiscard]] std::string FormatDiagnosticsReport(
    const DiagnosticsConfig &config,
    const DiagnosticsState &state);
} // namespace renderlab::gfx::d3d12