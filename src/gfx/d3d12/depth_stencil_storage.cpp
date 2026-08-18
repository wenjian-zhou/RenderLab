#include "gfx/d3d12/depth_stencil_storage.h"

#include <stdexcept>

#include "gfx/d3d12/hresult_error.h"

namespace renderlab::gfx::d3d12
{
DepthStencilStorage CreateDepthStencilStorage(
    ID3D12Device &device)
{
    const D3D12_DESCRIPTOR_HEAP_DESC heapDescription{
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
        .NumDescriptors = DepthStencilDescriptorCount,
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
        .NodeMask = 0,
    };

    DepthStencilStorage storage = {};
    ThrowIfFailed(
        device.CreateDescriptorHeap(
            &heapDescription,
            IID_PPV_ARGS(&storage.dsvHeap)),
        "ID3D12Device::CreateDescriptorHeap(FixedDSV)");

    if (!storage.dsvHeap)
    {
        throw std::runtime_error(
            "DSV descriptor heap creation succeeded without returning a heap");
    }

    ThrowIfFailed(
        storage.dsvHeap->SetName(L"RenderLab Fixed DSV Heap"),
        "ID3D12DescriptorHeap::SetName(FixedDSV)");

    storage.descriptorIncrement =
        device.GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    if (storage.descriptorIncrement == 0)
    {
        throw std::runtime_error(
            "DSV descriptor handle increment is zero");
    }

    return storage;
}

D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilHandle(
    const DepthStencilStorage &storage)
{
    if (!storage.dsvHeap)
    {
        throw std::logic_error(
            "DSV descriptor heap has not been created");
    }

    if (storage.descriptorIncrement == 0)
    {
        throw std::logic_error(
            "DSV descriptor increment has not been initialized");
    }

    return storage.dsvHeap->GetCPUDescriptorHandleForHeapStart();
}
} // namespace renderlab::gfx::d3d12
