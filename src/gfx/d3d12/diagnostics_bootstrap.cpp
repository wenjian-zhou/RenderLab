#include "gfx/d3d12/diagnostics_bootstrap.h"

#include <wrl/client.h>

namespace renderlab::gfx::d3d12
{
namespace
{
    using Microsoft::WRL::ComPtr;
}

DiagnosticsState ConfigureD3D12Diagnostics(const DiagnosticsConfig &config) noexcept
{
    DiagnosticsState state = {};

    if (config.enableDebugLayer || config.enableGpuBasedValidation)
    {
        ComPtr<ID3D12Debug> debugController;
        state.status = D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
        if (FAILED(state.status))
        {
            return state;
        }

        debugController->EnableDebugLayer();
        state.debugLayerEnabled = true;

        if (config.enableGpuBasedValidation)
        {
            ComPtr<ID3D12Debug1> debugController1;
            state.status = debugController.As(&debugController1);
            if (FAILED(state.status))
            {
                return state;
            }

            debugController1->SetEnableGPUBasedValidation(TRUE);
            state.gpuBasedValidationEnabled = true;
        }
    }

    if (config.enableDred)
    {
        ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
        state.status = D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings));
        if (FAILED(state.status))
        {
            return state;
        }

        dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        state.dredConfigured = true;
    }

    return state;
}
} // namespace renderlab::gfx::d3d12