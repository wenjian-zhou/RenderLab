#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace renderlab::gfx::d3d12
{
struct DeviceBootstrapResult
{
    HRESULT status = E_FAIL;
    HRESULT infoQueueStatus = E_NOINTERFACE;
    HRESULT directQueueStatus = E_FAIL;
    HRESULT directQueueNameStatus = E_FAIL;

    bool infoQueueCreated = false;
    bool infoQueuePolicyConfigured = false;
    bool infoQueueBreaksEnabled = false;
    bool directQueueCreated = false;
    bool directQueueNamed = false;

    // Declaration order makes destruction run Queue, InfoQueue, Device, then Adapter.
    Microsoft::WRL::ComPtr<IDXGIAdapter4> selectedAdapter;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> directQueue;

    DeviceBootstrapResult() = default;
    DeviceBootstrapResult(DeviceBootstrapResult &&) noexcept = default;
    DeviceBootstrapResult &operator=(DeviceBootstrapResult &&) noexcept = default;

    DeviceBootstrapResult(const DeviceBootstrapResult &) = delete;
    DeviceBootstrapResult &operator=(const DeviceBootstrapResult &) = delete;
};

[[nodiscard]] DeviceBootstrapResult CreateDeviceBootstrap(
    Microsoft::WRL::ComPtr<IDXGIAdapter4> selectedAdapter,
    bool debugLayerEnabled) noexcept;
} // namespace renderlab::gfx::d3d12
