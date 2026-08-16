#include "gfx/d3d12/adapter_discovery.h"

#include <utility>
#include <vector>

namespace renderlab::gfx::d3d12
{
AdapterDiscoveryResult DiscoverHardwareAdapters(IDXGIFactory6 &factory)
{
    AdapterDiscoveryResult result = {};
    std::vector<AdapterCandidate> candidates;

    for (UINT adapterIndex = 0;; ++adapterIndex)
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter;

        const HRESULT enumerationStatus =
            factory.EnumAdapterByGpuPreference(
                adapterIndex,
                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&adapter));

        if (enumerationStatus == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }

        if (FAILED(enumerationStatus))
        {
            result.status = enumerationStatus;
            return result;
        }

        AdapterInfo info = {};
        info.preferenceIndex = adapterIndex;
        info.adapter = std::move(adapter);

        const HRESULT descriptionStatus =
            info.adapter->GetDesc3(&info.properties);

        if (FAILED(descriptionStatus))
        {
            result.status = descriptionStatus;
            return result;
        }

        info.candidate.isSoftware =
            (info.properties.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) != 0;

        info.probeStatus =
            D3D12CreateDevice(
                info.adapter.Get(),
                D3D_FEATURE_LEVEL_11_0,
                __uuidof(ID3D12Device),
                nullptr);

        info.candidate.supportsD3D12 = SUCCEEDED(info.probeStatus);

        info.driverVersionStatus =
            info.adapter->CheckInterfaceSupport(
                __uuidof(ID3D12Device),
                &info.driverVersion);

        candidates.push_back(info.candidate);
        result.adapters.push_back(std::move(info));
    }

    result.selectedIndex = SelectHardwareAdapter(candidates);
    return result;
}
} // namespace renderlab::gfx::d3d12
