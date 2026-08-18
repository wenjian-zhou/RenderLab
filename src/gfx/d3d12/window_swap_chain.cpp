#include "gfx/d3d12/window_swap_chain.h"

#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "gfx/d3d12/hresult_error.h"

namespace renderlab::gfx::d3d12
{
D3D12_CPU_DESCRIPTOR_HANDLE OffsetCpuDescriptorHandle(
    D3D12_CPU_DESCRIPTOR_HANDLE heapStart,
    UINT descriptorIncrement,
    UINT descriptorCount,
    UINT descriptorIndex)
{
    if (descriptorCount == 0)
    {
        throw std::invalid_argument(
            "Descriptor count must be greater than zero");
    }

    if (descriptorIncrement == 0)
    {
        throw std::invalid_argument(
            "Descriptor increment must be greater than zero");
    }

    if (descriptorIndex >= descriptorCount)
    {
        throw std::out_of_range(
            "Descriptor index is outside the descriptor heap");
    }

    const SIZE_T index = static_cast<SIZE_T>(descriptorIndex);
    const SIZE_T increment = static_cast<SIZE_T>(descriptorIncrement);
    if (index > ((std::numeric_limits<SIZE_T>::max)() - heapStart.ptr) /
                    increment)
    {
        throw std::overflow_error(
            "CPU descriptor handle arithmetic overflow");
    }

    return D3D12_CPU_DESCRIPTOR_HANDLE{
        .ptr = heapStart.ptr + index * increment,
    };
}

D3D12_CPU_DESCRIPTOR_HANDLE GetBackBufferRtvHandle(
    const WindowSwapChain &windowSwapChain,
    UINT bufferIndex)
{
    if (!windowSwapChain.rtvHeap)
    {
        throw std::logic_error(
            "Swap-chain RTV heap has not been created");
    }

    return OffsetCpuDescriptorHandle(
        windowSwapChain.rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        windowSwapChain.rtvDescriptorIncrement,
        SwapChainBufferCount,
        bufferIndex);
}

WindowSwapChain CreateWindowSwapChain(
    IDXGIFactory6 &factory,
    ID3D12CommandQueue &directQueue,
    HWND window
)
{
    if (window == nullptr || !IsWindow(window))
    {
        throw std::invalid_argument(
            "CreateWindowSwapChain requires a valid HWND");
    }

    RECT clientRectangle = {};
    if (!GetClientRect(window, &clientRectangle))
    {
        throw std::system_error(
            static_cast<int>(GetLastError()),
            std::system_category(),
            "GetClientRect");
    }

    const LONG clientWidth =
        clientRectangle.right - clientRectangle.left;
    const LONG clientHeight =
        clientRectangle.bottom - clientRectangle.top;

    if (clientWidth <= 0 || clientHeight <= 0)
    {
        throw std::runtime_error(
            "CreateWindowSwapChain requires a nonzero client size");
    }

    if (static_cast<unsigned long long>(clientWidth) >
            (std::numeric_limits<UINT>::max)() ||
        static_cast<unsigned long long>(clientHeight) >
            (std::numeric_limits<UINT>::max)())
    {
        throw std::overflow_error(
            "Swap-chain client size exceeds UINT range");
    }

    WindowSwapChain result = {};
    result.width = static_cast<UINT>(clientWidth);
    result.height = static_cast<UINT>(clientHeight);

    const DXGI_SWAP_CHAIN_DESC1 description{
        .Width = result.width,
        .Height = result.height,
        .Format = SwapChainFormat,
        .Stereo = FALSE,
        .SampleDesc = {
            .Count = 1,
            .Quality = 0,
        },
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = SwapChainBufferCount,
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
        .Flags = 0,
    };

    Microsoft::WRL::ComPtr<IDXGISwapChain1> baseSwapChain;

    ThrowIfFailed(
        factory.CreateSwapChainForHwnd(
            &directQueue,
            window,
            &description,
            nullptr,
            nullptr,
            &baseSwapChain),
        "IDXGIFactory6::CreateSwapChainForHwnd");

    if (!baseSwapChain)
    {
        throw std::runtime_error(
            "CreateSwapChainForHwnd succeeded without returning a swap chain");
    }

    ThrowIfFailed(
        baseSwapChain.As(&result.swapChain),
        "IDXGISwapChain1::QueryInterface(IDXGISwapChain3)");

    if (!result.swapChain)
    {
        throw std::runtime_error(
            "IDXGISwapChain3 query succeeded without returning an interface");
    }

    ThrowIfFailed(
        factory.MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER),
        "IDXGIFactory6::MakeWindowAssociation(DXGI_MWA_NO_ALT_ENTER)");

    return result;
}

void CreateSwapChainRenderTargetStorage(
    ID3D12Device &device,
    WindowSwapChain &windowSwapChain)
{
    if (!windowSwapChain.swapChain)
    {
        throw std::invalid_argument(
            "CreateSwapChainRenderTargetStorage requires a swap chain");
    }

    if (windowSwapChain.rtvHeap)
    {
        throw std::logic_error(
            "Swap-chain RTV storage is already initialized");
    }

    for (const auto &backBuffer : windowSwapChain.backBuffers)
    {
        if (backBuffer)
        {
            throw std::logic_error(
                "Swap-chain back-buffer storage is already initialized");
        }
    }

    const D3D12_DESCRIPTOR_HEAP_DESC heapDescription{
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        .NumDescriptors = SwapChainBufferCount,
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
        .NodeMask = 0,
    };

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ThrowIfFailed(
        device.CreateDescriptorHeap(
            &heapDescription,
            IID_PPV_ARGS(&rtvHeap)),
        "ID3D12Device::CreateDescriptorHeap(SwapChainRTV)");

    if (!rtvHeap)
    {
        throw std::runtime_error(
            "RTV descriptor heap creation succeeded without returning a heap");
    }

    ThrowIfFailed(
        rtvHeap->SetName(L"RenderLab Swap Chain RTV Heap"),
        "ID3D12DescriptorHeap::SetName(SwapChainRTV)");

    const UINT descriptorIncrement =
        device.GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    if (descriptorIncrement == 0)
    {
        throw std::runtime_error(
            "RTV descriptor handle increment is zero");
    }

    std::array<
        Microsoft::WRL::ComPtr<ID3D12Resource>,
        SwapChainBufferCount>
        backBuffers;

    const D3D12_RENDER_TARGET_VIEW_DESC rtvDescription{
        .Format = SwapChainFormat,
        .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
        .Texture2D = {
            .MipSlice = 0,
            .PlaneSlice = 0,
        },
    };

    const D3D12_CPU_DESCRIPTOR_HANDLE heapStart =
        rtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT bufferIndex = 0;
         bufferIndex < SwapChainBufferCount;
         ++bufferIndex)
    {
        const std::string getBufferOperation =
            "IDXGISwapChain3::GetBuffer[" +
            std::to_string(bufferIndex) + ']';
        ThrowIfFailed(
            windowSwapChain.swapChain->GetBuffer(
                bufferIndex,
                IID_PPV_ARGS(&backBuffers[bufferIndex])),
            getBufferOperation);

        if (!backBuffers[bufferIndex])
        {
            throw std::runtime_error(
                "GetBuffer succeeded without returning buffer index " +
                std::to_string(bufferIndex));
        }

        const std::wstring bufferName =
            L"RenderLab Swap Chain Back Buffer " +
            std::to_wstring(bufferIndex);
        const std::string setNameOperation =
            "ID3D12Resource::SetName(SwapChainBackBuffer[" +
            std::to_string(bufferIndex) + "])";
        ThrowIfFailed(
            backBuffers[bufferIndex]->SetName(bufferName.c_str()),
            setNameOperation);

        const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
            OffsetCpuDescriptorHandle(
                heapStart,
                descriptorIncrement,
                SwapChainBufferCount,
                bufferIndex);
        device.CreateRenderTargetView(
            backBuffers[bufferIndex].Get(),
            &rtvDescription,
            rtvHandle);
    }

    // Publish only complete storage. Failed HRESULT operations release these
    // local COM objects and leave the owner uninitialized.
    windowSwapChain.rtvHeap = std::move(rtvHeap);
    windowSwapChain.backBuffers = std::move(backBuffers);
    windowSwapChain.rtvDescriptorIncrement = descriptorIncrement;
}
} // namespace renderlab::gfx::d3d12
