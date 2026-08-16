#pragma once

#include "gfx/d3d12/adapter_selection.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <optional>
#include <vector>

namespace renderlab::gfx::d3d12
{
struct AdapterInfo
{
    AdapterCandidate candidate{};
    UINT preferenceIndex = 0;
    DXGI_ADAPTER_DESC3 properties{};
    HRESULT probeStatus = E_FAIL;
    HRESULT driverVersionStatus = E_FAIL;
    LARGE_INTEGER driverVersion{};
    Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter;
};

struct AdapterDiscoveryResult
{
    HRESULT status = S_OK;
    std::vector<AdapterInfo> adapters;
    std::optional<std::size_t> selectedIndex;
};

[[nodiscard]] AdapterDiscoveryResult DiscoverHardwareAdapters(
    IDXGIFactory6 &factory);
} // namespace renderlab::gfx::d3d12
