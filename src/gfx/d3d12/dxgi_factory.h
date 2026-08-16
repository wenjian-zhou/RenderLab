#pragma once

#include <dxgi1_6.h>
#include <wrl/client.h>

namespace renderlab::gfx::d3d12
{
[[nodiscard]] HRESULT CreateDxgiFactory(
    bool enableDebugLayer,
    Microsoft::WRL::ComPtr<IDXGIFactory6> &factory) noexcept;
} // namespace renderlab::gfx::d3d12