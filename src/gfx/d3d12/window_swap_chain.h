#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>

namespace renderlab::gfx::d3d12
{
inline constexpr UINT SwapChainBufferCount = 3;
inline constexpr DXGI_FORMAT SwapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

// Loop 3 smoke policy: VSync enabled, no tearing flags.
inline constexpr UINT PresentSyncInterval = 1;
inline constexpr UINT PresentFlags = 0;

struct WindowSwapChain
{
    // Reverse declaration-order destruction releases the back buffers, RTV
    // heap, and swap chain in that order.
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
    std::array<
        Microsoft::WRL::ComPtr<ID3D12Resource>,
        SwapChainBufferCount>
        backBuffers;

    UINT rtvDescriptorIncrement = 0;
    UINT width = 0;
    UINT height = 0;

    WindowSwapChain() = default;
    WindowSwapChain(WindowSwapChain &&) noexcept = default;
    WindowSwapChain &operator=(WindowSwapChain &&) noexcept = default;

    WindowSwapChain(const WindowSwapChain &) = delete;
    WindowSwapChain &operator=(const WindowSwapChain &) = delete;
};

[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE OffsetCpuDescriptorHandle(
    D3D12_CPU_DESCRIPTOR_HANDLE heapStart,
    UINT descriptorIncrement,
    UINT descriptorCount,
    UINT descriptorIndex);

[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetBackBufferRtvHandle(
    const WindowSwapChain &windowSwapChain,
    UINT bufferIndex);

[[nodiscard]] WindowSwapChain CreateWindowSwapChain(
    IDXGIFactory6 &factory,
    ID3D12CommandQueue &directQueue,
    HWND window);

void CreateSwapChainRenderTargetStorage(
    ID3D12Device &device,
    WindowSwapChain &windowSwapChain);
} // namespace renderlab::gfx::d3d12
