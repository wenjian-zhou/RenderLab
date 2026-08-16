#pragma once

namespace renderlab::gfx::d3d12
{
struct DiagnosticsConfig
{
    bool enableDebugLayer = false;
    bool enableGpuBasedValidation = false;
    bool enableDred = true;
};

[[nodiscard]] constexpr DiagnosticsConfig DefaultDiagnosticsConfig() noexcept
{
    DiagnosticsConfig config = {};

#if defined(_DEBUG)
    config.enableDebugLayer = true;
#endif

    return config;
}
} // namespace renderlab::gfx::d3d12