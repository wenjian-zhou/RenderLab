#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace renderlab::gfx::d3d12
{
inline constexpr UINT DepthStencilDescriptorCount = 1;

struct DepthStencilStorage
{
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap;
    UINT descriptorIncrement = 0;

    DepthStencilStorage() = default;
    DepthStencilStorage(DepthStencilStorage &&) noexcept = default;
    DepthStencilStorage &operator=(DepthStencilStorage &&) noexcept = default;

    DepthStencilStorage(const DepthStencilStorage &) = delete;
    DepthStencilStorage &operator=(const DepthStencilStorage &) = delete;
};

[[nodiscard]] DepthStencilStorage CreateDepthStencilStorage(
    ID3D12Device &device);

[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilHandle(
    const DepthStencilStorage &storage);
} // namespace renderlab::gfx::d3d12
