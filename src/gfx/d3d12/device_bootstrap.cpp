#include "gfx/d3d12/device_bootstrap.h"

#include <utility>

namespace renderlab::gfx::d3d12
{
DeviceBootstrapResult CreateDeviceBootstrap(
    Microsoft::WRL::ComPtr<IDXGIAdapter4> selectedAdapter,
    bool debugLayerEnabled) noexcept
{
    DeviceBootstrapResult result = {};
    result.selectedAdapter = std::move(selectedAdapter);

    if (!result.selectedAdapter)
    {
        result.status = E_INVALIDARG;
        return result;
    }

    result.status = D3D12CreateDevice(
        result.selectedAdapter.Get(),
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&result.device));

    if (FAILED(result.status))
    {
        return result;
    }

    if (!result.device)
    {
        result.status = E_UNEXPECTED;
        return result;
    }

    result.infoQueueStatus =
        result.device.As(&result.infoQueue);

    if (SUCCEEDED(result.infoQueueStatus))
    {
        if (!result.infoQueue)
        {
            result.status = E_UNEXPECTED;
            return result;
        }

        result.infoQueueCreated = true;
    }
    else if (debugLayerEnabled || result.infoQueueStatus != E_NOINTERFACE)
    {
        result.status = result.infoQueueStatus;
        return result;
    }

    if (result.infoQueue)
    {
        const bool enableBreaks = IsDebuggerPresent() != FALSE;

        if (enableBreaks)
        {
            result.status = result.infoQueue->SetBreakOnSeverity(
                D3D12_MESSAGE_SEVERITY_CORRUPTION,
                TRUE);
            if (FAILED(result.status))
            {
                return result;
            }

            result.status = result.infoQueue->SetBreakOnSeverity(
                D3D12_MESSAGE_SEVERITY_ERROR,
                TRUE);
            if (FAILED(result.status))
            {
                return result;
            }
        }

        result.infoQueuePolicyConfigured = true;
        result.infoQueueBreaksEnabled = enableBreaks;
    }

    result.status = result.device->SetName(L"RenderLab D3D12 Device");
    if (FAILED(result.status))
    {
        return result;
    }

    D3D12_COMMAND_QUEUE_DESC queueDescription{
        .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
        .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
        .NodeMask = 0,
    };

    result.directQueueStatus =
        result.device->CreateCommandQueue(
            &queueDescription,
            IID_PPV_ARGS(&result.directQueue));

    if (FAILED(result.directQueueStatus))
    {
        result.status = result.directQueueStatus;
        return result;
    }

    if (!result.directQueue)
    {
        result.directQueueStatus = E_UNEXPECTED;
        result.status = E_UNEXPECTED;
        return result;
    }

    result.directQueueCreated = true;

    result.directQueueNameStatus =
        result.directQueue->SetName(L"RenderLab D3D12 Direct Queue");

    if (FAILED(result.directQueueNameStatus))
    {
        result.status = result.directQueueNameStatus;
        return result;
    }

    result.directQueueNamed = true;

    result.status = S_OK;
    return result;
}
} // namespace renderlab::gfx::d3d12
